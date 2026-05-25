#include "maps/MapSourceRegistry.h"
#include "maps/CesiumBridge.h"
#include "maps/NavigationOverlayModels.h"
#include "maps/OfflineMapManager.h"
#include "maps/TerrainClearanceAnalyzer.h"
#include "maps/qgc/AnimusMapCacheManager.h"
#include "models/VehicleModel.h"
#include "models/VehicleModelProfileManager.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/MavlinkDecoder.h"
#include "telemetry/TelemetryService.h"
#include "ui/ThemeController.h"

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
#include <cstring>
#include <memory>

namespace
{

std::unique_ptr<QObject> createMap2DView(animus::VehicleModel *vehicle,
                                         animus::BreadcrumbPathModel *breadcrumbs,
                                         animus::NavigationOverlayModels *navigationOverlays,
                                         animus::MapSourceRegistry *mapSources,
                                         animus::OfflineMapManager *offlineMaps,
                                         animus::AnimusMapCacheManager *mapCache,
                                         animus::TelemetryService *telemetry,
                                         animus::ThemeController *theme,
                                         QQmlEngine *engine)
{
    engine->rootContext()->setContextProperty(QStringLiteral("vehicleModel"), vehicle);
    engine->rootContext()->setContextProperty(QStringLiteral("breadcrumbModel"), breadcrumbs);
    engine->rootContext()->setContextProperty(QStringLiteral("navigationOverlays"),
                                              navigationOverlays);
    engine->rootContext()->setContextProperty(QStringLiteral("mapSources"), mapSources);
    engine->rootContext()->setContextProperty(QStringLiteral("offlineMaps"), offlineMaps);
    engine->rootContext()->setContextProperty(QStringLiteral("mapCache"), mapCache);
    engine->rootContext()->setContextProperty(QStringLiteral("telemetryService"), telemetry);
    engine->rootContext()->setContextProperty(QStringLiteral("animusTheme"), theme);

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

std::unique_ptr<QObject> createWorkspaceShell(animus::VehicleModel *vehicle,
                                              animus::BreadcrumbPathModel *breadcrumbs,
                                              animus::NavigationOverlayModels *navigationOverlays,
                                              animus::MapSourceRegistry *mapSources,
                                              animus::OfflineMapManager *offlineMaps,
                                              animus::AnimusMapCacheManager *mapCache,
                                              animus::TelemetryService *telemetry,
                                              animus::VehicleModelProfileManager *profiles,
                                              animus::CesiumBridge *cesium,
                                              animus::ThemeController *theme,
                                              QQmlEngine *engine)
{
    engine->rootContext()->setContextProperty(QStringLiteral("vehicleModel"), vehicle);
    engine->rootContext()->setContextProperty(QStringLiteral("breadcrumbModel"), breadcrumbs);
    engine->rootContext()->setContextProperty(QStringLiteral("navigationOverlays"),
                                              navigationOverlays);
    engine->rootContext()->setContextProperty(QStringLiteral("mapSources"), mapSources);
    engine->rootContext()->setContextProperty(QStringLiteral("offlineMaps"), offlineMaps);
    engine->rootContext()->setContextProperty(QStringLiteral("mapCache"), mapCache);
    engine->rootContext()->setContextProperty(QStringLiteral("telemetryService"), telemetry);
    engine->rootContext()->setContextProperty(QStringLiteral("animusTheme"), theme);
    engine->rootContext()->setContextProperty(QStringLiteral("vehicleModelProfiles"), profiles);
    engine->rootContext()->setContextProperty(QStringLiteral("cesiumBridge"), cesium);
    engine->rootContext()->setContextProperty(QStringLiteral("webEngineTerrainEnabled"), false);

    const QUrl url = QUrl::fromLocalFile(QStringLiteral(ANIMUS_QT_QML_DIR) +
                                         QStringLiteral("/WorkspaceShell.qml"));
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

void writeLe32(QByteArray *bytes, int offset, quint32 value)
{
    (*bytes)[offset] = static_cast<char>(value & 0xffU);
    (*bytes)[offset + 1] = static_cast<char>((value >> 8U) & 0xffU);
    (*bytes)[offset + 2] = static_cast<char>((value >> 16U) & 0xffU);
    (*bytes)[offset + 3] = static_cast<char>((value >> 24U) & 0xffU);
}

void writeLeFloat(QByteArray *bytes, int offset, float value)
{
    quint32 raw = 0U;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&raw, &value, sizeof(value));
    (*bytes)[offset] = static_cast<char>(raw & 0xffU);
    (*bytes)[offset + 1] = static_cast<char>((raw >> 8U) & 0xffU);
    (*bytes)[offset + 2] = static_cast<char>((raw >> 16U) & 0xffU);
    (*bytes)[offset + 3] = static_cast<char>((raw >> 24U) & 0xffU);
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

QByteArray
attitudeFrame(float roll, float pitch, float yaw, float rollspeed, float pitchspeed, float yawspeed)
{
    QByteArray payload(28, '\0');
    writeLeFloat(&payload, 4, roll);
    writeLeFloat(&payload, 8, pitch);
    writeLeFloat(&payload, 12, yaw);
    writeLeFloat(&payload, 16, rollspeed);
    writeLeFloat(&payload, 20, pitchspeed);
    writeLeFloat(&payload, 24, yawspeed);
    return mavlinkV1Frame(30, payload, 39);
}

QByteArray heartbeatFrame(quint32 customMode = 0U,
                          quint8 vehicleType = 1U,
                          quint8 autopilot = 0U,
                          quint8 baseMode = 0x80U,
                          quint8 systemStatus = 4U)
{
    QByteArray payload(9, '\0');
    writeLe32(&payload, 0, customMode);
    payload[4] = static_cast<char>(vehicleType);
    payload[5] = static_cast<char>(autopilot);
    payload[6] = static_cast<char>(baseMode);
    payload[7] = static_cast<char>(systemStatus);
    payload[8] = 3;
    return mavlinkV1Frame(0, payload, 50);
}

QByteArray
sysStatusFrame(quint16 voltageMv = 12150U, qint16 currentCa = 345, qint8 remainingPct = 73)
{
    QByteArray payload(31, '\0');
    writeLe16(&payload, 14, voltageMv);
    writeLe16(&payload, 16, static_cast<quint16>(currentCa));
    payload[30] = static_cast<char>(remainingPct);
    return mavlinkV1Frame(1, payload, 124);
}

QByteArray vfrHudFrame(float airspeed,
                       float groundspeed,
                       qint16 heading,
                       quint16 throttle,
                       float altitude,
                       float climb)
{
    QByteArray payload(20, '\0');
    writeLeFloat(&payload, 0, airspeed);
    writeLeFloat(&payload, 4, groundspeed);
    writeLe16(&payload, 8, static_cast<quint16>(heading));
    writeLe16(&payload, 10, throttle);
    writeLeFloat(&payload, 12, altitude);
    writeLeFloat(&payload, 16, climb);
    return mavlinkV1Frame(74, payload, 20);
}

QByteArray gpsRawIntFrame()
{
    QByteArray payload(30, '\0');
    writeLe32(&payload, 8, static_cast<quint32>(qint32(374275000)));
    writeLe32(&payload, 12, static_cast<quint32>(qint32(-1221697000)));
    writeLe32(&payload, 16, 151000U);
    writeLe16(&payload, 20, 100U);
    writeLe16(&payload, 22, 150U);
    writeLe16(&payload, 24, 1810U);
    writeLe16(&payload, 26, 13000U);
    payload[28] = 3;
    payload[29] = 10;
    return mavlinkV1Frame(24, payload, 24);
}

QByteArray globalPositionFrame()
{
    QByteArray payload(28, '\0');
    writeLe32(&payload, 4, static_cast<quint32>(qint32(374275000)));
    writeLe32(&payload, 8, static_cast<quint32>(qint32(-1221697000)));
    writeLe32(&payload, 12, 151000U);
    writeLe32(&payload, 16, 151000U);
    writeLe16(&payload, 20, 1800U);
    writeLe16(&payload, 22, 100U);
    writeLe16(&payload, 24, static_cast<quint16>(qint16(-20)));
    writeLe16(&payload, 26, 13000U);
    return mavlinkV1Frame(33, payload, 104);
}

QByteArray missionCurrentFrame()
{
    QByteArray payload(2, '\0');
    writeLe16(&payload, 0, 2U);
    return mavlinkV1Frame(42, payload, 28);
}

QByteArray terrainReportFrame()
{
    QByteArray payload(22, '\0');
    writeLe32(&payload, 0, static_cast<quint32>(qint32(374275000)));
    writeLe32(&payload, 4, static_cast<quint32>(qint32(-1221697000)));
    writeLe16(&payload, 8, 100U);
    writeLeFloat(&payload, 10, 0.0F);
    writeLeFloat(&payload, 14, 151.0F);
    writeLe16(&payload, 18, 0U);
    writeLe16(&payload, 20, 1U);
    return mavlinkV1Frame(136, payload, 1);
}

QByteArray homePositionFrame()
{
    QByteArray payload(52, '\0');
    writeLe32(&payload, 0, static_cast<quint32>(qint32(374275000)));
    writeLe32(&payload, 4, static_cast<quint32>(qint32(-1221697000)));
    writeLe32(&payload, 8, 150000U);
    writeLeFloat(&payload, 24, 1.0F);
    return mavlinkV1Frame(242, payload, 104);
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
                    while (hasPendingConnections())
                    {
                        ++m_connectionCount;
                        QTcpSocket *socket = nextPendingConnection();
                        connect(socket,
                                &QTcpSocket::readyRead,
                                socket,
                                [this, socket]() { respondToTileRequest(socket); });
                        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                        respondToTileRequest(socket);
                    }
                });
    }

