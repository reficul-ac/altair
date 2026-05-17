#include "telemetry/TelemetryService.h"

#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/MavlinkDecoder.h"

#include <QHostAddress>
#include <QUdpSocket>
#include <cmath>
#include <QtMath>

namespace animus
{

TelemetryService::TelemetryService(VehicleModel *vehicle,
                                   BreadcrumbPathModel *trail,
                                   QObject *parent)
    : QObject(parent), m_vehicle(vehicle), m_trail(trail), m_socket(nullptr), m_running(false),
      m_mockRunning(false), m_hasPendingSample(false), m_uiRateHz(20), m_udpPort(14551),
      m_udpHost(QStringLiteral("127.0.0.1")), m_elapsedS(0.0)
{
    connect(&m_timer, &QTimer::timeout, this, &TelemetryService::publishMockSample);
    connect(&m_publishTimer, &QTimer::timeout, this, &TelemetryService::publishPendingSample);
    m_publishTimer.setInterval(1000 / m_uiRateHz);
}

bool TelemetryService::running() const
{
    return m_running;
}

int TelemetryService::uiRateHz() const
{
    return m_uiRateHz;
}

void TelemetryService::setUiRateHz(int uiRateHz)
{
    const int bounded = qBound(1, uiRateHz, 30);
    if (m_uiRateHz == bounded)
        return;
    m_uiRateHz = bounded;
    if (m_mockRunning)
        m_timer.start(1000 / m_uiRateHz);
    m_publishTimer.setInterval(1000 / m_uiRateHz);
    emit uiRateHzChanged();
}

quint16 TelemetryService::udpPort() const
{
    return m_udpPort;
}

void TelemetryService::setUdpPort(quint16 udpPort)
{
    if (m_udpPort == udpPort)
        return;
    m_udpPort = udpPort;
    emit udpEndpointChanged();
}

QString TelemetryService::udpHost() const
{
    return m_udpHost;
}

void TelemetryService::setUdpHost(const QString &udpHost)
{
    if (m_udpHost == udpHost)
        return;
    m_udpHost = udpHost;
    emit udpEndpointChanged();
}

void TelemetryService::startMockTelemetry()
{
    if (m_running)
        return;
    m_mockRunning = true;
    setRunning(true);
    m_vehicle->setConnected(true);
    m_timer.start(1000 / m_uiRateHz);
}

bool TelemetryService::startUdpTelemetry()
{
    stop();

    QHostAddress address(m_udpHost);
    if (address.isNull())
        address = QHostAddress::AnyIPv4;

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(
            address, m_udpPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        m_socket->deleteLater();
        m_socket = nullptr;
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &TelemetryService::readPendingDatagrams);
    m_publishTimer.start();
    setRunning(true);
    return true;
}

void TelemetryService::stop()
{
    if (!m_running)
        return;
    m_timer.stop();
    m_publishTimer.stop();
    if (m_socket)
    {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_mockRunning = false;
    m_hasPendingSample = false;
    setRunning(false);
}

bool TelemetryService::ingestDatagram(const QByteArray &datagram)
{
    bool decoded = false;
    const QVector<MavlinkTelemetrySample> samples = m_decoder.decodeDatagram(datagram);
    for (const MavlinkTelemetrySample &sample : samples)
    {
        m_pendingSample = sample;
        m_hasPendingSample = true;
        decoded = true;
    }
    return decoded;
}

void TelemetryService::publishMockSample()
{
    m_elapsedS += 1.0 / static_cast<double>(m_uiRateHz);
    const double radiusDeg = 0.0012;
    const double angle = m_elapsedS * 0.12;
    const double lat = 37.4275 + qSin(angle) * radiusDeg;
    const double lon = -122.1697 + qCos(angle) * radiusDeg;
    const double heading = qRadiansToDegrees(angle) + 90.0;

    m_vehicle->setLatitudeDeg(lat);
    m_vehicle->setLongitudeDeg(lon);
    m_vehicle->setAltitudeM(45.0 + qSin(angle * 2.0) * 8.0);
    m_vehicle->setHeadingDeg(std::fmod(heading + 360.0, 360.0));
    m_vehicle->setYawRad(angle);
    m_vehicle->setGroundspeedMps(18.0);
    m_trail->append(lat, lon, m_vehicle->altitudeM(), m_elapsedS);
}

void TelemetryService::readPendingDatagrams()
{
    if (!m_socket)
        return;
    while (m_socket->hasPendingDatagrams())
    {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(datagram.data(), datagram.size());
        ingestDatagram(datagram);
    }
}

void TelemetryService::publishPendingSample()
{
    if (!m_hasPendingSample)
        return;
    applySample(m_pendingSample);
    m_hasPendingSample = false;
}

void TelemetryService::applySample(const MavlinkTelemetrySample &sample)
{
    m_vehicle->setSystemId(sample.systemId);
    m_vehicle->setComponentId(sample.componentId);
    m_vehicle->setConnected(true);

    if (sample.hasHeartbeat)
    {
        m_vehicle->setVehicleType(sample.vehicleType);
        m_vehicle->setAutopilot(sample.autopilot);
        m_vehicle->setBaseMode(sample.baseMode);
        m_vehicle->setSystemStatus(sample.systemStatus);
        m_vehicle->setArmed(sample.armed);
    }
    if (sample.hasAttitude)
    {
        m_vehicle->setRollRad(sample.rollRad);
        m_vehicle->setPitchRad(sample.pitchRad);
        m_vehicle->setYawRad(sample.yawRad);
    }
    if (sample.hasGlobalPosition || sample.hasGpsRaw)
    {
        m_vehicle->setLatitudeDeg(sample.latitudeDeg);
        m_vehicle->setLongitudeDeg(sample.longitudeDeg);
        m_vehicle->setAltitudeM(sample.altitudeM);
        m_trail->append(sample.latitudeDeg, sample.longitudeDeg, sample.altitudeM, m_elapsedS);
    }
    if (sample.hasGlobalPosition)
    {
        m_vehicle->setHeadingDeg(sample.headingDeg);
        m_vehicle->setVxNorthMps(sample.vxNorthMps);
        m_vehicle->setVyEastMps(sample.vyEastMps);
        m_vehicle->setVzDownMps(sample.vzDownMps);
        m_vehicle->setGroundspeedMps(std::hypot(sample.vxNorthMps, sample.vyEastMps));
    }
    if (sample.hasGpsRaw)
    {
        m_vehicle->setGpsFixType(sample.gpsFixType);
        m_vehicle->setSatellitesVisible(sample.satellitesVisible);
    }
    if (sample.hasMissionCurrent)
        m_vehicle->setMissionSeq(sample.missionSeq);
    if (sample.hasHomePosition)
    {
        m_vehicle->setHomeLatitudeDeg(sample.homeLatitudeDeg);
        m_vehicle->setHomeLongitudeDeg(sample.homeLongitudeDeg);
        m_vehicle->setHomeAltitudeM(sample.homeAltitudeM);
    }
    if (sample.hasTerrainReport)
    {
        m_vehicle->setTerrainLatitudeDeg(sample.terrainLatitudeDeg);
        m_vehicle->setTerrainLongitudeDeg(sample.terrainLongitudeDeg);
        m_vehicle->setTerrainHeightM(sample.terrainHeightM);
        m_vehicle->setTerrainCurrentHeightM(sample.terrainCurrentHeightM);
        m_vehicle->setTerrainPending(sample.terrainPending);
        m_vehicle->setTerrainLoaded(sample.terrainLoaded);
    }
}

void TelemetryService::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

} // namespace animus
