#include "maps/MapSourceRegistry.h"
#include "maps/CesiumBridge.h"
#include "maps/OfflineMapManager.h"
#include "maps/qgc/AnimusMapCacheManager.h"
#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/TelemetryService.h"

#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

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

        QFile script(cesiumDir.filePath(QStringLiteral("animus-cesium.js")));
        QVERIFY(script.open(QIODevice::ReadOnly));
        const QString scriptText = QString::fromUtf8(script.readAll());
        QVERIFY(scriptText.contains(QStringLiteral("window.animusApplySnapshot")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusCaptureCesiumPng")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusSetCameraMode")));
        QVERIFY(scriptText.contains(QStringLiteral("CustomHeightmapTerrainProvider")));
        QVERIFY(scriptText.contains(QStringLiteral("UrlTemplateImageryProvider")));
        QVERIFY(scriptText.contains(QStringLiteral("aircraftModelUrl")));
        QVERIFY(scriptText.contains(QStringLiteral("Cesium.HeadingPitchRange")));

        QVERIFY(QFileInfo::exists(cesiumDir.filePath(QStringLiteral("fixture/tiles/0/0/0.png"))));
        QVERIFY(QFileInfo::exists(cesiumDir.filePath(QStringLiteral("fixture/tiles/1/1/1.png"))));
        QVERIFY(QFileInfo::exists(cesiumDir.filePath(QStringLiteral("fixture/tiles/2/3/3.png"))));
        QVERIFY(QFileInfo::exists(
            cesiumDir.filePath(QStringLiteral("fixture/aircraft/generic-fixed-wing.gltf"))));
    }
};

QTEST_MAIN(AnimusQtMapModelTests)

#include "test_map_models.moc"
