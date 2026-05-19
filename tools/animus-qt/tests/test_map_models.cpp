#include "maps/MapSourceRegistry.h"
#include "maps/CesiumBridge.h"
#include "maps/OfflineMapManager.h"
#include "maps/qgc/AnimusMapCacheManager.h"
#include "models/VehicleModel.h"
#include "models/VehicleModelProfileManager.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/MavlinkDecoder.h"
#include "telemetry/TelemetryService.h"

#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <array>
#include <memory>

namespace
{

std::unique_ptr<QObject> createMap2DView(animus::VehicleModel *vehicle,
                                         animus::BreadcrumbPathModel *breadcrumbs,
                                         animus::MapSourceRegistry *mapSources,
                                         animus::OfflineMapManager *offlineMaps,
                                         animus::AnimusMapCacheManager *mapCache,
                                         animus::TelemetryService *telemetry,
                                         QQmlEngine *engine)
{
    engine->rootContext()->setContextProperty(QStringLiteral("vehicleModel"), vehicle);
    engine->rootContext()->setContextProperty(QStringLiteral("breadcrumbModel"), breadcrumbs);
    engine->rootContext()->setContextProperty(QStringLiteral("mapSources"), mapSources);
    engine->rootContext()->setContextProperty(QStringLiteral("offlineMaps"), offlineMaps);
    engine->rootContext()->setContextProperty(QStringLiteral("mapCache"), mapCache);
    engine->rootContext()->setContextProperty(QStringLiteral("telemetryService"), telemetry);

    const QUrl url =
        QUrl::fromLocalFile(QStringLiteral(ANIMUS_QT_QML_DIR) + QStringLiteral("/Map2DView.qml"));
    QQmlComponent component(engine, url, QQmlComponent::PreferSynchronous);
    if (component.isError())
        qWarning().noquote() << component.errors();
    std::unique_ptr<QObject> object(component.create());
    if (!object)
        qWarning().noquote() << component.errors();
    return object;
}

QByteArray pngTile()
{
    return QByteArray::fromHex("89504e470d0a1a0a0000000d4948445200000001000000010806000000"
                               "1f15c4890000000d49444154789c6360f8ffff3f0005fe02fea73581"
                               "840000000049454e44ae426082");
}

int countOccurrences(const QString &text, const QString &needle)
{
    int count = 0;
    int offset = 0;
    while ((offset = text.indexOf(needle, offset)) != -1)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}

quint16 crcAccumulate(unsigned char data, quint16 crc)
{
    unsigned char tmp = data ^ static_cast<unsigned char>(crc & 0xffU);
    tmp ^= static_cast<unsigned char>(tmp << 4U);
    return static_cast<quint16>((crc >> 8U) ^ (static_cast<quint16>(tmp) << 8U) ^
                                (static_cast<quint16>(tmp) << 3U) ^
                                (static_cast<quint16>(tmp) >> 4U));
}

void appendLe16(QByteArray *bytes, quint16 value)
{
    bytes->append(static_cast<char>(value & 0xffU));
    bytes->append(static_cast<char>((value >> 8U) & 0xffU));
}

void writeLe16(QByteArray *bytes, int offset, quint16 value)
{
    (*bytes)[offset] = static_cast<char>(value & 0xffU);
    (*bytes)[offset + 1] = static_cast<char>((value >> 8U) & 0xffU);
}

QByteArray mavlinkV1Frame(quint8 msgId,
                          const QByteArray &payload,
                          quint8 crcExtra,
                          quint8 systemId = 1,
                          quint8 componentId = 1)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xfeU));
    frame.append(static_cast<char>(payload.size()));
    frame.append(static_cast<char>(1U));
    frame.append(static_cast<char>(systemId));
    frame.append(static_cast<char>(componentId));
    frame.append(static_cast<char>(msgId));
    frame.append(payload);

    quint16 checksum = 0xffffU;
    for (int index = 1; index < frame.size(); ++index)
        checksum = crcAccumulate(static_cast<unsigned char>(frame.at(index)), checksum);
    checksum = crcAccumulate(crcExtra, checksum);
    appendLe16(&frame, checksum);
    return frame;
}

QByteArray servoOutputRawFrame(const std::array<quint16, 8> &servoPwm)
{
    QByteArray payload(21, '\0');
    for (int index = 0; index < static_cast<int>(servoPwm.size()); ++index)
        writeLe16(&payload, 4 + index * 2, servoPwm[static_cast<std::size_t>(index)]);
    payload[20] = 0;
    return mavlinkV1Frame(36, payload, 222);
}

QVariantMap surfaceById(const QVariantList &surfaces, const QString &id)
{
    for (const QVariant &surfaceValue : surfaces)
    {
        const QVariantMap surface = surfaceValue.toMap();
        if (surface.value(QStringLiteral("id")).toString() == id)
            return surface;
    }
    return {};
}

QString bundledModelProfilesDir()
{
    return QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium/models"));
}

class TileHttpServer final : public QTcpServer
{
    Q_OBJECT

  public:
    explicit TileHttpServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this,
                &QTcpServer::newConnection,
                this,
                [this]()
                {
                    QTcpSocket *socket = nextPendingConnection();
                    connect(socket,
                            &QTcpSocket::readyRead,
                            socket,
                            [socket]()
                            {
                                socket->readAll();
                                const QByteArray body = pngTile();
                                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n"
                                              "Content-Length: " +
                                              QByteArray::number(body.size()) +
                                              "\r\nConnection: close\r\n\r\n" + body);
                                socket->disconnectFromHost();
                            });
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                });
    }
};

} // namespace

class AnimusQtMapModelTests final : public QObject
{
    Q_OBJECT

  private slots:
    void strictOfflineRejectsNetworkSource()
    {
        animus::MapSourceRegistry registry;
        animus::OfflineMapManager manager(&registry);

        manager.setMode(animus::OfflineMapManager::StrictOffline);

        QVERIFY(manager.canUseSource(QStringLiteral("offline-cache")));
        QVERIFY(!manager.canUseSource(QStringLiteral("osm")));
        QVERIFY(!manager.canUseSource(QStringLiteral("missing-source")));
        QCOMPARE(manager.sourceBlockReason(QStringLiteral("osm")),
                 QStringLiteral("Strict offline blocks network map sources"));
    }

