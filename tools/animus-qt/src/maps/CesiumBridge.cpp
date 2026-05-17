#include "maps/CesiumBridge.h"

#include "models/VehicleModel.h"

namespace animus
{

CesiumBridge::CesiumBridge(VehicleModel *vehicle, QObject *parent)
    : QObject(parent), m_vehicle(vehicle)
{
    connect(vehicle, &VehicleModel::positionChanged, this, &CesiumBridge::publishVehicle);
    connect(vehicle, &VehicleModel::attitudeChanged, this, &CesiumBridge::publishVehicle);
    publishVehicle();
}

QVariantMap CesiumBridge::latestVehicle() const
{
    return m_latestVehicle;
}

QVariantMap CesiumBridge::snapshot() const
{
    return m_latestVehicle;
}

void CesiumBridge::publishVehicle()
{
    m_latestVehicle = {
        {QStringLiteral("id"), m_vehicle->vehicleId()},
        {QStringLiteral("latDeg"), m_vehicle->latitudeDeg()},
        {QStringLiteral("lonDeg"), m_vehicle->longitudeDeg()},
        {QStringLiteral("altitudeM"), m_vehicle->altitudeM()},
        {QStringLiteral("headingDeg"), m_vehicle->headingDeg()},
        {QStringLiteral("rollRad"), m_vehicle->rollRad()},
        {QStringLiteral("pitchRad"), m_vehicle->pitchRad()},
        {QStringLiteral("yawRad"), m_vehicle->yawRad()},
    };
    emit latestVehicleChanged(m_latestVehicle);
}

} // namespace animus