    int connectionCount() const
    {
        return m_connectionCount;
    }
    int requestCount() const
    {
        return m_requestCount;
    }
    QString lastRequestLine() const
    {
        return m_lastRequestLine;
    }

  private:
    void respondToTileRequest(QTcpSocket *socket)
    {
        if (socket->property("animusTileResponded").toBool() || socket->bytesAvailable() <= 0)
            return;

        socket->setProperty("animusTileResponded", true);
        const QByteArray request = socket->readAll();
        ++m_requestCount;
        const int lineEnd = request.indexOf('\n');
        m_lastRequestLine =
            QString::fromUtf8((lineEnd >= 0 ? request.left(lineEnd) : request).trimmed());

        const QByteArray body = pngTile();
        if (m_lastRequestLine.startsWith(QStringLiteral("GET ")))
        {
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n"
                          "Content-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        }
        else
        {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n"
                          "Connection: close\r\n\r\n");
        }
        socket->disconnectFromHost();
    }

    int m_connectionCount = 0;
    int m_requestCount = 0;
    QString m_lastRequestLine;
};

class ScopedEnvironmentVariable final
{
  public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)), m_previous(qgetenv(name))
    {
        qputenv(name, value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previous);
        else
            qunsetenv(m_name.constData());
    }

    Q_DISABLE_COPY_MOVE(ScopedEnvironmentVariable)

  private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previous;
};

} // namespace

class AnimusQtMapModelTests final : public QObject
{
    Q_OBJECT

  private slots:
    void themeControllerDefaultsToLight()
    {
        animus::ThemeController theme(nullptr);

        QCOMPARE(theme.mode(), QStringLiteral("light"));
        QCOMPARE(theme.displayName(), QStringLiteral("Light"));
        QCOMPARE(theme.dark(), false);
        QVERIFY(theme.text().isValid());
        QVERIFY(theme.surface().isValid());
    }

    void themeControllerInvalidPersistedModeFallsBackToLight()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(QDir(dir.path()).filePath(QStringLiteral("theme.ini")),
                           QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/themeMode"), QStringLiteral("sepia"));

        animus::ThemeController theme(&settings);

        QCOMPARE(theme.mode(), QStringLiteral("light"));
        QCOMPARE(theme.dark(), false);
    }

    void themeControllerPersistsAndEmitsChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(QDir(dir.path()).filePath(QStringLiteral("theme.ini")),
                           QSettings::IniFormat);
        animus::ThemeController theme(&settings);
        QSignalSpy modeSpy(&theme, &animus::ThemeController::modeChanged);
        QSignalSpy themeSpy(&theme, &animus::ThemeController::themeChanged);

        theme.setMode(QStringLiteral("dark"));
        QCOMPARE(theme.mode(), QStringLiteral("dark"));
        QCOMPARE(settings.value(QStringLiteral("ui/themeMode")).toString(), QStringLiteral("dark"));
        QCOMPARE(modeSpy.count(), 1);
        QCOMPARE(themeSpy.count(), 1);