    void qgcProviderRegistryExposesLicensedDefaults()
    {
        animus::AnimusMapCacheManager manager;

        QCOMPARE(manager.rowCount(), 3);
        QCOMPARE(manager.activeProviderId(), QStringLiteral("offline-cache"));
        QCOMPARE(manager.activePluginName(), QStringLiteral("QGroundControl"));
        QCOMPARE(manager.activeMapTypeId(), QStringLiteral("offline"));
        QCOMPARE(manager.providerIndex(QStringLiteral("offline-cache")), 2);
        QVERIFY(manager.providerRequiresNetwork(QStringLiteral("osm-street")));
        QVERIFY(!manager.providerRequiresNetwork(QStringLiteral("offline-cache")));
        QVERIFY(manager.providerConfigured(QStringLiteral("offline-cache")));
        QVERIFY(!manager.providerConfigured(QStringLiteral("operator-raster")));
        QCOMPARE(manager.providerBlockReason(QStringLiteral("operator-raster"), true),
                 QStringLiteral("Operator Raster requires an operator-configured tile URL"));
    }

    void offlinePolicyBlocksNetworkProviders()
    {
        animus::AnimusMapCacheManager manager;

        QCOMPARE(manager.providerBlockReason(QStringLiteral("osm-street"), false),
                 QStringLiteral("OpenStreetMap requires online map policy"));
        QCOMPARE(manager.providerBlockReason(QStringLiteral("offline-cache"), false), QString());
    }

    void tileCountEstimateUsesWebMercatorBounds()
    {
        animus::AnimusMapCacheManager manager;

        const int tiles = manager.estimateTileCount(-122.25, 37.36, -122.05, 37.50, 12, 12);
        QVERIFY(tiles > 0);
        QVERIFY(tiles < 40);
        QCOMPARE(manager.estimateTileCount(-122.05, 37.36, -122.25, 37.50, 12, 12), 0);
        QVERIFY(manager.estimateSizeMb(tiles) > 0.0);
    }

