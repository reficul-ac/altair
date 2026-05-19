#pragma once

#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace animus
{

class BreadcrumbPathModel;
class VehicleModel;
class VehicleModelProfileManager;

class CesiumBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap latestVehicle READ latestVehicle NOTIFY latestVehicleChanged)
    Q_PROPERTY(QVariantMap terrainStatus READ terrainStatus NOTIFY terrainStatusChanged)
    Q_PROPERTY(QVariantMap terrainClearance READ terrainClearance NOTIFY terrainClearanceChanged)
    Q_PROPERTY(QVariantMap sceneStatus READ sceneStatus NOTIFY sceneStatusChanged)
    Q_PROPERTY(QString terrainCachePath READ terrainCachePath WRITE setTerrainCachePath NOTIFY
                   terrainCachePathChanged)

  public:
    explicit CesiumBridge(VehicleModel *vehicle,
                          BreadcrumbPathModel *trail,
                          QObject *parent = nullptr);
    CesiumBridge(VehicleModel *vehicle,
                 BreadcrumbPathModel *trail,
                 VehicleModelProfileManager *profileManager,
                 QObject *parent = nullptr);

    QVariantMap latestVehicle() const;
    QVariantMap terrainStatus() const;
    QVariantMap terrainClearance() const;
    QVariantMap sceneStatus() const;
    QString terrainCachePath() const;
    void setTerrainCachePath(const QString &terrainCachePath);

    Q_INVOKABLE QVariantMap snapshot() const;
    Q_INVOKABLE QVariantMap controlSurfaceVerificationSnapshot() const;
    Q_INVOKABLE void setSceneStatus(const QString &status, const QString &error = QString());

  signals:
    void latestVehicleChanged(const QVariantMap &vehicle);
    void trailChanged(const QVariantList &trail);
    void homeChanged(const QVariantMap &home);
    void terrainStatusChanged(const QVariantMap &terrain);
    void terrainClearanceChanged(const QVariantMap &clearance);
    void sceneStatusChanged(const QVariantMap &scene);
    void terrainCachePathChanged();

  private slots:
    void publishVehicle();
    void publishHome();
    void publishTrail();
    void publishTerrain();
    void publishClearance();

  private:
    QVariantMap vehicleMap() const;
    QVariantMap homeMap() const;
    QVariantList trailList() const;
    QVariantMap terrainMap() const;
    QVariantMap configMap() const;
    QVariantMap fixtureMap() const;
    bool hasQuantizedMeshTerrain() const;

    VehicleModel *m_vehicle;
    BreadcrumbPathModel *m_trail;
    VehicleModelProfileManager *m_profileManager;
    QScopedPointer<VehicleModelProfileManager> m_ownedProfileManager;
    QVariantMap m_latestVehicle;
    QVariantMap m_latestHome;
    QVariantList m_latestTrail;
    QVariantMap m_terrainStatus;
    QVariantMap m_terrainClearance;
    QString m_terrainCachePath;
    QVariantMap m_sceneStatus;
};

} // namespace animus