        theme.toggleMode();
        QCOMPARE(theme.mode(), QStringLiteral("light"));
        QCOMPARE(settings.value(QStringLiteral("ui/themeMode")).toString(),
                 QStringLiteral("light"));
        QCOMPARE(modeSpy.count(), 2);
        QCOMPARE(themeSpy.count(), 2);
    }

    void themeControllerOverrideDoesNotPersist()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(QDir(dir.path()).filePath(QStringLiteral("theme.ini")),
                           QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/themeMode"), QStringLiteral("light"));
        animus::ThemeController theme(&settings);

        theme.setModeOverride(QStringLiteral("dark"));

        QCOMPARE(theme.mode(), QStringLiteral("dark"));
        QCOMPARE(settings.value(QStringLiteral("ui/themeMode")).toString(),
                 QStringLiteral("light"));
    }

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

    void captureFixtureSeedsDeterministicRasterTiles()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.seedDefaultCruise6DofFixtureTiles());
        QCOMPARE(manager.tileSets().size(), 1);
        const QVariantMap tileSet = manager.tileSets().constFirst().toMap();
        QCOMPARE(tileSet.value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
        QCOMPARE(tileSet.value(QStringLiteral("cachedCount")).toInt(),
                 tileSet.value(QStringLiteral("tileCount")).toInt());

        const QString url =
            manager.tileUrlFor(QStringLiteral("offline-cache"), 15, 5263, 12705, false);
        QVERIFY(url.startsWith(QStringLiteral("file:")));
        QVERIFY(!url.startsWith(QStringLiteral("http:")));
        QVERIFY(!url.startsWith(QStringLiteral("https:")));
        QVERIFY(QFileInfo::exists(QUrl(url).toLocalFile()));
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
        ScopedEnvironmentVariable operatorTileUrl("ANIMUS_QT_OPERATOR_TILE_URL", url);

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

        const bool requestObserved =
            QTest::qWaitFor([&server]() { return server.requestCount() > 0; }, 5000);
        if (!requestObserved && server.connectionCount() == 0)
            QSKIP("Sandbox permits local TCP listen but blocks the loopback tile download");
        if (!requestObserved)
        {
            const QString message =
                QStringLiteral("Local tile server accepted %1 connection(s) but no HTTP request; "
                               "manager.lastError='%2', activeStatus='%3'")
                    .arg(server.connectionCount())
                    .arg(manager.lastError(), manager.activeStatus());
            QFAIL(qPrintable(message));
        }

        QTRY_VERIFY_WITH_TIMEOUT(manager.cachedTileCount() == 1 || manager.failedTileCount() > 0,
                                 5000);
        manager.reloadTileSets();
        const QVariantMap tileSet = manager.tileSets().constFirst().toMap();
        const QString tileSetLastError = tileSet.value(QStringLiteral("lastError")).toString();
        const QString fileUrl =
            manager.tileUrlFor(QStringLiteral("operator-raster"), 0, 0, 0, false);
        if (manager.cachedTileCount() != 1 || manager.failedTileCount() > 0 ||
            !fileUrl.startsWith(QStringLiteral("file:")))
        {
            const QString message =
                QStringLiteral("Local tile request reached the cache manager but did not produce "
                               "one cached file tile: cached=%1 failed=%2 fileUrl='%3' "
                               "manager.lastError='%4' tileSet.lastError='%5' request='%6'")
                    .arg(manager.cachedTileCount())
                    .arg(manager.failedTileCount())
                    .arg(fileUrl, manager.lastError(), tileSetLastError, server.lastRequestLine());
            QFAIL(qPrintable(message));
        }
    }

    void map2dQmlLoadsWithCacheContext()
    {
        animus::VehicleModel vehicle;
        vehicle.setLatitudeDeg(37.4275);
        vehicle.setLongitudeDeg(-122.1697);
        animus::BreadcrumbPathModel breadcrumbs;
        animus::NavigationOverlayModels navigationOverlays;
        animus::MapSourceRegistry mapSources;
        animus::OfflineMapManager offlineMaps(&mapSources);
        animus::AnimusMapCacheManager mapCache;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        animus::ThemeController theme(nullptr);
        QQmlEngine engine;

        std::unique_ptr<QObject> map = createMap2DView(&vehicle,
                                                       &breadcrumbs,
                                                       &navigationOverlays,
                                                       &mapSources,
                                                       &offlineMaps,
                                                       &mapCache,
                                                       &telemetry,
                                                       &theme,
                                                       &engine);
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

    void navigationOverlayModelsValidateRolesAndFixture()
    {
        animus::NavigationOverlayModels overlays;

        QVERIFY(!overlays.missionItems()->append(
            1, QStringLiteral("bad"), 95.0, -122.0, 10.0, QStringLiteral("NAV_WAYPOINT"), false));
        QCOMPARE(overlays.missionItems()->rowCount(), 0);
        QVERIFY(overlays.missionItems()->append(
            1, QStringLiteral("WP1"), 37.42, -122.17, 80.0, QStringLiteral("NAV_WAYPOINT"), true));
        QCOMPARE(overlays.missionItems()->roleNames().value(animus::MissionItemModel::LatitudeRole),
                 QByteArray("latitudeDeg"));
        QCOMPARE(
            overlays.missionItems()
                ->data(overlays.missionItems()->index(0, 0), animus::MissionItemModel::CommandRole)
                .toString(),
            QStringLiteral("NAV_WAYPOINT"));

        QVERIFY(!overlays.geofences()->appendCircle(
            1, QStringLiteral("bad circle"), 37.42, -122.17, -10.0, true));
        QVERIFY(!overlays.geofences()->appendPolygon(
            2,
            QStringLiteral("bad polygon"),
            QVariantList{QVariantMap{{QStringLiteral("latitudeDeg"), 37.0},
                                     {QStringLiteral("longitudeDeg"), -122.0}}},
            true));

        overlays.seedCruise6DofFixture();
        QCOMPARE(overlays.missionItems()->rowCount(), 3);
        QCOMPARE(overlays.geofences()->rowCount(), 2);
        QCOMPARE(overlays.rallyPoints()->rowCount(), 2);
        QCOMPARE(overlays.eventMarkers()->rowCount(), 2);
        const QVariantMap exported = overlays.toVariantMap(1, true);
        QCOMPARE(exported.value(QStringLiteral("missionItems")).toList().size(), 3);
        QCOMPARE(exported.value(QStringLiteral("geofences")).toList().size(), 2);
        QCOMPARE(exported.value(QStringLiteral("rallyPoints")).toList().size(), 2);
        QCOMPARE(exported.value(QStringLiteral("eventMarkers")).toList().size(), 2);
        QCOMPARE(exported.value(QStringLiteral("activeMissionSeq")).toInt(), 1);
        QCOMPARE(exported.value(QStringLiteral("missionValid")).toBool(), true);

        overlays.clear();
        QCOMPARE(overlays.missionItems()->rowCount(), 0);
        QCOMPARE(overlays.geofences()->rowCount(), 0);
        QCOMPARE(overlays.rallyPoints()->rowCount(), 0);
        QCOMPARE(overlays.eventMarkers()->rowCount(), 0);
    }

    void cesiumBridgeSnapshotExportsNavigationOverlays()
    {
        animus::VehicleModel vehicle;
        vehicle.setMissionSeq(1);
        vehicle.setMissionValid(true);
        animus::BreadcrumbPathModel breadcrumbs;
        animus::NavigationOverlayModels overlays;
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge bridge(&vehicle, &breadcrumbs, &profiles, &overlays);
        QSignalSpy overlaysSpy(&bridge, &animus::CesiumBridge::overlaysChanged);

        overlays.seedCruise6DofFixture();
        QVERIFY(overlaysSpy.count() > 0);
        const QVariantMap overlaySnapshot =
            bridge.snapshot().value(QStringLiteral("overlays")).toMap();
        QCOMPARE(overlaySnapshot.value(QStringLiteral("missionItems")).toList().size(), 3);
        QCOMPARE(overlaySnapshot.value(QStringLiteral("geofences")).toList().size(), 2);
        QCOMPARE(overlaySnapshot.value(QStringLiteral("rallyPoints")).toList().size(), 2);
        QCOMPARE(overlaySnapshot.value(QStringLiteral("eventMarkers")).toList().size(), 2);
        QCOMPARE(overlaySnapshot.value(QStringLiteral("activeMissionSeq")).toInt(), 1);
        QCOMPARE(overlaySnapshot.value(QStringLiteral("missionValid")).toBool(), true);

        vehicle.setMissionSeq(2);
        const QVariantMap updated = bridge.snapshot().value(QStringLiteral("overlays")).toMap();
        QCOMPARE(updated.value(QStringLiteral("activeMissionSeq")).toInt(), 2);
    }

    void map2dOverlayDiagnosticsReportSeededLayers()
    {
        animus::VehicleModel vehicle;
        vehicle.setLatitudeDeg(37.4275);
        vehicle.setLongitudeDeg(-122.1697);
        vehicle.setHomeValid(true);
        vehicle.setMissionSeq(1);
        vehicle.setMissionValid(true);
        animus::BreadcrumbPathModel breadcrumbs;
        breadcrumbs.setMinDistanceM(0.0);
        QVERIFY(breadcrumbs.append(37.4275, -122.1697, 35.0, 0.0));
        animus::NavigationOverlayModels navigationOverlays;
        navigationOverlays.seedCruise6DofFixture();
        animus::MapSourceRegistry mapSources;
        animus::OfflineMapManager offlineMaps(&mapSources);
        animus::AnimusMapCacheManager mapCache;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        animus::ThemeController theme(nullptr);
        QQmlEngine engine;

        std::unique_ptr<QObject> map = createMap2DView(&vehicle,
                                                       &breadcrumbs,
                                                       &navigationOverlays,
                                                       &mapSources,
                                                       &offlineMaps,
                                                       &mapCache,
                                                       &telemetry,
                                                       &theme,
                                                       &engine);
        QVERIFY(map);
        map->setProperty("width", 800);
        map->setProperty("height", 600);

        QVariant diagnosticValue;
        QVERIFY(QMetaObject::invokeMethod(
            map.get(), "overlayDiagnostics", Q_RETURN_ARG(QVariant, diagnosticValue)));
        const QVariantMap diagnostic = diagnosticValue.toMap();
        QCOMPARE(diagnostic.value(QStringLiteral("missionItems")).toInt(), 3);
        QCOMPARE(diagnostic.value(QStringLiteral("geofences")).toInt(), 2);
        QCOMPARE(diagnostic.value(QStringLiteral("rallyPoints")).toInt(), 2);
        QCOMPARE(diagnostic.value(QStringLiteral("eventMarkers")).toInt(), 2);
        QCOMPARE(diagnostic.value(QStringLiteral("breadcrumbs")).toInt(), 1);
        QCOMPARE(diagnostic.value(QStringLiteral("home")).toInt(), 1);
        QCOMPARE(diagnostic.value(QStringLiteral("activeMissionSeq")).toInt(), 1);
        QCOMPARE(diagnostic.value(QStringLiteral("missionValid")).toBool(), true);
    }

    void workspaceChromeDiagnosticReportsVisibleTabs()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel breadcrumbs;
        animus::NavigationOverlayModels navigationOverlays;
        animus::MapSourceRegistry mapSources;
        animus::OfflineMapManager offlineMaps(&mapSources);
        animus::AnimusMapCacheManager mapCache;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge cesium(&vehicle, &breadcrumbs, &profiles, &navigationOverlays);
        animus::ThemeController theme(nullptr);
        QQmlEngine engine;

        std::unique_ptr<QObject> shell = createWorkspaceShell(&vehicle,
                                                              &breadcrumbs,
                                                              &navigationOverlays,
                                                              &mapSources,
                                                              &offlineMaps,
                                                              &mapCache,
                                                              &telemetry,
                                                              &profiles,
                                                              &cesium,
                                                              &theme,
                                                              &engine);
        QVERIFY(shell);
        shell->setProperty("width", 1280);
        shell->setProperty("height", 820);
        QVERIFY(QMetaObject::invokeMethod(
            shell.get(), "selectWorkspace", Q_ARG(QVariant, QStringLiteral("terrain-3d"))));

        QVariant diagnosticValue;
        QVERIFY(QMetaObject::invokeMethod(
            shell.get(), "workspaceChromeDiagnostics", Q_RETURN_ARG(QVariant, diagnosticValue)));
        const QVariantMap diagnostic = diagnosticValue.toMap();
        QCOMPARE(diagnostic.value(QStringLiteral("selectedWorkspace")).toString(),
                 QStringLiteral("terrain-3d"));
        QCOMPARE(diagnostic.value(QStringLiteral("themeMode")).toString(), QStringLiteral("light"));
        const QVariantMap settingsDisclosure =
            diagnostic.value(QStringLiteral("settingsDisclosure")).toMap();
        QCOMPARE(settingsDisclosure.value(QStringLiteral("semanticallyVisible")).toBool(), true);
        QCOMPARE(settingsDisclosure.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Diagnostics and settings"));
        const QVariantMap diagnosticsDrawer =
            diagnostic.value(QStringLiteral("diagnosticsDrawer")).toMap();
        QCOMPARE(diagnosticsDrawer.value(QStringLiteral("label")).toString(),
                 QStringLiteral("diagnostics drawer"));
        QCOMPARE(diagnosticsDrawer.value(QStringLiteral("semanticallyVisible")).toBool(), false);
        const QVariantMap linkStatus = diagnostic.value(QStringLiteral("linkStatus")).toMap();
        QCOMPARE(linkStatus.value(QStringLiteral("semanticallyVisible")).toBool(), true);
        QCOMPARE(linkStatus.value(QStringLiteral("label")).toString(), QStringLiteral("Link idle"));
        const QVariantMap authority = diagnostic.value(QStringLiteral("authority")).toMap();
        QCOMPARE(authority.value(QStringLiteral("semanticallyVisible")).toBool(), true);
        QCOMPARE(authority.value(QStringLiteral("label")).toString(), QStringLiteral("Read-only"));
        const QVariantList tabs = diagnostic.value(QStringLiteral("tabs")).toList();
        QCOMPARE(tabs.size(), 5);
        const QStringList expectedLabels{QStringLiteral("Map 2D"),
                                         QStringLiteral("Terrain 3D"),
                                         QStringLiteral("FPV"),
                                         QStringLiteral("Tactical"),
                                         QStringLiteral("Setup")};
        for (int index = 0; index < tabs.size(); ++index)
        {
            const QVariantMap tab = tabs.at(index).toMap();
            QCOMPARE(tab.value(QStringLiteral("label")).toString(), expectedLabels.at(index));
            QCOMPARE(tab.value(QStringLiteral("semanticallyVisible")).toBool(), true);
            QVERIFY(tab.value(QStringLiteral("width")).toInt() > 1);
            QVERIFY(tab.value(QStringLiteral("height")).toInt() > 1);
        }

        theme.toggleMode();
        QVERIFY(QMetaObject::invokeMethod(
            shell.get(), "workspaceChromeDiagnostics", Q_RETURN_ARG(QVariant, diagnosticValue)));
        const QVariantMap darkDiagnostic = diagnosticValue.toMap();
        QCOMPARE(darkDiagnostic.value(QStringLiteral("themeMode")).toString(),
                 QStringLiteral("dark"));
        const QVariantList darkTabs = darkDiagnostic.value(QStringLiteral("tabs")).toList();
        QCOMPARE(darkTabs.size(), 5);
        for (const QVariant &tabValue : darkTabs)
            QCOMPARE(tabValue.toMap().value(QStringLiteral("semanticallyVisible")).toBool(), true);
    }

    void workspaceShellSelectsTacticalById()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel breadcrumbs;
        animus::NavigationOverlayModels navigationOverlays;
        animus::MapSourceRegistry mapSources;
        animus::OfflineMapManager offlineMaps(&mapSources);
        animus::AnimusMapCacheManager mapCache;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge cesium(&vehicle, &breadcrumbs, &profiles, &navigationOverlays);
        animus::ThemeController theme(nullptr);
        QQmlEngine engine;

        std::unique_ptr<QObject> shell = createWorkspaceShell(&vehicle,
                                                              &breadcrumbs,
                                                              &navigationOverlays,
                                                              &mapSources,
                                                              &offlineMaps,
                                                              &mapCache,
                                                              &telemetry,
                                                              &profiles,
                                                              &cesium,
                                                              &theme,
                                                              &engine);
        QVERIFY(shell);
        QVERIFY(QMetaObject::invokeMethod(
            shell.get(), "selectWorkspace", Q_ARG(QVariant, QStringLiteral("tactical"))));
        QCOMPARE(shell->property("currentWorkspace").toString(), QStringLiteral("tactical"));
    }

    void workspaceShellSelectsFpvById()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel breadcrumbs;
        animus::NavigationOverlayModels navigationOverlays;
        animus::MapSourceRegistry mapSources;
        animus::OfflineMapManager offlineMaps(&mapSources);
        animus::AnimusMapCacheManager mapCache;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge cesium(&vehicle, &breadcrumbs, &profiles, &navigationOverlays);
        animus::ThemeController theme(nullptr);
        QQmlEngine engine;

        std::unique_ptr<QObject> shell = createWorkspaceShell(&vehicle,
                                                              &breadcrumbs,
                                                              &navigationOverlays,
                                                              &mapSources,
                                                              &offlineMaps,
                                                              &mapCache,
                                                              &telemetry,
                                                              &profiles,
                                                              &cesium,
                                                              &theme,
                                                              &engine);
        QVERIFY(shell);
        QVERIFY(QMetaObject::invokeMethod(
            shell.get(), "selectWorkspace", Q_ARG(QVariant, QStringLiteral("fpv"))));
        QCOMPARE(shell->property("currentWorkspace").toString(), QStringLiteral("fpv"));
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

    void mavlinkDecoderParsesAttitudeAngularRates()
    {
        animus::MavlinkDecoder decoder;
        const QVector<animus::MavlinkTelemetrySample> samples =
            decoder.decodeDatagram(attitudeFrame(0.1F, -0.2F, 0.3F, 0.4F, -0.5F, 0.6F));

        QCOMPARE(samples.size(), 1);
        const animus::MavlinkTelemetrySample sample = samples.constFirst();
        QCOMPARE(sample.hasAttitude, true);
        QVERIFY(qAbs(sample.rollRad - 0.1) < 1.0e-6);
        QVERIFY(qAbs(sample.pitchRad + 0.2) < 1.0e-6);
        QVERIFY(qAbs(sample.yawRad - 0.3) < 1.0e-6);
        QVERIFY(qAbs(sample.rollRateRps - 0.4) < 1.0e-6);
        QVERIFY(qAbs(sample.pitchRateRps + 0.5) < 1.0e-6);
        QVERIFY(qAbs(sample.yawRateRps - 0.6) < 1.0e-6);
    }

    void mavlinkDecoderParsesVfrHud()
    {
        animus::MavlinkDecoder decoder;
        const QVector<animus::MavlinkTelemetrySample> samples =
            decoder.decodeDatagram(vfrHudFrame(18.5F, 18.1F, 130, 62, 151.0F, 0.2F));

        QCOMPARE(samples.size(), 1);
        const animus::MavlinkTelemetrySample sample = samples.constFirst();
        QCOMPARE(sample.hasVfrHud, true);
        QVERIFY(qAbs(sample.airspeedMps - 18.5) < 1.0e-6);
        QVERIFY(qAbs(sample.groundspeedMps - 18.1) < 1.0e-6);
        QCOMPARE(sample.headingDeg, 130.0);
        QCOMPARE(sample.throttlePct, 62);
        QVERIFY(qAbs(sample.altitudeM - 151.0) < 1.0e-6);
        QVERIFY(qAbs(sample.climbMps - 0.2) < 1.0e-6);
    }

    void mavlinkDecoderParsesHeartbeatIdentityAndCustomMode()
    {
        animus::MavlinkDecoder decoder;
        const QVector<animus::MavlinkTelemetrySample> samples =
            decoder.decodeDatagram(heartbeatFrame(0x12345678U, 2U, 12U, 0xc1U, 3U));

        QCOMPARE(samples.size(), 1);
        const animus::MavlinkTelemetrySample sample = samples.constFirst();
        QCOMPARE(sample.hasHeartbeat, true);
        QCOMPARE(sample.customMode, 0x12345678U);
        QCOMPARE(sample.vehicleType, 2);
        QCOMPARE(sample.autopilot, 12);
        QCOMPARE(sample.baseMode, 0xc1);
        QCOMPARE(sample.systemStatus, 3);
        QCOMPARE(sample.armed, true);
    }

    void mavlinkDecoderParsesSysStatusBatteryFields()
    {
        animus::MavlinkDecoder decoder;
        const QVector<animus::MavlinkTelemetrySample> samples =
            decoder.decodeDatagram(sysStatusFrame(12150U, -230, 67));

        QCOMPARE(samples.size(), 1);
        const animus::MavlinkTelemetrySample sample = samples.constFirst();
        QCOMPARE(sample.hasSysStatus, true);
        QCOMPARE(sample.batteryVoltageValid, true);
        QCOMPARE(sample.batteryCurrentValid, true);
        QCOMPARE(sample.batteryRemainingValid, true);
        QVERIFY(qAbs(sample.batteryVoltageV - 12.15) < 1.0e-6);
        QVERIFY(qAbs(sample.batteryCurrentA + 2.3) < 1.0e-6);
        QCOMPARE(sample.batteryRemainingPct, 67);

        const QVector<animus::MavlinkTelemetrySample> unknownSamples =
            decoder.decodeDatagram(sysStatusFrame(65535U, -1, -1));
        QCOMPARE(unknownSamples.size(), 1);
        const animus::MavlinkTelemetrySample unknown = unknownSamples.constFirst();
        QCOMPARE(unknown.hasSysStatus, true);
        QCOMPARE(unknown.batteryVoltageValid, false);
        QCOMPARE(unknown.batteryCurrentValid, false);
        QCOMPARE(unknown.batteryRemainingValid, false);
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

    void telemetryServiceAppliesHeartbeatBatteryAndDiagnosticStates()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel breadcrumbs;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        QByteArray datagram;
        datagram.append(heartbeatFrame(42U, 1U, 12U, 0xc1U, 4U));
        datagram.append(sysStatusFrame(11800U, 250, 81));

        QVERIFY(telemetry.ingestDatagram(datagram));
        QVERIFY(
            QMetaObject::invokeMethod(&telemetry, "publishPendingSample", Qt::DirectConnection));

        QCOMPARE(vehicle.heartbeatValid(), true);
        QCOMPARE(vehicle.customMode(), 42U);
        QCOMPARE(vehicle.autopilotLabel(), QStringLiteral("PX4"));
        QCOMPARE(vehicle.vehicleTypeLabel(), QStringLiteral("Fixed wing"));
        QVERIFY(vehicle.baseModeSummary().contains(QStringLiteral("armed")));
        QVERIFY(vehicle.baseModeSummary().contains(QStringLiteral("custom")));
        QCOMPARE(vehicle.systemStatusLabel(), QStringLiteral("Active"));
        QCOMPARE(vehicle.batteryValid(), true);
        QCOMPARE(vehicle.batteryRemainingValid(), true);
        QCOMPARE(vehicle.batteryRemainingPct(), 81);
        QVERIFY(qAbs(vehicle.batteryVoltageV() - 11.8) < 1.0e-6);
        QVERIFY(qAbs(vehicle.batteryCurrentA() - 2.5) < 1.0e-6);
        QCOMPARE(telemetry.firmwareModeFieldState(), QStringLiteral("fresh"));
        QCOMPARE(telemetry.batteryFieldState(), QStringLiteral("fresh"));
        QCOMPARE(telemetry.fieldState(QStringLiteral("firmwareMode")), QStringLiteral("fresh"));
        QCOMPARE(telemetry.fieldState(QStringLiteral("battery")), QStringLiteral("fresh"));
        QVERIFY(telemetry.datagramRateHz() > 0.0);
        QVERIFY(telemetry.decodedRateHz() > 0.0);

        telemetry.updateFreshnessForElapsedMs(3000);
        QCOMPARE(telemetry.firmwareModeFieldState(), QStringLiteral("stale"));
        QCOMPARE(telemetry.batteryFieldState(), QStringLiteral("stale"));
    }

    void telemetryServiceAppliesRequiredSitlMavlinkSet()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel breadcrumbs;
        animus::TelemetryService telemetry(&vehicle, &breadcrumbs);
        QByteArray datagram;
        datagram.append(heartbeatFrame());
        datagram.append(sysStatusFrame());
        datagram.append(attitudeFrame(0.1F, -0.2F, 0.3F, 0.4F, -0.5F, 0.6F));
        datagram.append(globalPositionFrame());
        datagram.append(gpsRawIntFrame());
        datagram.append(vfrHudFrame(18.5F, 18.1F, 130, 62, 151.0F, 0.2F));
        datagram.append(missionCurrentFrame());
        datagram.append(homePositionFrame());
        datagram.append(terrainReportFrame());
        datagram.append(servoOutputRawFrame({1600, 1500, 1510, 1490, 0, 0, 0, 0}));

        QVERIFY(telemetry.ingestDatagram(datagram));
        QVERIFY(
            QMetaObject::invokeMethod(&telemetry, "publishPendingSample", Qt::DirectConnection));

        QCOMPARE(vehicle.attitudeValid(), true);
        QCOMPARE(vehicle.heartbeatValid(), true);
        QCOMPARE(vehicle.armed(), true);
        QCOMPARE(vehicle.batteryValid(), true);
        QCOMPARE(vehicle.batteryRemainingPct(), 73);
        QCOMPARE(vehicle.positionValid(), true);
        QCOMPARE(vehicle.velocityValid(), true);
        QCOMPARE(vehicle.gpsValid(), true);
        QCOMPARE(vehicle.missionValid(), true);
        QCOMPARE(vehicle.homeValid(), true);
        QCOMPARE(vehicle.terrainValid(), true);
        QVERIFY(qAbs(vehicle.airspeedMps() - 18.5) < 1.0e-6);
        QVERIFY(qAbs(vehicle.groundspeedMps() - 18.1) < 1.0e-6);
        QVERIFY(qAbs(vehicle.climbMps() - 0.2) < 1.0e-6);
        QCOMPARE(vehicle.throttlePct(), 62);
        QCOMPARE(vehicle.gpsFixType(), 3);
        QCOMPARE(vehicle.satellitesVisible(), 10);
        QCOMPARE(vehicle.missionSeq(), 2);
        QCOMPARE(vehicle.homeAltitudeM(), 150.0);
        QCOMPARE(vehicle.terrainLoaded(), 1);
        QCOMPARE(vehicle.servoOutputPwm(1), 1600);
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

        const QVariantMap clearance = snapshot.value(QStringLiteral("clearance")).toMap();
        QCOMPARE(clearance.value(QStringLiteral("terrainReportValid")).toBool(), true);
        QCOMPARE(clearance.value(QStringLiteral("aglM")).toDouble(), 22.5);
        QCOMPARE(clearance.value(QStringLiteral("homeRelativeAltitudeM")).toDouble(), 36.0);
        QCOMPARE(clearance.value(QStringLiteral("state")).toString(), QStringLiteral("warning"));

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

    void terrainClearanceAnalyzerHandlesUnknownThresholdsAndTrend()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        QVariantMap clearance = animus::TerrainClearanceAnalyzer::analyze(vehicle, trail);
        QCOMPARE(clearance.value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
        QCOMPARE(clearance.value(QStringLiteral("message")).toString(),
                 QStringLiteral("position unavailable"));

        vehicle.setPositionValid(true);
        clearance = animus::TerrainClearanceAnalyzer::analyze(vehicle, trail);
        QCOMPARE(clearance.value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
        QCOMPARE(clearance.value(QStringLiteral("message")).toString(),
                 QStringLiteral("terrain report unavailable"));

        vehicle.setAltitudeM(125.0);
        vehicle.setHomeAltitudeM(25.0);
        vehicle.setHomeValid(true);
        vehicle.setTerrainCurrentHeightM(80.0);
        vehicle.setTerrainValid(true);
        trail.setMinDistanceM(0.0);
        QVERIFY(trail.append(37.0, -122.0, 145.0, 10.0));
        QVERIFY(trail.append(37.0, -122.0, 120.0, 15.0));
        clearance = animus::TerrainClearanceAnalyzer::analyze(vehicle, trail);
        QCOMPARE(clearance.value(QStringLiteral("aglM")).toDouble(), 45.0);
        QCOMPARE(clearance.value(QStringLiteral("homeRelativeAltitudeM")).toDouble(), 100.0);
        QCOMPARE(clearance.value(QStringLiteral("minimumRecentClearanceM")).toDouble(), 40.0);
        QCOMPARE(clearance.value(QStringLiteral("trendMps")).toDouble(), -5.0);
        QCOMPARE(clearance.value(QStringLiteral("state")).toString(), QStringLiteral("caution"));

        vehicle.setAltitudeM(95.0);
        trail.clear();
        QVERIFY(trail.append(37.0, -122.0, 95.0, 20.0));
        clearance = animus::TerrainClearanceAnalyzer::analyze(vehicle, trail);
        QCOMPARE(clearance.value(QStringLiteral("state")).toString(), QStringLiteral("warning"));

        vehicle.setAltitudeM(150.0);
        trail.clear();
        QVERIFY(trail.append(37.0, -122.0, 150.0, 30.0));
        clearance = animus::TerrainClearanceAnalyzer::analyze(vehicle, trail);
        QCOMPARE(clearance.value(QStringLiteral("state")).toString(), QStringLiteral("clear"));
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

    void tacticalAndTerrainUseSameSelectedProfileMetadata()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge bridge(&vehicle, &trail, &profiles);

        const QVariantMap terrainModel = bridge.snapshot().value(QStringLiteral("model")).toMap();
        profiles.setSelectedProfileId(QStringLiteral("generic_fixed_wing_smooth"));
        const QVariantMap tacticalModel = bridge.snapshot().value(QStringLiteral("model")).toMap();

        QCOMPARE(tacticalModel, terrainModel);
        QCOMPARE(tacticalModel.value(QStringLiteral("profile")).toString(),
                 QStringLiteral("generic_fixed_wing_smooth"));
        QCOMPARE(tacticalModel.value(QStringLiteral("asset")).toString(),
                 QStringLiteral("models/generic_fixed_wing_smooth.glb"));
    }

    void tacticalAndTerrainShareProfilePolarityMapping()
    {
        animus::VehicleModel vehicle;
        vehicle.setServoOutputPwm(1, 1750, true);
        animus::BreadcrumbPathModel trail;
        animus::VehicleModelProfileManager profiles(bundledModelProfilesDir(), nullptr, &vehicle);
        animus::CesiumBridge bridge(&vehicle, &trail, &profiles);

        const QVariantList terrainSurfaces =
            bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        QCOMPARE(surfaceById(terrainSurfaces, QStringLiteral("left_aileron"))
                     .value(QStringLiteral("deflectionDeg"))
                     .toDouble(),
                 12.5);

        profiles.reverseSurfacePolarity(QStringLiteral("left_aileron"));
        const QVariantList tacticalSurfaces =
            bridge.snapshot().value(QStringLiteral("controlSurfaces")).toList();
        const QVariantMap left = surfaceById(tacticalSurfaces, QStringLiteral("left_aileron"));
        QCOMPARE(left.value(QStringLiteral("polarity")).toDouble(), -1.0);
        QCOMPARE(left.value(QStringLiteral("deflectionDeg")).toDouble(), -12.5);
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
        QVERIFY(scriptText.contains(QStringLiteral("window.animusOverlayDiagnostics")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusSetCameraMode")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusResetFpvCamera")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusFpvCameraState")));
        QVERIFY(scriptText.contains(QStringLiteral("window.animusResetTacticalCamera")));
        QVERIFY(scriptText.contains(QStringLiteral("ANIMUS_CAMERA_MODE")));
        QVERIFY(scriptText.contains(QStringLiteral("installCameraControls")));
        QVERIFY(scriptText.contains(QStringLiteral("screenSpaceCameraController")));
        QVERIFY(scriptText.contains(QStringLiteral("CustomHeightmapTerrainProvider")));
        QVERIFY(scriptText.contains(QStringLiteral("UrlTemplateImageryProvider")));
        QVERIFY(scriptText.contains(QStringLiteral("aircraftModelUrl")));
        QVERIFY(scriptText.contains(QStringLiteral("VehicleModelController")));
        QVERIFY(scriptText.contains(QStringLiteral("applyControlSurfaces")));
        QVERIFY(scriptText.contains(QStringLiteral("function updateNavigationOverlays()")));
        QVERIFY(scriptText.contains(QStringLiteral("overlayEntities")));
        QVERIFY(scriptText.contains(QStringLiteral("overlays.missionItems")));
        QVERIFY(scriptText.contains(QStringLiteral("overlays.geofences")));
        QVERIFY(scriptText.contains(
            QStringLiteral("workspaceMode === 'tactical' || workspaceMode === 'fpv'")));
        QVERIFY(scriptText.contains(QStringLiteral("models/${profile}.json")));
        QVERIFY(scriptText.contains(QStringLiteral("Cesium.Model.fromGltfAsync")));
        QVERIFY(scriptText.contains(QStringLiteral("upAxis: Cesium.Axis.Z")));
        QVERIFY(scriptText.contains(QStringLiteral("forwardAxis: Cesium.Axis.X")));
        QVERIFY(!scriptText.contains(QStringLiteral("colorBlendMode: Cesium.ColorBlendMode.MIX")));
        QVERIFY(scriptText.contains(QStringLiteral("vehicleModelProfile.asset")));
        QVERIFY(scriptText.contains(QStringLiteral("fallbackUri")));
        QVERIFY(scriptText.contains(QStringLiteral("Cesium.HeadingPitchRange")));
        QVERIFY(scriptText.contains(QStringLiteral("function applyWorkspaceSceneStyle()")));
        QVERIFY(scriptText.contains(
            QStringLiteral("viewer.scene.backgroundColor = "
                           "Cesium.Color.fromCssColorString(tactical ? '#050b0f'")));

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

    void tacticalStaticBundleUsesRgbRingsAndResetApi()
    {
        const QDir cesiumDir(
            QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium")));
        QFile script(cesiumDir.filePath(QStringLiteral("animus-cesium.js")));
        QVERIFY(script.open(QIODevice::ReadOnly));
        const QString scriptText = QString::fromUtf8(script.readAll());

        QVERIFY(scriptText.contains(QStringLiteral("const tacticalRingRadiusM = 7.5;")));
        QVERIFY(scriptText.contains(QStringLiteral("function resetTacticalCamera()")));
        QVERIFY(scriptText.contains(QStringLiteral("resetLockedCameraOffset('tactical');")));
        QVERIFY(scriptText.contains(QStringLiteral("function attitudeAxisRingPoints(")));
        QVERIFY(scriptText.contains(QStringLiteral("Cesium.Matrix3.fromQuaternion")));
        QVERIFY(scriptText.contains(QStringLiteral("color: '#d92626'")));
        QVERIFY(scriptText.contains(QStringLiteral("color: '#2fbf5b'")));
        QVERIFY(scriptText.contains(QStringLiteral("color: '#2f6df6'")));
        QVERIFY(scriptText.contains(QStringLiteral("workspaceMode = 'tactical';")));
        QVERIFY(scriptText.contains(QStringLiteral("cameraMode = 'tactical';")));

        const int attitudeSection =
            scriptText.indexOf(QStringLiteral("function updateAttitudeReferences()"));
        QVERIFY(attitudeSection >= 0);
        const int renderSection = scriptText.indexOf(QStringLiteral("function renderCesium()"));
        QVERIFY(renderSection > attitudeSection);
        const QString tacticalReferenceSection =
            scriptText.mid(attitudeSection, renderSection - attitudeSection);
        QVERIFY(!tacticalReferenceSection.contains(QStringLiteral("#f0c84b")));
        QVERIFY(!tacticalReferenceSection.contains(QStringLiteral("amber")));

        QFile tacticalQml(QStringLiteral(ANIMUS_QT_QML_DIR) +
                          QStringLiteral("/TacticalAttitudeView.qml"));
        QVERIFY(tacticalQml.open(QIODevice::ReadOnly));
        const QString tacticalQmlText = QString::fromUtf8(tacticalQml.readAll());
        QVERIFY(tacticalQmlText.contains(QStringLiteral("resetTacticalCamera()")));
        QVERIFY(!tacticalQmlText.contains(QStringLiteral("setCameraMode(\"tactical\")")));
    }

    void fpvStaticBundleUsesForwardHemisphereCamera()
    {
        const QDir cesiumDir(
            QDir(QStringLiteral(ANIMUS_QT_QML_DIR)).filePath(QStringLiteral("../web/cesium")));
        QFile script(cesiumDir.filePath(QStringLiteral("animus-cesium.js")));
        QVERIFY(script.open(QIODevice::ReadOnly));
        const QString scriptText = QString::fromUtf8(script.readAll());

        QVERIFY(scriptText.contains(QStringLiteral("const fpvVerticalFovDeg = 70.0;")));
        QVERIFY(scriptText.contains(QStringLiteral("function resetFpvCamera()")));
        QVERIFY(scriptText.contains(QStringLiteral("workspaceMode = 'fpv';")));
        QVERIFY(scriptText.contains(QStringLiteral("cameraMode = 'fpv';")));
        QVERIFY(scriptText.contains(QStringLiteral("function fpvCameraPose(")));
        QVERIFY(scriptText.contains(QStringLiteral("fpvNoseOffsetM.forward")));
        QVERIFY(scriptText.contains(QStringLiteral("fpvLook.yawDeg = clamp")));
        QVERIFY(scriptText.contains(QStringLiteral("fpvLook.forwardDot")));
        QVERIFY(scriptText.contains(QStringLiteral("forwardHemisphereCompliant")));
        QVERIFY(scriptText.contains(QStringLiteral("ownshipHidden: workspaceMode === 'fpv'")));
        QVERIFY(
            scriptText.contains(QStringLiteral("terrainEnabled: workspaceMode !== 'tactical'")));

        const int fpvPoseSection = scriptText.indexOf(QStringLiteral("function fpvCameraPose("));
        QVERIFY(fpvPoseSection >= 0);
        const int applyManualSection =
            scriptText.indexOf(QStringLiteral("function applyManualCamera()"));
        QVERIFY(applyManualSection > fpvPoseSection);
        const QString fpvSection =
            scriptText.mid(fpvPoseSection, applyManualSection - fpvPoseSection);
        QVERIFY(
            fpvSection.contains(QStringLiteral("Cesium.Cartesian3.dot(direction, axes.forward)")));
        QVERIFY(fpvSection.contains(QStringLiteral("Math.cos(pitchRad) * Math.cos(yawRad)")));

        QFile fpvQml(QStringLiteral(ANIMUS_QT_QML_DIR) + QStringLiteral("/FpvView.qml"));
        QVERIFY(fpvQml.open(QIODevice::ReadOnly));
        const QString fpvQmlText = QString::fromUtf8(fpvQml.readAll());
        QVERIFY(fpvQmlText.contains(QStringLiteral("objectName: \"fpvView\"")));
        QVERIFY(fpvQmlText.contains(QStringLiteral("item.workspaceMode = \"fpv\"")));
        QVERIFY(fpvQmlText.contains(QStringLiteral("resetFpvCamera()")));

        QFile webViewQml(QStringLiteral(ANIMUS_QT_QML_DIR) +
                         QStringLiteral("/Terrain3DWebView.qml"));
        QVERIFY(webViewQml.open(QIODevice::ReadOnly));
        const QString webViewText = QString::fromUtf8(webViewQml.readAll());
        QVERIFY(webViewText.contains(QStringLiteral("function resetFpvCamera()")));
    }

    void terrainFpvAndTacticalUseLocalWebEngineSceneStatus()
    {
        QFile webViewQml(QStringLiteral(ANIMUS_QT_QML_DIR) +
                         QStringLiteral("/Terrain3DWebView.qml"));
        QVERIFY(webViewQml.open(QIODevice::ReadOnly));
        const QString webViewText = QString::fromUtf8(webViewQml.readAll());
        QVERIFY(webViewText.contains(QStringLiteral("property var sceneStatus")));
        QVERIFY(
            webViewText.contains(QStringLiteral("function setLocalSceneStatus(status, error)")));
        QVERIFY(
            webViewText.contains(QStringLiteral("setLocalSceneStatus(\"webengine-ready\", \"\")")));
        QVERIFY(!webViewText.contains(QStringLiteral("cesiumBridge.setSceneStatus")));

        QFile terrainQml(QStringLiteral(ANIMUS_QT_QML_DIR) + QStringLiteral("/Terrain3DView.qml"));
        QVERIFY(terrainQml.open(QIODevice::ReadOnly));
        const QString terrainText = QString::fromUtf8(terrainQml.readAll());
        QVERIFY(terrainText.contains(QStringLiteral("property var localSceneStatus")));
        QVERIFY(terrainText.contains(QStringLiteral("root.localSceneStatus.status")));
        QVERIFY(terrainText.contains(
            QStringLiteral("root.localSceneStatus = webLoader.item.sceneStatus")));
        QVERIFY(!terrainText.contains(QStringLiteral("cesiumBridge.sceneStatus")));

        QFile fpvQml(QStringLiteral(ANIMUS_QT_QML_DIR) + QStringLiteral("/FpvView.qml"));
        QVERIFY(fpvQml.open(QIODevice::ReadOnly));
        const QString fpvText = QString::fromUtf8(fpvQml.readAll());
        QVERIFY(fpvText.contains(QStringLiteral("property var localSceneStatus")));
        QVERIFY(fpvText.contains(QStringLiteral("root.localSceneStatus.status")));
        QVERIFY(
            fpvText.contains(QStringLiteral("root.localSceneStatus = webLoader.item.sceneStatus")));
        QVERIFY(!fpvText.contains(QStringLiteral("cesiumBridge.sceneStatus")));

        QFile tacticalQml(QStringLiteral(ANIMUS_QT_QML_DIR) +
                          QStringLiteral("/TacticalAttitudeView.qml"));
        QVERIFY(tacticalQml.open(QIODevice::ReadOnly));
        const QString tacticalText = QString::fromUtf8(tacticalQml.readAll());
        QVERIFY(tacticalText.contains(QStringLiteral("property var localSceneStatus")));
        QVERIFY(tacticalText.contains(QStringLiteral("root.localSceneStatus.status")));
        QVERIFY(tacticalText.contains(
            QStringLiteral("root.localSceneStatus = webLoader.item.sceneStatus")));
        QVERIFY(!tacticalText.contains(QStringLiteral("cesiumBridge.sceneStatus")));
    }

    void setupViewExposesModelProfilePolarityControls()
    {
        QFile qml(QStringLiteral(ANIMUS_QT_QML_DIR) + QStringLiteral("/SetupView.qml"));
        QVERIFY(qml.open(QIODevice::ReadOnly));
        const QString qmlText = QString::fromUtf8(qml.readAll());

        QFile sectionQml(QStringLiteral(ANIMUS_QT_QML_DIR) +
                         QStringLiteral("/AnimusSetupSection.qml"));
        QVERIFY(sectionQml.open(QIODevice::ReadOnly));
        const QString sectionText = QString::fromUtf8(sectionQml.readAll());

        QVERIFY(sectionText.contains(QStringLiteral("detailsExpanded")));
        QVERIFY(sectionText.contains(QStringLiteral("detailsContent")));

        QVERIFY(qmlText.contains(QStringLiteral("setupReadinessSection")));
        QVERIFY(qmlText.contains(QStringLiteral("setupTelemetryLinkSection")));
        QVERIFY(qmlText.contains(QStringLiteral("setupVehicleModelSection")));
        QVERIFY(qmlText.contains(QStringLiteral("setupMapsTerrainSection")));
        QVERIFY(qmlText.contains(QStringLiteral("setupLogsSection")));
        QVERIFY(qmlText.contains(QStringLiteral("setupDiagnosticsSection")));
        QVERIFY(qmlText.contains(QStringLiteral("setupModelProfileSelector")));
        QVERIFY(qmlText.contains(QStringLiteral("setupSurfacePolarityButton")));
        QVERIFY(qmlText.contains(QStringLiteral("setupTileDownloadButton")));
        QVERIFY(qmlText.contains(QStringLiteral("setupSeedCacheButton")));
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