    void defaultCruise6DofTileSetSeedsExpectedBounds()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.ensureDefaultCruise6DofTileSet());
        QCOMPARE(manager.tileSets().size(), 1);

        const QVariantMap tileSet = manager.tileSets().constFirst().toMap();
        QCOMPARE(tileSet.value(QStringLiteral("id")).toString(),
                 QStringLiteral("cruise6dof-5mi-origin"));
        QCOMPARE(tileSet.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Cruise 6DOF 5mi Origin"));
        QCOMPARE(tileSet.value(QStringLiteral("providerId")).toString(),
                 QStringLiteral("osm-street"));
        QCOMPARE(tileSet.value(QStringLiteral("minZoom")).toInt(), 12);
        QCOMPARE(tileSet.value(QStringLiteral("maxZoom")).toInt(), 15);
        QCOMPARE(tileSet.value(QStringLiteral("tileCount")).toInt(), 438);
        QCOMPARE(tileSet.value(QStringLiteral("status")).toString(), QStringLiteral("empty"));
        QCOMPARE(tileSet.value(QStringLiteral("cachedCount")).toInt(), 0);
        QCOMPARE(tileSet.value(QStringLiteral("missingCount")).toInt(), 438);
        QCOMPARE(tileSet.value(QStringLiteral("west")).toDouble(), -122.2607248);
        QCOMPARE(tileSet.value(QStringLiteral("south")).toDouble(), 37.3552151);
        QCOMPARE(tileSet.value(QStringLiteral("east")).toDouble(), -122.0786752);
        QCOMPARE(tileSet.value(QStringLiteral("north")).toDouble(), 37.4997849);
    }

    void tileUrlPrefersCachedFilesAndHonorsOfflinePolicy()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.ensureDefaultCruise6DofTileSet());

        const int zoom = 12;
        const int x = 657;
        const int y = 1588;
        const QString cachedPath =
            manager.cachedTilePathFor(QStringLiteral("osm-street"), zoom, x, y);
        QVERIFY(QDir().mkpath(QFileInfo(cachedPath).absolutePath()));
        QFile tile(cachedPath);
        QVERIFY(tile.open(QIODevice::WriteOnly));
        QVERIFY(tile.write("cached") > 0);
        tile.close();

        QVERIFY(manager.tileUrlFor(QStringLiteral("osm-street"), zoom, x, y, false)
                    .startsWith(QStringLiteral("file:")));
        QVERIFY(manager.tileUrlFor(QStringLiteral("offline-cache"), zoom, x, y, false)
                    .startsWith(QStringLiteral("file:")));
        QCOMPARE(manager.tileUrlFor(QStringLiteral("osm-street"), zoom, x + 1, y, false),
                 QString());
        QCOMPARE(manager.tileUrlFor(QStringLiteral("offline-cache"), zoom, x + 1, y, true),
                 QString());
        QCOMPARE(manager.tileUrlFor(QStringLiteral("osm-street"), zoom, x + 1, y, true), QString());
    }

    void cacheDbInitializesAndTracksTileSets()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.initializeCache());
        QVERIFY(QFile::exists(manager.cacheDatabasePath()));
        QCOMPARE(manager.tileSets().size(), 0);

        QVERIFY(manager.createTileSet(
            QStringLiteral("Stanford"), -122.25, 37.36, -122.05, 37.50, 12, 13));
        QCOMPARE(manager.tileSets().size(), 1);
        const QVariantMap tileSet = manager.tileSets().constFirst().toMap();
        QCOMPARE(tileSet.value(QStringLiteral("name")).toString(), QStringLiteral("Stanford"));
        QCOMPARE(tileSet.value(QStringLiteral("status")).toString(), QStringLiteral("empty"));
        QVERIFY(tileSet.value(QStringLiteral("tileCount")).toInt() > 0);

        QVERIFY(manager.downloadTileSet(tileSet.value(QStringLiteral("id")).toString()));
        QVERIFY(manager.cancelTileSetDownload(tileSet.value(QStringLiteral("id")).toString()));

        QVERIFY(manager.deleteTileSet(tileSet.value(QStringLiteral("id")).toString()));
        QCOMPARE(manager.tileSets().size(), 0);
    }

    void tileSetImportExportRoundTripsMetadata()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        const int zoom = 12;
        const int x = 657;
        const int y = 1588;
        const QString cachedPath =
            manager.cachedTilePathFor(QStringLiteral("osm-street"), zoom, x, y);
        QVERIFY(QDir().mkpath(QFileInfo(cachedPath).absolutePath()));
        QFile tile(cachedPath);
        QVERIFY(tile.open(QIODevice::WriteOnly));
        QVERIFY(tile.write(pngTile()) > 0);
        tile.close();
        QVERIFY(manager.createTileSet(
            QStringLiteral("Exported Stanford"), -122.25, 37.36, -122.05, 37.50, 12, 12));
        const QString id =
            manager.tileSets().constFirst().toMap().value(QStringLiteral("id")).toString();

        const QString exportPath = root.filePath(QStringLiteral("tile-set-export"));
        QVERIFY(manager.exportTileSet(id, exportPath));

        animus::AnimusMapCacheManager imported;
        imported.setRootPath(root.filePath(QStringLiteral("imported")));
        QVERIFY(imported.importTileSet(exportPath));
        QCOMPARE(imported.tileSets().size(), 1);
        QCOMPARE(imported.tileSets().constFirst().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Exported Stanford"));
    }

    void asyncDownloaderFetchesTilesFromLocalServer()
    {
        TileHttpServer server;
        if (!server.listen(QHostAddress(QHostAddress::LocalHost), 0))
            QSKIP("Sandbox does not permit local TCP listen sockets");
        const QByteArray url =
            QStringLiteral("http://127.0.0.1:%1/{z}/{x}/{y}.png").arg(server.serverPort()).toUtf8();
        qputenv("ANIMUS_QT_OPERATOR_TILE_URL", url);

        QTemporaryDir root;
        QVERIFY(root.isValid());
        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        manager.setActiveProviderId(QStringLiteral("operator-raster"));
        QVERIFY(
            manager.createTileSet(QStringLiteral("Local tile"), -180.0, -80.0, 180.0, 80.0, 0, 0));
        const QString id =
            manager.tileSets().constFirst().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(manager.downloadTileSet(id));
        QTRY_COMPARE(manager.cachedTileCount(), 1);
        QCOMPARE(manager.failedTileCount(), 0);
        QVERIFY(manager.tileUrlFor(QStringLiteral("operator-raster"), 0, 0, 0, false)
                    .startsWith(QStringLiteral("file:")));
        qunsetenv("ANIMUS_QT_OPERATOR_TILE_URL");
    }

    void map2dQmlLoadsWithCacheContext()
    {
        animus::VehicleModel vehicle;
        vehicle.setLatitudeDeg(37.4275);
        vehicle.setLongitudeDeg(-122.1697);
        animus::BreadcrumbPathModel breadcrumbs;
        animus::MapSourceRegistry mapSources;
        animus::OfflineMapManager offlineMaps(&mapSources);
        animus::AnimusMapCacheManager mapCache;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        QQmlEngine engine;

        std::unique_ptr<QObject> map = createMap2DView(
            &vehicle, &breadcrumbs, &mapSources, &offlineMaps, &mapCache, &telemetry, &engine);
        QVERIFY(map);
        map->setProperty("width", 800);
        map->setProperty("height", 600);

        QCOMPARE(map->property("following").toBool(), true);
        const int defaultZoom = map->property("zoomLevel").toInt();
        QVERIFY(QMetaObject::invokeMethod(map.get(), "zoomBy", Q_ARG(QVariant, 1)));
        QCOMPARE(map->property("zoomLevel").toInt(), defaultZoom + 1);
        QVERIFY(QMetaObject::invokeMethod(
            map.get(), "panByPixels", Q_ARG(QVariant, 32), Q_ARG(QVariant, 0)));
        QCOMPARE(map->property("following").toBool(), false);
        QVERIFY(QMetaObject::invokeMethod(map.get(), "recenterOnVehicle"));
        QCOMPARE(map->property("following").toBool(), true);
    }

    void mavlinkDecoderParsesServoOutputRaw()
    {
        const QByteArray frame =
            servoOutputRawFrame({1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800});

        animus::MavlinkDecoder decoder;
        const QVector<animus::MavlinkTelemetrySample> samples = decoder.decodeDatagram(frame);

        QCOMPARE(samples.size(), 1);
        const animus::MavlinkTelemetrySample sample = samples.constFirst();
        QCOMPARE(sample.systemId, 1);
        QCOMPARE(sample.componentId, 1);
        QCOMPARE(sample.hasServoOutputRaw, true);
        for (int index = 0; index < 8; ++index)
        {
            QCOMPARE(sample.servoOutputValid[index], true);
            QCOMPARE(sample.servoOutputPwm[index], 1100 + index * 100);
        }
        QCOMPARE(sample.servoOutputValid[8], false);

        QByteArray corrupted = frame;
        corrupted[corrupted.size() - 1] =
            static_cast<char>(corrupted.at(corrupted.size() - 1) ^ 0x01);
        QVERIFY(decoder.decodeDatagram(corrupted).isEmpty());
    }

    void telemetryServiceAppliesServoOutputRaw()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel breadcrumbs;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        QSignalSpy actuatorSpy(&vehicle, &animus::VehicleModel::actuatorChanged);

        QVERIFY(telemetry.ingestDatagram(servoOutputRawFrame({0, 1250, 1500, 1750, 0, 0, 0, 0})));
        QVERIFY(
            QMetaObject::invokeMethod(&telemetry, "publishPendingSample", Qt::DirectConnection));

        QCOMPARE(vehicle.servoOutputValid(1), false);
        QCOMPARE(vehicle.servoOutputPwm(1), 0);
        QCOMPARE(vehicle.servoOutputValid(2), true);
        QCOMPARE(vehicle.servoOutputPwm(2), 1250);
        QCOMPARE(vehicle.servoOutputValid(3), true);
        QCOMPARE(vehicle.servoOutputPwm(3), 1500);
        QCOMPARE(vehicle.servoOutputValid(4), true);
        QCOMPARE(vehicle.servoOutputPwm(4), 1750);
        QVERIFY(actuatorSpy.count() >= 3);
    }

    void cesiumBridgeSnapshotExportsVehicleTerrainHomeAndTrail()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString terrainPath =
            root.filePath(QStringLiteral("map_cache/terrain/quantized-mesh"));
        QVERIFY(QDir().mkpath(terrainPath));
        QFile layer(QDir(terrainPath).filePath(QStringLiteral("layer.json")));
        QVERIFY(layer.open(QIODevice::WriteOnly));
        QVERIFY(layer.write("{}") > 0);
        layer.close();

        animus::VehicleModel vehicle;
        vehicle.setVehicleId(QStringLiteral("altair-7"));
        vehicle.setConnected(true);
        vehicle.setLatitudeDeg(37.42);
        vehicle.setLongitudeDeg(-122.17);
        vehicle.setAltitudeM(48.0);
        vehicle.setHeadingDeg(91.0);
        vehicle.setPositionValid(true);
        vehicle.setAttitudeValid(true);
        vehicle.setHomeLatitudeDeg(37.41);
        vehicle.setHomeLongitudeDeg(-122.16);
        vehicle.setHomeAltitudeM(12.0);
        vehicle.setHomeValid(true);
        vehicle.setTerrainLatitudeDeg(37.42);
        vehicle.setTerrainLongitudeDeg(-122.17);
        vehicle.setTerrainHeightM(22.5);
        vehicle.setTerrainCurrentHeightM(25.5);
        vehicle.setTerrainLoaded(4);
        vehicle.setTerrainPending(2);
        vehicle.setTerrainValid(true);

        animus::BreadcrumbPathModel trail;
        trail.setMinDistanceM(0.0);
        QVERIFY(trail.append(37.40, -122.18, 20.0, 1.0));
        QVERIFY(trail.append(37.42, -122.17, 48.0, 2.0));

        animus::CesiumBridge bridge(&vehicle, &trail);
        bridge.setTerrainCachePath(terrainPath);

        const QVariantMap snapshot = bridge.snapshot();
        const QVariantMap exportedVehicle = snapshot.value(QStringLiteral("vehicle")).toMap();
        QCOMPARE(exportedVehicle.value(QStringLiteral("id")).toString(),
                 QStringLiteral("altair-7"));
        QCOMPARE(exportedVehicle.value(QStringLiteral("positionValid")).toBool(), true);
        QCOMPARE(exportedVehicle.value(QStringLiteral("attitudeValid")).toBool(), true);

        const QVariantMap home = snapshot.value(QStringLiteral("home")).toMap();
        QCOMPARE(home.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(home.value(QStringLiteral("altitudeM")).toDouble(), 12.0);

        const QVariantList exportedTrail = snapshot.value(QStringLiteral("trail")).toList();
        QCOMPARE(exportedTrail.size(), 2);
        QCOMPARE(exportedTrail.constLast().toMap().value(QStringLiteral("altitudeM")).toDouble(),
                 48.0);

        const QVariantMap terrain = snapshot.value(QStringLiteral("terrain")).toMap();
        QCOMPARE(terrain.value(QStringLiteral("terrainAvailable")).toBool(), true);
        QCOMPARE(terrain.value(QStringLiteral("quantizedMeshAvailable")).toBool(), true);
        QCOMPARE(terrain.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("quantized-mesh"));
        QVERIFY(terrain.value(QStringLiteral("cacheUrl"))
                    .toString()
                    .startsWith(QStringLiteral("file:")));
        QVERIFY(terrain.value(QStringLiteral("cacheUrl")).toString().endsWith(QStringLiteral("/")));
        QVERIFY(!terrain.value(QStringLiteral("fixture")).toMap().isEmpty());
        QCOMPARE(terrain.value(QStringLiteral("loaded")).toInt(), 4);
        QCOMPARE(terrain.value(QStringLiteral("pending")).toInt(), 2);

        const QVariantMap model = snapshot.value(QStringLiteral("model")).toMap();
        QCOMPARE(model.value(QStringLiteral("profile")).toString(),
                 QStringLiteral("generic_fixed_wing_smooth"));
        QCOMPARE(model.value(QStringLiteral("asset")).toString(),
                 QStringLiteral("models/generic_fixed_wing_smooth.glb"));

        const QVariantList controlSurfaces =
            snapshot.value(QStringLiteral("controlSurfaces")).toList();
        QCOMPARE(controlSurfaces.size(), 4);
        const QStringList expectedNodes{QStringLiteral("aileron_left_pivot"),
                                        QStringLiteral("aileron_right_pivot"),
                                        QStringLiteral("elevator_pivot"),
                                        QStringLiteral("rudder_pivot")};
        for (const QVariant &surfaceValue : controlSurfaces)
        {
            const QVariantMap surface = surfaceValue.toMap();
            QVERIFY(!surface.value(QStringLiteral("id")).toString().isEmpty());
            QVERIFY(expectedNodes.contains(surface.value(QStringLiteral("node")).toString()));
            QCOMPARE(surface.value(QStringLiteral("deflectionDeg")).toDouble(), 0.0);
            QCOMPARE(surface.value(QStringLiteral("valid")).toBool(), false);
        }
    }

    void cesiumBridgeMapsServoOutputsToControlSurfaceDeflections()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::CesiumBridge bridge(&vehicle, &trail);
        QSignalSpy publishSpy(&bridge, &animus::CesiumBridge::latestVehicleChanged);

        QVariantList controlSurfaces =
            bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        const QVariantMap neutralLeft =
            surfaceById(controlSurfaces, QStringLiteral("left_aileron"));
        QCOMPARE(neutralLeft.value(QStringLiteral("valid")).toBool(), false);
        QCOMPARE(neutralLeft.value(QStringLiteral("deflectionDeg")).toDouble(), 0.0);

        vehicle.setServoOutputPwm(1, 1750, true);
        vehicle.setServoOutputPwm(2, 1750, true);
        vehicle.setServoOutputPwm(3, 1250, true);
        vehicle.setServoOutputPwm(4, 2000, true);
        QVERIFY(publishSpy.count() >= 4);

        controlSurfaces = bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        const QVariantMap left = surfaceById(controlSurfaces, QStringLiteral("left_aileron"));
        const QVariantMap right = surfaceById(controlSurfaces, QStringLiteral("right_aileron"));
        const QVariantMap elevator = surfaceById(controlSurfaces, QStringLiteral("elevator"));
        const QVariantMap rudder = surfaceById(controlSurfaces, QStringLiteral("rudder"));

        QCOMPARE(left.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(right.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(elevator.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(rudder.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(left.value(QStringLiteral("deflectionDeg")).toDouble(), 12.5);
        QCOMPARE(right.value(QStringLiteral("deflectionDeg")).toDouble(), -12.5);
        QCOMPARE(elevator.value(QStringLiteral("deflectionDeg")).toDouble(), -15.0);
        QCOMPARE(rudder.value(QStringLiteral("deflectionDeg")).toDouble(), 28.0);

        vehicle.setServoOutputPwm(1, 2500, true);
        controlSurfaces = bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        const QVariantMap clampedLeft =
            surfaceById(controlSurfaces, QStringLiteral("left_aileron"));
        QCOMPARE(clampedLeft.value(QStringLiteral("deflectionDeg")).toDouble(), 25.0);
    }

    void vehicleModelProfileManagerLoadsBundledDefault()
    {
        animus::VehicleModel vehicle;
        animus::VehicleModelProfileManager manager(bundledModelProfilesDir(), nullptr, &vehicle);

        QCOMPARE(manager.selectedProfileId(), QStringLiteral("generic_fixed_wing_smooth"));
        QCOMPARE(manager.profiles().size(), 1);
        const QVariantMap selected = manager.selectedProfile();
        QCOMPARE(selected.value(QStringLiteral("asset")).toString(),
                 QStringLiteral("models/generic_fixed_wing_smooth.glb"));

        const QVariantList surfaces = manager.surfaces();
        QCOMPARE(surfaces.size(), 4);
        const QVariantMap right = surfaceById(surfaces, QStringLiteral("right_aileron"));
        QCOMPARE(right.value(QStringLiteral("label")).toString(), QStringLiteral("Right aileron"));
        QCOMPARE(right.value(QStringLiteral("actuatorChannel")).toInt(), 2);
        QCOMPARE(right.value(QStringLiteral("node")).toString(),
                 QStringLiteral("aileron_right_pivot"));
        QCOMPARE(right.value(QStringLiteral("profilePolarity")).toDouble(), -1.0);
        QCOMPARE(right.value(QStringLiteral("polarity")).toDouble(), -1.0);
    }

    void vehicleModelProfileManagerReversesAndResetsSurfacePolarity()
    {
        animus::VehicleModel vehicle;
        animus::VehicleModelProfileManager manager(bundledModelProfilesDir(), nullptr, &vehicle);

        QCOMPARE(manager.mappedDeflectionDeg(QStringLiteral("left_aileron"), 1750), 12.5);
        manager.reverseSurfacePolarity(QStringLiteral("left_aileron"));
        QCOMPARE(manager.surfacePolarity(QStringLiteral("left_aileron")), -1.0);
        QCOMPARE(manager.mappedDeflectionDeg(QStringLiteral("left_aileron"), 1750), -12.5);

        QVariantMap left = surfaceById(manager.surfaces(), QStringLiteral("left_aileron"));
        QCOMPARE(left.value(QStringLiteral("polarityReversed")).toBool(), true);
        manager.resetSurfacePolarity(QStringLiteral("left_aileron"));
        QCOMPARE(manager.surfacePolarity(QStringLiteral("left_aileron")), 1.0);
        QCOMPARE(manager.mappedDeflectionDeg(QStringLiteral("left_aileron"), 1750), 12.5);
    }

    void vehicleModelProfileManagerSettingsRoundTrip()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QDir sourceDir(bundledModelProfilesDir());
        const QString profilesDir = root.filePath(QStringLiteral("profiles"));
        QVERIFY(QDir().mkpath(profilesDir));
        QVERIFY(QFile::copy(
            sourceDir.filePath(QStringLiteral("generic_fixed_wing_smooth.json")),
            QDir(profilesDir).filePath(QStringLiteral("generic_fixed_wing_smooth.json"))));

        QFile genericFile(
            QDir(profilesDir).filePath(QStringLiteral("generic_fixed_wing_smooth.json")));
        QVERIFY(genericFile.open(QIODevice::ReadOnly));
        QJsonDocument variant = QJsonDocument::fromJson(genericFile.readAll());
        genericFile.close();
        QJsonObject variantObject = variant.object();
        variantObject[QStringLiteral("id")] = QStringLiteral("test_variant");
        variantObject[QStringLiteral("name")] = QStringLiteral("Test Variant");
        variantObject[QStringLiteral("asset")] = QStringLiteral("models/test_variant.glb");
        QFile variantFile(QDir(profilesDir).filePath(QStringLiteral("test_variant.json")));
        QVERIFY(variantFile.open(QIODevice::WriteOnly));
        QVERIFY(variantFile.write(QJsonDocument(variantObject).toJson()) > 0);
        variantFile.close();

        const QString settingsPath = root.filePath(QStringLiteral("animus.ini"));
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            animus::VehicleModelProfileManager manager(profilesDir, &settings, nullptr);
            QCOMPARE(manager.profiles().size(), 2);
            manager.setSelectedProfileId(QStringLiteral("test_variant"));
            manager.reverseSurfacePolarity(QStringLiteral("left_aileron"));
            settings.sync();
        }

        QSettings settings(settingsPath, QSettings::IniFormat);
        animus::VehicleModelProfileManager restored(profilesDir, &settings, nullptr);
        QCOMPARE(restored.selectedProfileId(), QStringLiteral("test_variant"));
        QCOMPARE(restored.surfacePolarity(QStringLiteral("left_aileron")), -1.0);
        restored.resetAllSurfacePolarity();
        QCOMPARE(restored.surfacePolarity(QStringLiteral("left_aileron")), 1.0);
    }

    void cesiumBridgeReflectsProfileManagerPolarityOverrides()
    {
        animus::VehicleModel vehicle;
        vehicle.setServoOutputPwm(1, 1750, true);
        animus::BreadcrumbPathModel trail;
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge bridge(&vehicle, &trail, &profiles);

        QVariantList surfaces = bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        QCOMPARE(surfaceById(surfaces, QStringLiteral("left_aileron"))
                     .value(QStringLiteral("deflectionDeg"))
                     .toDouble(),
                 12.5);

        profiles.reverseSurfacePolarity(QStringLiteral("left_aileron"));
        surfaces = bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        QCOMPARE(surfaceById(surfaces, QStringLiteral("left_aileron"))
                     .value(QStringLiteral("deflectionDeg"))
                     .toDouble(),
                 -12.5);
        const QVariantList verification = bridge.controlSurfaceVerificationSnapshot()
                                              .value(QStringLiteral("controlSurfaces"))
                                              .toList();
        QCOMPARE(surfaceById(verification, QStringLiteral("left_aileron"))
                     .value(QStringLiteral("deflectionDeg"))
                     .toDouble(),
                 -12.0);
    }

    void cesiumBridgeRequiresQuantizedMeshLayerJson()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString terrainPath =
            root.filePath(QStringLiteral("map_cache/terrain/quantized-mesh"));
        QVERIFY(QDir().mkpath(terrainPath));
        QFile orphanTile(QDir(terrainPath).filePath(QStringLiteral("0.terrain")));
        QVERIFY(orphanTile.open(QIODevice::WriteOnly));
        QVERIFY(orphanTile.write("tile") > 0);
        orphanTile.close();

        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::CesiumBridge bridge(&vehicle, &trail);
        bridge.setTerrainCachePath(terrainPath);

        QVariantMap terrain = bridge.snapshot().value(QStringLiteral("terrain")).toMap();
        QCOMPARE(terrain.value(QStringLiteral("terrainAvailable")).toBool(), true);
        QCOMPARE(terrain.value(QStringLiteral("quantizedMeshAvailable")).toBool(), false);
        QCOMPARE(terrain.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("heightmap-fixture"));

        QFile layer(QDir(terrainPath).filePath(QStringLiteral("layer.json")));
        QVERIFY(layer.open(QIODevice::WriteOnly));
        QVERIFY(layer.write("{}") > 0);
        layer.close();
        bridge.setTerrainCachePath(root.filePath(QStringLiteral("map_cache/terrain/other")));
        bridge.setTerrainCachePath(terrainPath);

        terrain = bridge.snapshot().value(QStringLiteral("terrain")).toMap();
        QCOMPARE(terrain.value(QStringLiteral("terrainAvailable")).toBool(), true);
        QCOMPARE(terrain.value(QStringLiteral("quantizedMeshAvailable")).toBool(), true);
        QCOMPARE(terrain.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("quantized-mesh"));
    }

    void cesiumBridgeUsesHeightmapFixtureWhenTerrainCacheMissing()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::CesiumBridge bridge(&vehicle, &trail);
        bridge.setTerrainCachePath(root.filePath(QStringLiteral("missing-quantized-mesh-cache")));

        const QVariantMap terrain = bridge.snapshot().value(QStringLiteral("terrain")).toMap();
        QCOMPARE(terrain.value(QStringLiteral("terrainAvailable")).toBool(), true);
        QCOMPARE(terrain.value(QStringLiteral("quantizedMeshAvailable")).toBool(), false);
        QCOMPARE(terrain.value(QStringLiteral("provider")).toString(),
                 QStringLiteral("heightmap-fixture"));
        QCOMPARE(terrain.value(QStringLiteral("status")).toString(),
                 QStringLiteral("fixture terrain available"));
        const QVariantMap fixture = terrain.value(QStringLiteral("fixture")).toMap();
        QCOMPARE(fixture.value(QStringLiteral("name")).toString(),
                 QStringLiteral("cruise6dof-stanford-sim-fixture"));
        QCOMPARE(fixture.value(QStringLiteral("width")).toInt(), 129);
        QCOMPARE(fixture.value(QStringLiteral("height")).toInt(), 129);
        QCOMPARE(fixture.value(QStringLiteral("minHeightM")).toDouble(), 2.0);
        QCOMPARE(fixture.value(QStringLiteral("maxHeightM")).toDouble(), 58.0);
        QVERIFY(fixture.value(QStringLiteral("imageryUrlTemplate"))
                    .toString()
                    .startsWith(QStringLiteral("qrc:/Animus/web/cesium/fixture/tiles/")));
        QCOMPARE(fixture.value(QStringLiteral("imageryMaximumLevel")).toInt(), 2);
        QVERIFY(fixture.value(QStringLiteral("aircraftModelUrl"))
                    .toString()
                    .endsWith(QStringLiteral("fixture/aircraft/generic-fixed-wing.gltf")));

        QSignalSpy sceneSpy(&bridge, &animus::CesiumBridge::sceneStatusChanged);
        bridge.setSceneStatus(QStringLiteral("ellipsoid-fallback"), QStringLiteral("no assets"));
        QCOMPARE(sceneSpy.count(), 1);
        const QVariantMap scene = bridge.sceneStatus();
        QCOMPARE(scene.value(QStringLiteral("status")).toString(),
                 QStringLiteral("ellipsoid-fallback"));
        QCOMPARE(scene.value(QStringLiteral("error")).toString(), QStringLiteral("no assets"));
    }

    void bundledCesiumRuntimeFilesExist()
    {
        const QDir vendorDir(QDir(QStringLiteral(ANIMUS_QT_QML_DIR))
                                 .filePath(QStringLiteral("../web/cesium/vendor/Cesium")));
        QVERIFY2(QFileInfo::exists(vendorDir.filePath(QStringLiteral("Cesium.js"))),
                 "Cesium.js must be vendored for offline Terrain 3D rendering");
        QVERIFY2(QFileInfo::exists(vendorDir.filePath(QStringLiteral("Widgets/widgets.css"))),
                 "Cesium widgets CSS must be bundled");
        QVERIFY2(QFileInfo::exists(vendorDir.filePath(QStringLiteral("Workers"))),
                 "Cesium workers must be bundled");
        QVERIFY2(QFileInfo::exists(vendorDir.filePath(QStringLiteral("Assets"))),
                 "Cesium assets must be bundled");
        QVERIFY2(QFileInfo::exists(vendorDir.filePath(QStringLiteral("ThirdParty"))),
                 "Cesium third-party metadata must be bundled");
        QVERIFY2(QFileInfo::exists(vendorDir.filePath(QStringLiteral("LICENSE.md"))),
                 "Cesium Apache-2.0 license must be bundled");
    }

    void genericFixedWingModelProfileParses()
    {
        const QDir cesiumDir(
            QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium")));
        QFile profileFile(
            cesiumDir.filePath(QStringLiteral("models/generic_fixed_wing_smooth.json")));
        QVERIFY(profileFile.open(QIODevice::ReadOnly));
        const QJsonDocument document = QJsonDocument::fromJson(profileFile.readAll());
        QVERIFY(document.isObject());
        const QJsonObject profile = document.object();
        QCOMPARE(profile.value(QStringLiteral("id")).toString(),
                 QStringLiteral("generic_fixed_wing_smooth"));
        QCOMPARE(profile.value(QStringLiteral("asset")).toString(),
                 QStringLiteral("models/generic_fixed_wing_smooth.glb"));
        QCOMPARE(profile.value(QStringLiteral("scale")).toDouble(), 1.0);

        const QJsonArray surfaces = profile.value(QStringLiteral("surfaces")).toArray();
        QVERIFY(surfaces.size() >= 4);
        QStringList nodes;
        for (const QJsonValue &surfaceValue : surfaces)
        {
            const QJsonObject surface = surfaceValue.toObject();
            nodes.push_back(surface.value(QStringLiteral("node")).toString());
            QVERIFY(surface.value(QStringLiteral("axis")).isArray());
            QVERIFY(surface.value(QStringLiteral("pwm")).isObject());
            QVERIFY(surface.value(QStringLiteral("deflectionDeg")).isObject());
            QVERIFY(surface.value(QStringLiteral("actuatorChannel")).isDouble());
        }
        QVERIFY(nodes.contains(QStringLiteral("aileron_left_pivot")));
        QVERIFY(nodes.contains(QStringLiteral("aileron_right_pivot")));
        QVERIFY(nodes.contains(QStringLiteral("elevator_pivot")));
        QVERIFY(nodes.contains(QStringLiteral("rudder_pivot")));
    }

    void genericFixedWingGlbContainsPivotHierarchy()
    {
        const QDir cesiumDir(
            QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium")));
        QFile glbFile(cesiumDir.filePath(QStringLiteral("models/generic_fixed_wing_smooth.glb")));
        QVERIFY2(glbFile.open(QIODevice::ReadOnly),
                 "generic_fixed_wing_smooth.glb must be bundled for Terrain 3D");
        const QByteArray glb = glbFile.readAll();
        QVERIFY(glb.size() > 20);

        auto readLe32 = [&glb](int offset) -> quint32
        {
            const auto byte = reinterpret_cast<const uchar *>(glb.constData() + offset);
            return static_cast<quint32>(byte[0]) | (static_cast<quint32>(byte[1]) << 8) |
                   (static_cast<quint32>(byte[2]) << 16) | (static_cast<quint32>(byte[3]) << 24);
        };
        QCOMPARE(readLe32(0), 0x46546C67u);
        QCOMPARE(readLe32(4), 2u);
        QCOMPARE(readLe32(8), static_cast<quint32>(glb.size()));
        const quint32 jsonLength = readLe32(12);
        QCOMPARE(readLe32(16), 0x4E4F534Au);
        QVERIFY(20 + static_cast<int>(jsonLength) <= glb.size());

        const QJsonDocument document =
            QJsonDocument::fromJson(glb.mid(20, static_cast<int>(jsonLength)).trimmed());
        QVERIFY(document.isObject());
        const QJsonObject gltf = document.object();
        const QJsonObject asset = gltf.value(QStringLiteral("asset")).toObject();
        QCOMPARE(asset.value(QStringLiteral("version")).toString(), QStringLiteral("2.0"));
        QVERIFY(asset.value(QStringLiteral("generator"))
                    .toString()
                    .contains(QStringLiteral("Altair Animus procedural RC aircraft generator")));
        QCOMPARE(asset.value(QStringLiteral("copyright")).toString(),
                 QStringLiteral("Original procedural Altair asset; no third-party artwork."));
        const QJsonObject assetExtras = asset.value(QStringLiteral("extras")).toObject();
        QCOMPARE(assetExtras.value(QStringLiteral("source")).toString(),
                 QStringLiteral("tools/python/generate_animus_aircraft_model.py"));
        QCOMPARE(assetExtras.value(QStringLiteral("provenance")).toString(),
                 QStringLiteral("original deterministic procedural geometry"));

        const QJsonArray nodes = gltf.value(QStringLiteral("nodes")).toArray();
        QHash<QString, QJsonObject> nodesByName;
        for (const QJsonValue &nodeValue : nodes)
        {
            const QJsonObject node = nodeValue.toObject();
            nodesByName.insert(node.value(QStringLiteral("name")).toString(), node);
        }

        const QStringList requiredPivots{QStringLiteral("aileron_left_pivot"),
                                         QStringLiteral("aileron_right_pivot"),
                                         QStringLiteral("elevator_pivot"),
                                         QStringLiteral("rudder_pivot")};
        for (const QString &pivotName : requiredPivots)
        {
            QVERIFY2(nodesByName.contains(pivotName), qPrintable(pivotName));
            const QJsonObject pivot = nodesByName.value(pivotName);
            const QJsonArray children = pivot.value(QStringLiteral("children")).toArray();
            QVERIFY2(!children.isEmpty(), qPrintable(pivotName));
            const int childIndex = children.at(0).toInt(-1);
            QVERIFY(childIndex >= 0 && childIndex < nodes.size());
            QVERIFY(nodes.at(childIndex).toObject().value(QStringLiteral("mesh")).isDouble());
        }

        const QJsonObject root =
            nodesByName.value(QStringLiteral("generic_fixed_wing_smooth_root"));
        const QJsonArray rootRotation = root.value(QStringLiteral("rotation")).toArray();
        QCOMPARE(rootRotation.size(), 4);
        QCOMPARE(rootRotation.at(0).toDouble(), 0.0);
        QCOMPARE(rootRotation.at(1).toDouble(), 0.0);
        QVERIFY(qAbs(rootRotation.at(2).toDouble() - 0.7071067811865475) < 1.0e-12);
        QVERIFY(qAbs(rootRotation.at(3).toDouble() - 0.7071067811865476) < 1.0e-12);

        const QStringList expectedComponentNodes{QStringLiteral("fuselage"),
                                                 QStringLiteral("main_wing"),
                                                 QStringLiteral("horizontal_tail"),
                                                 QStringLiteral("vertical_stabilizer"),
                                                 QStringLiteral("canopy"),
                                                 QStringLiteral("propeller"),
                                                 QStringLiteral("spinner"),
                                                 QStringLiteral("landing_skid"),
                                                 QStringLiteral("aileron_left_surface"),
                                                 QStringLiteral("aileron_right_surface"),
                                                 QStringLiteral("elevator_surface"),
                                                 QStringLiteral("rudder_surface")};
        for (const QString &nodeName : expectedComponentNodes)
        {
            QVERIFY2(nodesByName.contains(nodeName), qPrintable(nodeName));
            QVERIFY2(nodesByName.value(nodeName).value(QStringLiteral("mesh")).isDouble(),
                     qPrintable(nodeName));
        }

        const QJsonArray componentNames = gltf.value(QStringLiteral("extras"))
                                              .toObject()
                                              .value(QStringLiteral("altairComponentNames"))
                                              .toArray();
        for (const QString &nodeName : expectedComponentNodes)
        {
            bool listed = false;
            for (const QJsonValue &componentValue : componentNames)
            {
                listed = listed || componentValue.toString() == nodeName;
            }
            QVERIFY2(listed, qPrintable(nodeName));
        }

        const QJsonArray materials = gltf.value(QStringLiteral("materials")).toArray();
        QStringList materialNames;
        for (const QJsonValue &materialValue : materials)
            materialNames.push_back(
                materialValue.toObject().value(QStringLiteral("name")).toString());
        QVERIFY(materialNames.contains(QStringLiteral("warm_white_foam_body")));
        QVERIFY(materialNames.contains(QStringLiteral("matte_charcoal_control_surfaces")));
        QVERIFY(materialNames.contains(QStringLiteral("clear_blue_canopy")));
        QVERIFY(materialNames.contains(QStringLiteral("safety_red_trim")));
        QVERIFY(materialNames.contains(QStringLiteral("dark_propeller_and_skids")));
        QVERIFY(materialNames.contains(QStringLiteral("brushed_spinner")));
    }

    void terrain3dStaticBundleUsesCesiumOnly()
    {
        const QDir cesiumDir(
            QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium")));
        QFile html(cesiumDir.filePath(QStringLiteral("index.html")));
        QVERIFY(html.open(QIODevice::ReadOnly));
        const QString htmlText = QString::fromUtf8(html.readAll());
        QVERIFY(!htmlText.contains(QStringLiteral("domScene")));
        QVERIFY(!htmlText.contains(QStringLiteral("domVehicle")));
        QVERIFY(!htmlText.contains(QStringLiteral("domTrail")));
        QVERIFY(htmlText.contains(QStringLiteral("vehicleModel.js")));

        QFile script(cesiumDir.filePath(QStringLiteral("animus-cesium.js")));
        QVERIFY(script.open(QIODevice::ReadOnly));
        const QString scriptText = QString::fromUtf8(script.readAll());
        QVERIFY(scriptText.contains(QStringLiteral("window.animusApplySnapshot")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusCaptureCesiumPng")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusInspectControlSurfaces")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusSetCameraMode")));
        QVERIFY(scriptText.contains(QStringLiteral("ANIMUS_CAMERA_MODE")));
        QVERIFY(scriptText.contains(QStringLiteral("installCameraControls")));
        QVERIFY(scriptText.contains(QStringLiteral("screenSpaceCameraController")));
        QVERIFY(scriptText.contains(QStringLiteral("CustomHeightmapTerrainProvider")));
        QVERIFY(scriptText.contains(QStringLiteral("UrlTemplateImageryProvider")));
        QVERIFY(scriptText.contains(QStringLiteral("aircraftModelUrl")));
        QVERIFY(scriptText.contains(QStringLiteral("VehicleModelController")));
        QVERIFY(scriptText.contains(QStringLiteral("applyControlSurfaces")));
        QVERIFY(scriptText.contains(QStringLiteral("models/${profile}.json")));
        QVERIFY(scriptText.contains(QStringLiteral("Cesium.Model.fromGltfAsync")));
        QVERIFY(scriptText.contains(QStringLiteral("upAxis: Cesium.Axis.Z")));
        QVERIFY(scriptText.contains(QStringLiteral("forwardAxis: Cesium.Axis.X")));
        QVERIFY(!scriptText.contains(QStringLiteral("colorBlendMode: Cesium.ColorBlendMode.MIX")));
        QVERIFY(scriptText.contains(QStringLiteral("vehicleModelProfile.asset")));
        QVERIFY(scriptText.contains(QStringLiteral("fallbackUri")));
        QVERIFY(scriptText.contains(QStringLiteral("Cesium.HeadingPitchRange")));

        QFile vehicleModelScript(cesiumDir.filePath(QStringLiteral("vehicleModel.js")));
        QVERIFY(vehicleModelScript.open(QIODevice::ReadOnly));
        const QString vehicleModelText = QString::fromUtf8(vehicleModelScript.readAll());
        QVERIFY(vehicleModelText.contains(QStringLiteral("class VehicleModelController")));
        QVERIFY(vehicleModelText.contains(QStringLiteral("applyControlSurfaces")));
        QVERIFY(vehicleModelText.contains(QStringLiteral("inspectControlSurfaces")));
        QVERIFY(vehicleModelText.contains(QStringLiteral("matrixChanged")));
        QVERIFY(vehicleModelText.contains(QStringLiteral("getNode")));
        QVERIFY(vehicleModelText.contains(QStringLiteral("originalMatrix")));
        QVERIFY(vehicleModelText.contains(QStringLiteral("nodeState.node.matrix")));

        QVERIFY(QFileInfo::exists(cesiumDir.filePath(QStringLiteral("fixture/tiles/0/0/0.png"))));
        QVERIFY(QFileInfo::exists(cesiumDir.filePath(QStringLiteral("fixture/tiles/1/1/1.png"))));
        QVERIFY(QFileInfo::exists(cesiumDir.filePath(QStringLiteral("fixture/tiles/2/3/3.png"))));
        QVERIFY(QFileInfo::exists(
            cesiumDir.filePath(QStringLiteral("fixture/aircraft/generic-fixed-wing.gltf"))));
        QVERIFY(QFileInfo::exists(
            cesiumDir.filePath(QStringLiteral("models/generic_fixed_wing_smooth.json"))));
        QVERIFY(QFileInfo::exists(
            cesiumDir.filePath(QStringLiteral("models/generic_fixed_wing_smooth.glb"))));
    }

    void setupViewExposesModelProfilePolarityControls()
    {
        QFile qml(QStringLiteral(ANIMUS_QT_QML_DIR) + QStringLiteral("/SetupView.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QString qmlText = QString::fromUtf8(qml.readAll());

        QVERIFY(qmlText.contains(QStringLiteral("Terrain 3D Vehicle Model")));
        QVERIFY(qmlText.contains(QStringLiteral("vehicleModelProfiles.profiles")));
        QVERIFY(qmlText.contains(QStringLiteral("selectedProfileId")));
        QVERIFY(qmlText.contains(QStringLiteral("selectedProfile.asset")));
        QVERIFY(qmlText.contains(QStringLiteral("vehicleModelProfiles.surfaces")));
        QVERIFY(qmlText.contains(QStringLiteral("actuatorChannel")));
        QVERIFY(qmlText.contains(QStringLiteral("deflectionDeg")));
        QVERIFY(qmlText.contains(QStringLiteral("reverseSurfacePolarity")));
        QVERIFY(qmlText.contains(QStringLiteral("resetSurfacePolarity")));
        QVERIFY(qmlText.contains(QStringLiteral("resetAllSurfacePolarity")));
    }

    void terrain3dCameraInputKeepsRotateSeparateFromPan()
    {
        const QDir cesiumDir(
            QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium")));
        QFile script(cesiumDir.filePath(QStringLiteral("animus-cesium.js")));
        QVERIFY(script.open(QIODevice::ReadOnly));
        const QString scriptText = QString::fromUtf8(script.readAll());

        QVERIFY(scriptText.contains(QStringLiteral("let cameraDrag = null;")));
        QVERIFY(scriptText.contains(QStringLiteral("function classifyCameraDrag(event)")));
        QVERIFY(scriptText.contains(QStringLiteral("const action = classifyCameraDrag(event);")));
        QVERIFY(scriptText.contains(QStringLiteral("action,")));
        QVERIFY(
            scriptText.contains(QStringLiteral("const implicitTrackpadPress = pressLikeEvent")));
        QVERIFY(scriptText.contains(QStringLiteral("if (cameraDrag.action === 'rotate')")));
        QVERIFY(scriptText.contains(QStringLiteral("function continueActiveCameraDrag(event)")));
        QVERIFY(
            scriptText.contains(QStringLiteral("if (!continueActiveCameraDrag(event)) return;")));
        QVERIFY(scriptText.contains(QStringLiteral("target.addEventListener('wheel'")));
        QVERIFY(scriptText.contains(QStringLiteral("if (spaceDown)")));
        QVERIFY(
            scriptText.contains(QStringLiteral("rotateCurrentCamera(event.deltaX, event.deltaY)")));
        QVERIFY(scriptText.contains(QStringLiteral("zoomCurrentCamera(event.deltaY)")));
        QVERIFY(!scriptText.contains(QStringLiteral(
            "pointerDrag.action === 'rotate' || (pointerDrag.button === 0 && spaceDown)")));

        QVERIFY(scriptText.contains(QStringLiteral("target.addEventListener('mousedown'")));
        QVERIFY(scriptText.contains(QStringLiteral("target.addEventListener('mousemove'")));
        QVERIFY(scriptText.contains(QStringLiteral("target.addEventListener('mouseup'")));
        QVERIFY(scriptText.contains(QStringLiteral("target.addEventListener('mouseleave'")));
        QVERIFY(scriptText.contains(
            QStringLiteral("const buttons = Number.isFinite(Number(event.buttons))")));
        QVERIFY(scriptText.contains(QStringLiteral("(buttons & 4) !== 0")));
        QVERIFY(scriptText.contains(QStringLiteral("(buttons & 1) !== 0")));
        QVERIFY(scriptText.contains(QStringLiteral("if (activePointerInteraction) return;")));

        QCOMPARE(countOccurrences(scriptText, QStringLiteral("switchToFreeFromPan()")), 2);
    }
};

QTEST_MAIN(AnimusQtMapModelTests)

#include "test_map_models.moc"
