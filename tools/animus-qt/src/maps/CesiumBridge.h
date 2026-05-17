#pragma once

#include <QObject>
#include <QVariantMap>

namespace animus
{

class VehicleModel;

class CesiumBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap latestVehicle READ latestVehicle NOTIFY latestVehicleChanged)

  public:
    explicit CesiumBridge(VehicleModel *vehicle, QObject *parent = nullptr);

    QVariantMap latestVehicle() const;
    Q_INVOKABLE QVariantMap snapshot() const;

  signals:
    void latestVehicleChanged(const QVariantMap &vehicle);

  private slots:
    void publishVehicle();

  private:
    VehicleModel *m_vehicle;
    QVariantMap m_latestVehicle;
};

} // namespace animus
