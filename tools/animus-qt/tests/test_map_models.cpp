#include "maps/MapPackManager.h"
#include "maps/MapSourceRegistry.h"
#include "maps/OfflineMapManager.h"
#include "maps/TileImageProvider.h"
#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/MavlinkDecoder.h"
#include "telemetry/TelemetryService.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cmath>
#include <cstring>

namespace
{

uint16_t crcAccumulate(unsigned char data, uint16_t crc)
{
    unsigned char tmp = data ^ static_cast<unsigned char>(crc & 0xffU);
    tmp ^= static_cast<unsigned char>(tmp << 4U);
    return static_cast<uint16_t>((crc >> 8U) ^ (static_cast<uint16_t>(tmp) << 8U) ^
                                 (static_cast<uint16_t>(tmp) << 3U) ^
                                 (static_cast<uint16_t>(tmp) >> 4U));
}

void putU16(QByteArray *payload, int offset, uint16_t value)
{
    (*payload)[offset] = static_cast<char>(value & 0xffU);
    (*payload)[offset + 1] = static_cast<char>((value >> 8U) & 0xffU);
}

void putU32(QByteArray *payload, int offset, uint32_t value)
{
    (*payload)[offset] = static_cast<char>(value & 0xffU);
    (*payload)[offset + 1] = static_cast<char>((value >> 8U) & 0xffU);
    (*payload)[offset + 2] = static_cast<char>((value >> 16U) & 0xffU);
    (*payload)[offset + 3] = static_cast<char>((value >> 24U) & 0xffU);
}

void putI16(QByteArray *payload, int offset, int16_t value)
{
    putU16(payload, offset, static_cast<uint16_t>(value));
}

void putI32(QByteArray *payload, int offset, int32_t value)
{
    putU32(payload, offset, static_cast<uint32_t>(value));
}

void putFloat(QByteArray *payload, int offset, float value)
{
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    putU32(payload, offset, raw);
}

QByteArray mavlinkV1Frame(unsigned char msgId, unsigned char crcExtra, const QByteArray &payload)
{
    QByteArray frame(6 + payload.size() + 2, '\0');
    frame[0] = static_cast<char>(0xfeU);
    frame[1] = static_cast<char>(payload.size());
    frame[2] = 7;
    frame[3] = 1;
    frame[4] = 1;
    frame[5] = static_cast<char>(msgId);
    for (int i = 0; i < payload.size(); ++i)
        frame[6 + i] = payload[i];

    uint16_t checksum = 0xffffU;
    for (int i = 1; i < 6 + payload.size(); ++i)
    {
        checksum = crcAccumulate(static_cast<unsigned char>(frame[i]), checksum);
    }
    checksum = crcAccumulate(crcExtra, checksum);
    frame[6 + payload.size()] = static_cast<char>(checksum & 0xffU);
    frame[7 + payload.size()] = static_cast<char>((checksum >> 8U) & 0xffU);
    return frame;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(contents) == contents.size();
}

QByteArray validPackMetadata(QByteArray extra = QByteArray())
{
    QByteArray metadata =
        "{\"schemaVersion\":1,\"name\":\"Stanford\",\"description\":\"SITL range\","
        "\"minZoom\":12,\"maxZoom\":16,\"license\":\"test-license\","
        "\"attribution\":\"test data\",\"imagery\":{\"format\":\"xyz\"}";
    if (!extra.isEmpty())
        metadata += "," + extra;
    metadata += "}";
    return metadata;
}

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

