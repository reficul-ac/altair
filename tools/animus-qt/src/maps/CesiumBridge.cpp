#include "maps/CesiumBridge.h"

#include "maps/TerrainClearanceAnalyzer.h"
#include "models/VehicleModelProfileManager.h"
#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"

#include <QDir>
#include <QFileInfo>
#include <QModelIndex>
#include <QUrl>

namespace animus
{
namespace
{

constexpr double kVerificationDeflectionDeg = 12.0;

} // namespace

CesiumBridge::CesiumBridge(VehicleModel *vehicle, BreadcrumbPathModel *trail, QObject *parent)
    : CesiumBridge(vehicle, trail, nullptr, parent)
{
}

CesiumBridge::CesiumBridge(VehicleModel *vehicle,
                           BreadcrumbPathModel *trail,
                           VehicleModelProfileManager *profileManager,
                           QObject *parent)
    : QObject(parent), m_vehicle(vehicle), m_trail(trail), m_profileManager(profileManager),
      m_terrainCachePath(QDir(QStringLiteral(ANIMUS_REPO_ROOT))
                             .filePath(QStringLiteral("map_cache/terrain/quantized-mesh"))),
      m_sceneStatus({{QStringLiteral("status"), QStringLiteral("initializing")},
                     {QStringLiteral("error"), QString()}})
{
    setObjectName(QStringLiteral("cesiumBridge"));
    if (!m_profileManager)
    {
        m_ownedProfileManager.reset(new VehicleModelProfileManager(this));
        m_profileManager = m_ownedProfileManager.data();
    }
    m_profileManager->setVehicleModel(vehicle);

    connect(vehicle, &VehicleModel::positionChanged, this, &CesiumBridge::publishVehicle);
    connect(vehicle, &VehicleModel::positionChanged, this, &CesiumBridge::publishClearance);
    connect(vehicle, &VehicleModel::attitudeChanged, this, &CesiumBridge::publishVehicle);
    connect(vehicle, &VehicleModel::velocityChanged, this, &CesiumBridge::publishVehicle);
    connect(vehicle, &VehicleModel::vehicleChanged, this, &CesiumBridge::publishVehicle);
    connect(vehicle, &VehicleModel::actuatorChanged, this, &CesiumBridge::publishVehicle);
    connect(vehicle, &VehicleModel::homeChanged, this, &CesiumBridge::publishHome);
    connect(vehicle, &VehicleModel::homeChanged, this, &CesiumBridge::publishClearance);
    connect(vehicle, &VehicleModel::terrainChanged, this, &CesiumBridge::publishTerrain);
    connect(vehicle, &VehicleModel::terrainChanged, this, &CesiumBridge::publishClearance);
    connect(trail, &BreadcrumbPathModel::rowsInserted, this, &CesiumBridge::publishTrail);
    connect(trail, &BreadcrumbPathModel::rowsInserted, this, &CesiumBridge::publishClearance);
    connect(trail, &BreadcrumbPathModel::rowsRemoved, this, &CesiumBridge::publishTrail);
    connect(trail, &BreadcrumbPathModel::rowsRemoved, this, &CesiumBridge::publishClearance);
    connect(trail, &BreadcrumbPathModel::modelReset, this, &CesiumBridge::publishTrail);
    connect(trail, &BreadcrumbPathModel::modelReset, this, &CesiumBridge::publishClearance);
    connect(m_profileManager,
            &VehicleModelProfileManager::selectedProfileChanged,
            this,
            &CesiumBridge::publishVehicle);
    connect(m_profileManager,
            &VehicleModelProfileManager::surfacesChanged,
            this,
            &CesiumBridge::publishVehicle);

    publishVehicle();
    publishHome();
    publishTrail();
    publishTerrain();
    publishClearance();
}

QVariantMap CesiumBridge::latestVehicle() const
{
    return m_latestVehicle;
}

QVariantMap CesiumBridge::terrainStatus() const
{
    return m_terrainStatus;
}

QVariantMap CesiumBridge::terrainClearance() const
{
    return m_terrainClearance;
}

QVariantMap CesiumBridge::sceneStatus() const
{
    return m_sceneStatus;
}

QString CesiumBridge::terrainCachePath() const
{
    return m_terrainCachePath;
}

void CesiumBridge::setTerrainCachePath(const QString &terrainCachePath)
{
    const QString cleanPath = QDir::cleanPath(terrainCachePath);
    if (m_terrainCachePath == cleanPath)
        return;
    m_terrainCachePath = cleanPath;
    publishTerrain();
    emit terrainCachePathChanged();
}

QVariantMap CesiumBridge::snapshot() const
{
    return {{QStringLiteral("vehicle"), m_latestVehicle},
            {QStringLiteral("home"), m_latestHome},
            {QStringLiteral("trail"), m_latestTrail},
            {QStringLiteral("terrain"), m_terrainStatus},
            {QStringLiteral("clearance"), m_terrainClearance},
            {QStringLiteral("scene"), m_sceneStatus},
            {QStringLiteral("model"), m_profileManager->selectedModelMap()},
            {QStringLiteral("controlSurfaces"), m_profileManager->mappedControlSurfaces()},
            {QStringLiteral("config"), configMap()}};
}

QVariantMap CesiumBridge::controlSurfaceVerificationSnapshot() const
{
    QVariantMap verification = snapshot();
    const VehicleModelProfileManager::ModelProfile &profile =
        m_profileManager->selectedModelProfile();
    QVariantList surfaces;
    surfaces.reserve(profile.surfaces.size());
    for (const VehicleModelProfileManager::SurfaceProfile &surface : profile.surfaces)
    {
        double deflectionDeg = kVerificationDeflectionDeg;
        if (surface.id == QStringLiteral("elevator"))
            deflectionDeg = 10.0;
        else if (surface.id == QStringLiteral("rudder"))
            deflectionDeg = 14.0;
        deflectionDeg *= m_profileManager->surfacePolarity(surface.id);
        const double polarity = m_profileManager->surfacePolarity(surface.id);
        surfaces.push_back(
            QVariantMap{{QStringLiteral("id"), surface.id},
                        {QStringLiteral("label"), surface.label},
                        {QStringLiteral("node"), surface.node},
                        {QStringLiteral("axis"), surface.axis},
                        {QStringLiteral("actuatorChannel"), surface.actuatorChannel},
                        {QStringLiteral("profilePolarity"), surface.profilePolarity},
                        {QStringLiteral("polarity"), polarity},
                        {QStringLiteral("polarityOverride"), QVariant()},
                        {QStringLiteral("polarityReversed"), polarity != surface.profilePolarity},
                        {QStringLiteral("deflectionDeg"), deflectionDeg},
                        {QStringLiteral("valid"), true}});
    }
    verification[QStringLiteral("controlSurfaces")] = surfaces;
    return verification;
}

void CesiumBridge::setSceneStatus(const QString &status, const QString &error)
{
    const QVariantMap scene{{QStringLiteral("status"), status}, {QStringLiteral("error"), error}};
    if (m_sceneStatus == scene)
        return;
    m_sceneStatus = scene;
    emit sceneStatusChanged(m_sceneStatus);
}

void CesiumBridge::publishVehicle()
{
    m_latestVehicle = vehicleMap();
    emit latestVehicleChanged(m_latestVehicle);
}

void CesiumBridge::publishHome()
{
    m_latestHome = homeMap();
    emit homeChanged(m_latestHome);
}

void CesiumBridge::publishTrail()
{
    m_latestTrail = trailList();
    emit trailChanged(m_latestTrail);
}

void CesiumBridge::publishTerrain()
{
    m_terrainStatus = terrainMap();
    emit terrainStatusChanged(m_terrainStatus);
}

void CesiumBridge::publishClearance()
{
    m_terrainClearance = TerrainClearanceAnalyzer::analyze(*m_vehicle, *m_trail);
    emit terrainClearanceChanged(m_terrainClearance);
}

QVariantMap CesiumBridge::vehicleMap() const
{
    return {
        {QStringLiteral("id"), m_vehicle->vehicleId()},
        {QStringLiteral("latDeg"), m_vehicle->latitudeDeg()},
        {QStringLiteral("lonDeg"), m_vehicle->longitudeDeg()},
        {QStringLiteral("altitudeM"), m_vehicle->altitudeM()},
        {QStringLiteral("headingDeg"), m_vehicle->headingDeg()},
        {QStringLiteral("rollRad"), m_vehicle->rollRad()},
        {QStringLiteral("pitchRad"), m_vehicle->pitchRad()},
        {QStringLiteral("yawRad"), m_vehicle->yawRad()},
        {QStringLiteral("rollRateRps"), m_vehicle->rollRateRps()},
        {QStringLiteral("pitchRateRps"), m_vehicle->pitchRateRps()},
        {QStringLiteral("yawRateRps"), m_vehicle->yawRateRps()},
        {QStringLiteral("groundspeedMps"), m_vehicle->groundspeedMps()},
        {QStringLiteral("positionValid"), m_vehicle->positionValid()},
        {QStringLiteral("attitudeValid"), m_vehicle->attitudeValid()},
        {QStringLiteral("velocityValid"), m_vehicle->velocityValid()},
        {QStringLiteral("connected"), m_vehicle->connected()},
    };
}

QVariantMap CesiumBridge::homeMap() const
{
    return {{QStringLiteral("latDeg"), m_vehicle->homeLatitudeDeg()},
            {QStringLiteral("lonDeg"), m_vehicle->homeLongitudeDeg()},
            {QStringLiteral("altitudeM"), m_vehicle->homeAltitudeM()},
            {QStringLiteral("valid"), m_vehicle->homeValid()}};
}

QVariantList CesiumBridge::trailList() const
{
    QVariantList trail;
    trail.reserve(m_trail->rowCount());
    for (int row = 0; row < m_trail->rowCount(); ++row)
    {
        const QModelIndex index = m_trail->index(row, 0);
        trail.push_back(QVariantMap{
            {QStringLiteral("latDeg"), m_trail->data(index, BreadcrumbPathModel::LatitudeRole)},
            {QStringLiteral("lonDeg"), m_trail->data(index, BreadcrumbPathModel::LongitudeRole)},
            {QStringLiteral("altitudeM"), m_trail->data(index, BreadcrumbPathModel::AltitudeRole)},
            {QStringLiteral("timestampS"),
             m_trail->data(index, BreadcrumbPathModel::TimestampRole)},
        });
    }
    return trail;
}

QVariantMap CesiumBridge::terrainMap() const
{
    const bool meshAvailable = hasQuantizedMeshTerrain();
    const QString terrainPath = QDir::cleanPath(m_terrainCachePath);
    QString terrainUrlPath = terrainPath;
    if (!terrainUrlPath.endsWith(QLatin1Char('/')))
        terrainUrlPath.append(QLatin1Char('/'));
    return {
        {QStringLiteral("latDeg"), m_vehicle->terrainLatitudeDeg()},
        {QStringLiteral("lonDeg"), m_vehicle->terrainLongitudeDeg()},
        {QStringLiteral("heightM"), m_vehicle->terrainHeightM()},
        {QStringLiteral("currentHeightM"), m_vehicle->terrainCurrentHeightM()},
        {QStringLiteral("pending"), m_vehicle->terrainPending()},
        {QStringLiteral("loaded"), m_vehicle->terrainLoaded()},
        {QStringLiteral("reportValid"), m_vehicle->terrainValid()},
        {QStringLiteral("terrainAvailable"), true},
        {QStringLiteral("quantizedMeshAvailable"), meshAvailable},
        {QStringLiteral("provider"),
         meshAvailable ? QStringLiteral("quantized-mesh") : QStringLiteral("heightmap-fixture")},
        {QStringLiteral("status"),
         meshAvailable ? QStringLiteral("terrain available")
                       : QStringLiteral("fixture terrain available")},
        {QStringLiteral("cachePath"), terrainPath},
        {QStringLiteral("cacheUrl"), QUrl::fromLocalFile(terrainUrlPath).toString()},
        {QStringLiteral("fixture"), fixtureMap()}};
}

QVariantMap CesiumBridge::configMap() const
{
    return {{QStringLiteral("terrainCachePath"), QDir::cleanPath(m_terrainCachePath)},
            {QStringLiteral("workspaceId"), QStringLiteral("terrain-3d")},
            {QStringLiteral("offline"), true}};
}

bool CesiumBridge::hasQuantizedMeshTerrain() const
{
    const QDir terrainDir(m_terrainCachePath);
    if (!terrainDir.exists())
        return false;
    return QFileInfo::exists(terrainDir.filePath(QStringLiteral("layer.json")));
}

QVariantMap CesiumBridge::fixtureMap() const
{
    return {{QStringLiteral("name"), QStringLiteral("cruise6dof-stanford-sim-fixture")},
            {QStringLiteral("westDeg"), -122.2607248},
            {QStringLiteral("southDeg"), 37.3552151},
            {QStringLiteral("eastDeg"), -122.0786752},
            {QStringLiteral("northDeg"), 37.4997849},
            {QStringLiteral("width"), 129},
            {QStringLiteral("height"), 129},
            {QStringLiteral("minHeightM"), 2.0},
            {QStringLiteral("maxHeightM"), 58.0},
            {QStringLiteral("imageryUrlTemplate"),
             QStringLiteral("qrc:/Animus/web/cesium/fixture/tiles/{z}/{x}/{y}.png")},
            {QStringLiteral("imageryMinimumLevel"), 0},
            {QStringLiteral("imageryMaximumLevel"), 2},
            {QStringLiteral("aircraftModelUrl"),
             QStringLiteral("fixture/aircraft/generic-fixed-wing.gltf")}};
}

} // namespace animus
