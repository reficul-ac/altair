#include "maps/MapSourceRegistry.h"
#include "maps/OfflineMapManager.h"
#include "maps/qgc/AnimusMapCacheManager.h"
#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/TelemetryService.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
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
        QCOMPARE(tileSet.value(QStringLiteral("status")).toString(), QStringLiteral("queued"));
        QVERIFY(tileSet.value(QStringLiteral("tileCount")).toInt() > 0);

        QVERIFY(manager.downloadTileSet(tileSet.value(QStringLiteral("id")).toString()));
        QCOMPARE(manager.tileSets().constFirst().toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("complete"));
        QCOMPARE(manager.progressPercent(), 100);

        QVERIFY(manager.deleteTileSet(tileSet.value(QStringLiteral("id")).toString()));
        QCOMPARE(manager.tileSets().size(), 0);
    }

    void tileSetImportExportRoundTripsMetadata()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        animus::AnimusMapCacheManager manager;
        manager.setRootPath(root.path());
        QVERIFY(manager.createTileSet(
            QStringLiteral("Exported Stanford"), -122.25, 37.36, -122.05, 37.50, 12, 12));
        const QString id =
            manager.tileSets().constFirst().toMap().value(QStringLiteral("id")).toString();
        const QString exportPath = root.filePath(QStringLiteral("tile-set.json"));
        QVERIFY(manager.exportTileSet(id, exportPath));

        animus::AnimusMapCacheManager imported;
        imported.setRootPath(root.filePath(QStringLiteral("imported")));
        QVERIFY(imported.importTileSet(exportPath));
        QCOMPARE(imported.tileSets().size(), 1);
        QCOMPARE(imported.tileSets().constFirst().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Exported Stanford"));
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
};

QTEST_MAIN(AnimusQtMapModelTests)

#include "test_map_models.moc"