        QVERIFY(manager.canUseSource(QStringLiteral("offline-pack")));
        QVERIFY(!manager.canUseSource(QStringLiteral("osm")));
        QVERIFY(!manager.canUseSource(QStringLiteral("missing-source")));
        QCOMPARE(manager.sourceBlockReason(QStringLiteral("osm")),
                 QStringLiteral("Strict offline blocks network map sources"));
        QCOMPARE(manager.sourceBlockReason(QStringLiteral("missing-source")),
                 QStringLiteral("Unknown map source"));
    }

    void mapSourceRegistryExposesActiveDisplayMetadata()
    {
        animus::MapSourceRegistry registry;

        QCOMPARE(registry.activeSourceId(), QStringLiteral("offline-pack"));
        QCOMPARE(registry.activeLabel(), QStringLiteral("Offline Map Pack"));
        QCOMPARE(registry.activeProvider(), QStringLiteral("animus-pack"));
        QCOMPARE(registry.sourceIndex(QStringLiteral("osm")), 1);
        QCOMPARE(registry.sourceIdAt(2), QStringLiteral("satellite"));
        QCOMPARE(registry.sourceLabel(QStringLiteral("satellite")),
                 QStringLiteral("Licensed Satellite"));
        QVERIFY(registry.sourceExists(QStringLiteral("offline-pack")));
        QVERIFY(!registry.sourceExists(QStringLiteral("missing-source")));
    }

    void offlinePolicyReturnsBlockedActiveSourceToLocalPack()
    {
        animus::MapSourceRegistry registry;
        animus::OfflineMapManager manager(&registry);

        manager.setMode(animus::OfflineMapManager::Online);
        registry.setActiveSourceId(QStringLiteral("osm"));
        QCOMPARE(registry.activeSourceId(), QStringLiteral("osm"));

        manager.setMode(animus::OfflineMapManager::CachedOffline);
        QCOMPARE(registry.activeSourceId(), QStringLiteral("offline-pack"));
        QVERIFY(manager.canUseSource(registry.activeSourceId()));
    }

    void mapPackEmptyRootHasExplicitNoPackState()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::MapPackManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.reload());
        QCOMPARE(manager.rowCount(), 0);
        QCOMPARE(manager.activePackId(), QString());
        QCOMPARE(manager.activePackPath(), QString());
        QCOMPARE(manager.activeTileRootPath(), QString());
        QVERIFY(!manager.activeHasLocalXyzImagery());
        QCOMPARE(manager.validationError(), QString());
    }

    void breadcrumbModelBoundsGrowth()
    {
        animus::BreadcrumbPathModel model;
        model.setMaxPoints(3);
        model.setMinDistanceM(0.0);

        QVERIFY(model.append(37.0, -122.0, 10.0, 1.0));
        QVERIFY(model.append(37.1, -122.0, 10.0, 2.0));
        QVERIFY(model.append(37.2, -122.0, 10.0, 3.0));
        QVERIFY(model.append(37.3, -122.0, 10.0, 4.0));
        QCOMPARE(model.rowCount(), 3);
    }

    void breadcrumbModelDecimatesByDistance()
    {
        animus::BreadcrumbPathModel model;
        model.setMaxPoints(10);
        model.setMinDistanceM(50.0);

        QVERIFY(model.append(37.0, -122.0, 10.0, 1.0));
        QVERIFY(!model.append(37.0001, -122.0, 10.0, 2.0));
        QCOMPARE(model.rowCount(), 1);
    }

    void mapPackLoadsValidMetadata()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir rootDir(root.path());
        QVERIFY(rootDir.mkpath(QStringLiteral("stanford/2d/xyz")));
        QVERIFY(rootDir.mkpath(QStringLiteral("stanford/3d/terrain")));

        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("stanford/metadata.json")),
                          validPackMetadata("\"terrain\":{\"format\":\"quantized-mesh\"},"
                                            "\"bounds\":{\"west\":-123,\"south\":36,"
                                            "\"east\":-121,\"north\":38}")));

        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("stanford/3d/terrain/layer.json")),
                          QByteArray("{}")));

        animus::MapPackManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.reload());
        QCOMPARE(manager.rowCount(), 1);
        QCOMPARE(manager.activePackId(), QStringLiteral("stanford"));
        manager.setActivePackId(QStringLiteral("stanford"));
        QCOMPARE(manager.activeAttribution(), QStringLiteral("test data"));
        QVERIFY(manager.activeHasLocalXyzImagery());
        QCOMPARE(manager.activeMinZoom(), 12);
        QCOMPARE(manager.activeMaxZoom(), 16);
        QVERIFY(manager.activeHasBounds());
        QCOMPARE(manager.activeWestDeg(), -123.0);
    }

    void mapPackRejectsInvalidXyzPacks()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir rootDir(root.path());
        QVERIFY(rootDir.mkpath(QStringLiteral("aaa-missing-license/2d/xyz")));
        QVERIFY(rootDir.mkpath(QStringLiteral("unsupported/2d/xyz")));
        QVERIFY(rootDir.mkpath(QStringLiteral("missing-root")));
        QVERIFY(rootDir.mkpath(QStringLiteral("bad-zoom/2d/xyz")));
        QVERIFY(rootDir.mkpath(QStringLiteral("bad-bounds/2d/xyz")));

        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("aaa-missing-license/metadata.json")),
                          "{\"schemaVersion\":1,\"name\":\"Bad\",\"attribution\":\"test\","
                          "\"imagery\":{\"format\":\"xyz\"},\"minZoom\":1,\"maxZoom\":2}"));
        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("unsupported/metadata.json")),
                          "{\"schemaVersion\":1,\"name\":\"Bad\",\"license\":\"test\","
                          "\"attribution\":\"test\",\"imagery\":{\"format\":\"pmtiles\"},"
                          "\"minZoom\":1,\"maxZoom\":2}"));
        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("missing-root/metadata.json")),
                          validPackMetadata()));
        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("bad-zoom/metadata.json")),
                          "{\"schemaVersion\":1,\"name\":\"Bad\",\"license\":\"test\","
                          "\"attribution\":\"test\",\"imagery\":{\"format\":\"xyz\"},"
                          "\"minZoom\":7,\"maxZoom\":2}"));
        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("bad-bounds/metadata.json")),
                          validPackMetadata("\"bounds\":{\"west\":3,\"south\":1,"
                                            "\"east\":2,\"north\":4}")));

        animus::MapPackManager manager;
        manager.setRootPath(root.path());
        QVERIFY(!manager.reload());
        QCOMPARE(manager.rowCount(), 0);
        QVERIFY(manager.validationError().contains(QStringLiteral("license")));
    }

    void tileProviderLoadsPngAndReturnsEmptyForInvalidRequests()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir rootDir(root.path());
        QVERIFY(rootDir.mkpath(QStringLiteral("stanford/2d/xyz/12/654")));
        QVERIFY(writeFile(rootDir.filePath(QStringLiteral("stanford/metadata.json")),
                          validPackMetadata()));

        QImage tile(4, 4, QImage::Format_RGBA8888);
        tile.fill(QColor(10, 20, 30, 255));
        QVERIFY(
            tile.save(rootDir.filePath(QStringLiteral("stanford/2d/xyz/12/654/1582.png")), "PNG"));

        animus::MapPackManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.reload());

        animus::TileImageProvider provider(&manager);
        const QImage loaded =
            provider.loadTileImage(QStringLiteral("stanford/12/654/1582"), QSize(4, 4));
        QCOMPARE(loaded.size(), QSize(4, 4));
        QCOMPARE(loaded.pixelColor(0, 0), QColor(10, 20, 30, 255));

        const QImage missing =
            provider.loadTileImage(QStringLiteral("stanford/11/654/1582"), QSize(4, 4));
        QCOMPARE(missing.size(), QSize(4, 4));
        QCOMPARE(missing.pixelColor(0, 0), QColor(Qt::transparent));

        const QImage traversal =
            provider.loadTileImage(QStringLiteral("../stanford/12/654/1582"), QSize(4, 4));
        QCOMPARE(traversal.pixelColor(0, 0), QColor(Qt::transparent));
    }

    void mavlinkDecoderCoversCoreTelemetryMessages()
    {
        animus::MavlinkDecoder decoder;

        QByteArray heartbeat(9, '\0');
        putU32(&heartbeat, 0, 3);
        heartbeat[4] = 1;
        heartbeat[5] = 12;
        heartbeat[6] = static_cast<char>(0x80U);
        heartbeat[7] = 4;
        heartbeat[8] = 3;

        QByteArray attitude(28, '\0');
        putFloat(&attitude, 4, 0.1F);
        putFloat(&attitude, 8, -0.2F);
        putFloat(&attitude, 12, 1.5F);

        QByteArray globalPosition(28, '\0');
        putI32(&globalPosition, 4, 374275000);
        putI32(&globalPosition, 8, -1221697000);
        putI32(&globalPosition, 12, 45000);
        putI16(&globalPosition, 20, 1234);
        putI16(&globalPosition, 22, 500);
        putU16(&globalPosition, 26, 9200);

        QByteArray mission(2, '\0');
        putU16(&mission, 0, 42);

        QByteArray home(52, '\0');
        putI32(&home, 0, 374200000);
        putI32(&home, 4, -1221600000);
        putI32(&home, 8, 12000);

        QByteArray terrain(22, '\0');
        putI32(&terrain, 0, 374275000);
        putI32(&terrain, 4, -1221697000);
        putFloat(&terrain, 10, 22.5F);
        putFloat(&terrain, 14, 30.0F);
        putU16(&terrain, 18, 2);
        putU16(&terrain, 20, 7);

        const QByteArray datagram =
            mavlinkV1Frame(0, 50, heartbeat) + mavlinkV1Frame(30, 39, attitude) +
            mavlinkV1Frame(33, 104, globalPosition) + mavlinkV1Frame(42, 28, mission) +
            mavlinkV1Frame(242, 104, home) + mavlinkV1Frame(136, 1, terrain);
        const QVector<animus::MavlinkTelemetrySample> samples = decoder.decodeDatagram(datagram);
        QCOMPARE(samples.size(), 6);
        QVERIFY(samples[0].hasHeartbeat);
        QVERIFY(samples[0].armed);
        QVERIFY(samples[1].hasAttitude);
        QCOMPARE(samples[1].yawRad, 1.5);
        QVERIFY(samples[2].hasGlobalPosition);
        QCOMPARE(samples[2].headingDeg, 92.0);
        QCOMPARE(samples[3].missionSeq, 42);
        QCOMPARE(samples[4].homeAltitudeM, 12.0);
        QCOMPARE(samples[5].terrainLoaded, 7);
    }

    void telemetryServicePublishesLatestMavlinkValueAtBoundedUiRate()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::TelemetryService service(&vehicle, &trail);
        service.setUiRateHz(200);
        QCOMPARE(service.uiRateHz(), 30);

        QByteArray first(28, '\0');
        putI32(&first, 4, 374275000);
        putI32(&first, 8, -1221697000);
        putI32(&first, 12, 45000);
        putU16(&first, 26, 9000);

        QByteArray second(28, '\0');
        putI32(&second, 4, 374285000);
        putI32(&second, 8, -1221797000);
        putI32(&second, 12, 47000);
        putU16(&second, 26, 9100);

        QVERIFY(service.ingestDatagram(mavlinkV1Frame(33, 104, first)));
        QVERIFY(service.ingestDatagram(mavlinkV1Frame(33, 104, second)));
        QVERIFY(QMetaObject::invokeMethod(&service, "publishPendingSample", Qt::DirectConnection));
        QCOMPARE(vehicle.latitudeDeg(), 37.4285);
        QCOMPARE(vehicle.longitudeDeg(), -122.1797);
        QCOMPARE(vehicle.altitudeM(), 47.0);
        QCOMPARE(trail.rowCount(), 1);
    }

    void telemetryServiceTracksCountersValidityAndFreshness()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::TelemetryService service(&vehicle, &trail);

        QByteArray heartbeat(9, '\0');
        heartbeat[4] = 1;
        heartbeat[5] = 12;
        heartbeat[6] = static_cast<char>(0x80U);
        heartbeat[7] = 4;

        QByteArray attitude(28, '\0');
        putFloat(&attitude, 4, 0.25F);
        putFloat(&attitude, 8, -0.1F);
        putFloat(&attitude, 12, 1.0F);

        QByteArray globalPosition(28, '\0');
        putI32(&globalPosition, 4, 374275000);
        putI32(&globalPosition, 8, -1221697000);
        putI32(&globalPosition, 12, 45000);
        putI16(&globalPosition, 20, 1200);
        putI16(&globalPosition, 22, 500);
        putI16(&globalPosition, 24, -150);
        putU16(&globalPosition, 26, 9200);

        const QByteArray datagram = mavlinkV1Frame(0, 50, heartbeat) +
                                    mavlinkV1Frame(30, 39, attitude) +
                                    mavlinkV1Frame(33, 104, globalPosition);
        QVERIFY(service.ingestDatagram(datagram));
        QCOMPARE(service.datagramCount(), 1);
        QCOMPARE(service.decodedSampleCount(), 3);
        QCOMPARE(service.decodeErrorCount(), 0);
        QVERIFY(service.linkFresh());
        QVERIFY(service.lastDatagramAgeS() >= 0.0);
        QVERIFY(service.lastDecodedAgeS() >= 0.0);

        QVERIFY(QMetaObject::invokeMethod(&service, "publishPendingSample", Qt::DirectConnection));
        QVERIFY(vehicle.heartbeatValid());
        QVERIFY(vehicle.attitudeValid());
        QVERIFY(vehicle.positionValid());
        QVERIFY(vehicle.velocityValid());
        QVERIFY(!vehicle.gpsValid());
        QVERIFY(!vehicle.missionValid());
        QVERIFY(!vehicle.homeValid());
        QVERIFY(!vehicle.terrainValid());
        QVERIFY(vehicle.armed());
        QCOMPARE(vehicle.latitudeDeg(), 37.4275);
        QCOMPARE(vehicle.groundspeedMps(), std::hypot(12.0, 5.0));
        QCOMPARE(vehicle.vzDownMps(), -1.5);

        QVERIFY(!service.ingestDatagram(QByteArray("not mavlink")));
        QCOMPARE(service.datagramCount(), 2);
        QCOMPARE(service.decodedSampleCount(), 3);
        QCOMPARE(service.decodeErrorCount(), 1);

        service.updateFreshnessForElapsedMs(1000);
        QVERIFY(service.linkFresh());
        service.updateFreshnessForElapsedMs(3000);
        QVERIFY(!service.linkFresh());
    }

    void mockTelemetryMarksPublishedDomainsFresh()
    {
        animus::VehicleModel vehicle;
        animus::BreadcrumbPathModel trail;
        animus::TelemetryService service(&vehicle, &trail);

        service.startMockTelemetry();
        QVERIFY(service.running());
        QVERIFY(service.linkFresh());
        QVERIFY(vehicle.connected());
        QVERIFY(vehicle.attitudeValid());
        QVERIFY(vehicle.positionValid());
        QVERIFY(vehicle.velocityValid());
        QVERIFY(!vehicle.gpsValid());
        service.stop();
        QVERIFY(!service.linkFresh());
    }
};

QTEST_GUILESS_MAIN(AnimusQtMapModelTests)
#include "test_map_models.moc"
