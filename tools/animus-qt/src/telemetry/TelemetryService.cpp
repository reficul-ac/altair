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
      m_mockRunning(false), m_hasPendingSample(false), m_hasDatagramTime(false),
      m_hasDecodedTime(false), m_linkFresh(false), m_uiRateHz(20), m_datagramCount(0),
      m_decodedSampleCount(0), m_decodeErrorCount(0), m_udpPort(14551),
      m_udpHost(QStringLiteral("127.0.0.1")), m_elapsedS(0.0), m_lastDatagramMs(0),
      m_lastDecodedMs(0), m_freshnessTimeoutMs(2500)
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

int TelemetryService::datagramCount() const
{
    return m_datagramCount;
}

int TelemetryService::decodedSampleCount() const
{
    return m_decodedSampleCount;
}

int TelemetryService::decodeErrorCount() const
{
    return m_decodeErrorCount;
}

double TelemetryService::lastDatagramAgeS() const
{
    if (!m_hasDatagramTime || !m_clock.isValid())
        return -1.0;
    return static_cast<double>(m_clock.elapsed() - m_lastDatagramMs) / 1000.0;
}

double TelemetryService::lastDecodedAgeS() const
{
    if (!m_hasDecodedTime || !m_clock.isValid())
        return -1.0;
    return static_cast<double>(m_clock.elapsed() - m_lastDecodedMs) / 1000.0;
}

bool TelemetryService::linkFresh() const
{
    return m_linkFresh;
}

void TelemetryService::startMockTelemetry()
{
    if (m_running)
        return;
    m_mockRunning = true;
    ensureClockStarted();
    setRunning(true);
    m_vehicle->setConnected(true);
    m_vehicle->setAttitudeValid(true);
    m_vehicle->setPositionValid(true);
    m_vehicle->setVelocityValid(true);
    m_vehicle->setHomeLatitudeDeg(37.4275);
    m_vehicle->setHomeLongitudeDeg(-122.1697);
    m_vehicle->setHomeAltitudeM(18.0);
    m_vehicle->setHomeValid(true);
    if (!m_linkFresh)
    {
        m_linkFresh = true;
        emit freshnessChanged();
    }
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
    if (m_linkFresh)
    {
        m_linkFresh = false;
        emit freshnessChanged();
    }
}

bool TelemetryService::ingestDatagram(const QByteArray &datagram)
{
    bool decoded = false;
    markDatagramReceived();
    const QVector<MavlinkTelemetrySample> samples = m_decoder.decodeDatagram(datagram);
    for (const MavlinkTelemetrySample &sample : samples)
    {
        mergePendingSample(sample);
        m_hasPendingSample = true;
        decoded = true;
        markDecodedSample();
    }
    if (!decoded)
    {
        ++m_decodeErrorCount;
        emit countersChanged();
        updateFreshness(elapsedMs());
    }
    return decoded;
}

void TelemetryService::publishMockSample()
{
    m_elapsedS += 1.0 / static_cast<double>(m_uiRateHz);
    const double radiusDeg = 0.0012;
    const double angle = m_elapsedS * 0.12;
    const double rollAmplitude = qDegreesToRadians(18.0);
    const double pitchAmplitude = qDegreesToRadians(7.0);
    const double rollPhase = angle * 3.0;
    const double pitchPhase = angle * 2.4;
    const double lat = 37.4275 + qSin(angle) * radiusDeg;
    const double lon = -122.1697 + qCos(angle) * radiusDeg;
    const double heading = qRadiansToDegrees(angle) + 90.0;

    m_vehicle->setLatitudeDeg(lat);
    m_vehicle->setLongitudeDeg(lon);
    m_vehicle->setAltitudeM(45.0 + qSin(angle * 2.0) * 8.0);
    m_vehicle->setHeadingDeg(std::fmod(heading + 360.0, 360.0));
    m_vehicle->setYawRad(angle);
    m_vehicle->setRollRad(qSin(rollPhase) * rollAmplitude);
    m_vehicle->setPitchRad(qCos(pitchPhase) * pitchAmplitude);
    m_vehicle->setRollRateRps(qCos(rollPhase) * rollAmplitude * 0.36);
    m_vehicle->setPitchRateRps(-qSin(pitchPhase) * pitchAmplitude * 0.288);
    m_vehicle->setYawRateRps(0.12);
    m_vehicle->setGroundspeedMps(18.0);
    m_vehicle->setAttitudeValid(true);
    m_vehicle->setPositionValid(true);
    m_vehicle->setVelocityValid(true);
    m_vehicle->setTerrainLatitudeDeg(lat);
    m_vehicle->setTerrainLongitudeDeg(lon);
    m_vehicle->setTerrainHeightM(18.0);
    m_vehicle->setTerrainCurrentHeightM(18.0 + qSin(angle * 0.5) * 2.0);
    m_vehicle->setTerrainLoaded(1);
    m_vehicle->setTerrainPending(0);
    m_vehicle->setTerrainValid(true);
    m_vehicle->setServoOutputPwm(1, 1500 + qRound(qSin(rollPhase) * 260.0), true);
    m_vehicle->setServoOutputPwm(2, 1500 - qRound(qSin(rollPhase) * 260.0), true);
    m_vehicle->setServoOutputPwm(3, 1500 + qRound(qSin(pitchPhase) * 220.0), true);
    m_vehicle->setServoOutputPwm(4, 1500 + qRound(qSin(angle * 1.7) * 180.0), true);
    updateFreshness(elapsedMs());
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
    updateFreshness(elapsedMs());
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
        m_vehicle->setHeartbeatValid(true);
    }
    if (sample.hasAttitude)
    {
        m_vehicle->setRollRad(sample.rollRad);
        m_vehicle->setPitchRad(sample.pitchRad);
        m_vehicle->setYawRad(sample.yawRad);
        m_vehicle->setRollRateRps(sample.rollRateRps);
        m_vehicle->setPitchRateRps(sample.pitchRateRps);
        m_vehicle->setYawRateRps(sample.yawRateRps);
        m_vehicle->setAttitudeValid(true);
    }
    if (sample.hasGlobalPosition || sample.hasGpsRaw)
    {
        m_vehicle->setLatitudeDeg(sample.latitudeDeg);
        m_vehicle->setLongitudeDeg(sample.longitudeDeg);
        m_vehicle->setAltitudeM(sample.altitudeM);
        m_vehicle->setPositionValid(true);
        m_trail->append(sample.latitudeDeg, sample.longitudeDeg, sample.altitudeM, m_elapsedS);
    }
    if (sample.hasGlobalPosition)
    {
        m_vehicle->setHeadingDeg(sample.headingDeg);
        m_vehicle->setVxNorthMps(sample.vxNorthMps);
        m_vehicle->setVyEastMps(sample.vyEastMps);
        m_vehicle->setVzDownMps(sample.vzDownMps);
        m_vehicle->setGroundspeedMps(std::hypot(sample.vxNorthMps, sample.vyEastMps));
        m_vehicle->setVelocityValid(true);
    }
    if (sample.hasGpsRaw)
    {
        m_vehicle->setGpsFixType(sample.gpsFixType);
        m_vehicle->setSatellitesVisible(sample.satellitesVisible);
        m_vehicle->setGpsValid(true);
    }
    if (sample.hasMissionCurrent)
    {
        m_vehicle->setMissionSeq(sample.missionSeq);
        m_vehicle->setMissionValid(true);
    }
    if (sample.hasHomePosition)
    {
        m_vehicle->setHomeLatitudeDeg(sample.homeLatitudeDeg);
        m_vehicle->setHomeLongitudeDeg(sample.homeLongitudeDeg);
        m_vehicle->setHomeAltitudeM(sample.homeAltitudeM);
        m_vehicle->setHomeValid(true);
    }
    if (sample.hasTerrainReport)
    {
        m_vehicle->setTerrainLatitudeDeg(sample.terrainLatitudeDeg);
        m_vehicle->setTerrainLongitudeDeg(sample.terrainLongitudeDeg);
        m_vehicle->setTerrainHeightM(sample.terrainHeightM);
        m_vehicle->setTerrainCurrentHeightM(sample.terrainCurrentHeightM);
        m_vehicle->setTerrainPending(sample.terrainPending);
        m_vehicle->setTerrainLoaded(sample.terrainLoaded);
        m_vehicle->setTerrainValid(true);
    }
    if (sample.hasServoOutputRaw)
    {
        for (int index = 0; index < MavlinkTelemetrySample::MaxServoOutputs; ++index)
        {
            m_vehicle->setServoOutputPwm(
                index + 1, sample.servoOutputPwm[index], sample.servoOutputValid[index]);
        }
    }
    updateFreshness(elapsedMs());
}

void TelemetryService::mergePendingSample(const MavlinkTelemetrySample &sample)
{
    if (!m_hasPendingSample)
    {
        m_pendingSample = sample;
        return;
    }

    m_pendingSample.systemId = sample.systemId;
    m_pendingSample.componentId = sample.componentId;
    if (sample.hasHeartbeat)
    {
        m_pendingSample.hasHeartbeat = true;
        m_pendingSample.vehicleType = sample.vehicleType;
        m_pendingSample.autopilot = sample.autopilot;
        m_pendingSample.baseMode = sample.baseMode;
        m_pendingSample.systemStatus = sample.systemStatus;
        m_pendingSample.armed = sample.armed;
    }
    if (sample.hasAttitude)
    {
        m_pendingSample.hasAttitude = true;
        m_pendingSample.rollRad = sample.rollRad;
        m_pendingSample.pitchRad = sample.pitchRad;
        m_pendingSample.yawRad = sample.yawRad;
        m_pendingSample.rollRateRps = sample.rollRateRps;
        m_pendingSample.pitchRateRps = sample.pitchRateRps;
        m_pendingSample.yawRateRps = sample.yawRateRps;
    }
    if (sample.hasGlobalPosition)
    {
        m_pendingSample.hasGlobalPosition = true;
        m_pendingSample.latitudeDeg = sample.latitudeDeg;
        m_pendingSample.longitudeDeg = sample.longitudeDeg;
        m_pendingSample.altitudeM = sample.altitudeM;
        m_pendingSample.relativeAltitudeM = sample.relativeAltitudeM;
        m_pendingSample.vxNorthMps = sample.vxNorthMps;
        m_pendingSample.vyEastMps = sample.vyEastMps;
        m_pendingSample.vzDownMps = sample.vzDownMps;
        m_pendingSample.headingDeg = sample.headingDeg;
    }
    if (sample.hasGpsRaw)
    {
        m_pendingSample.hasGpsRaw = true;
        m_pendingSample.latitudeDeg = sample.latitudeDeg;
        m_pendingSample.longitudeDeg = sample.longitudeDeg;
        m_pendingSample.altitudeM = sample.altitudeM;
        m_pendingSample.gpsFixType = sample.gpsFixType;
        m_pendingSample.satellitesVisible = sample.satellitesVisible;
    }
    if (sample.hasMissionCurrent)
    {
        m_pendingSample.hasMissionCurrent = true;
        m_pendingSample.missionSeq = sample.missionSeq;
    }
    if (sample.hasHomePosition)
    {
        m_pendingSample.hasHomePosition = true;
        m_pendingSample.homeLatitudeDeg = sample.homeLatitudeDeg;
        m_pendingSample.homeLongitudeDeg = sample.homeLongitudeDeg;
        m_pendingSample.homeAltitudeM = sample.homeAltitudeM;
    }
    if (sample.hasTerrainReport)
    {
        m_pendingSample.hasTerrainReport = true;
        m_pendingSample.terrainLatitudeDeg = sample.terrainLatitudeDeg;
        m_pendingSample.terrainLongitudeDeg = sample.terrainLongitudeDeg;
        m_pendingSample.terrainHeightM = sample.terrainHeightM;
        m_pendingSample.terrainCurrentHeightM = sample.terrainCurrentHeightM;
        m_pendingSample.terrainPending = sample.terrainPending;
        m_pendingSample.terrainLoaded = sample.terrainLoaded;
    }
    if (sample.hasServoOutputRaw)
    {
        m_pendingSample.hasServoOutputRaw = true;
        for (int index = 0; index < MavlinkTelemetrySample::MaxServoOutputs; ++index)
        {
            if (!sample.servoOutputValid[index])
                continue;
            m_pendingSample.servoOutputPwm[index] = sample.servoOutputPwm[index];
            m_pendingSample.servoOutputValid[index] = true;
        }
    }
}

qint64 TelemetryService::elapsedMs() const
{
    return m_clock.isValid() ? m_clock.elapsed() : 0;
}

void TelemetryService::ensureClockStarted()
{
    if (!m_clock.isValid())
        m_clock.start();
}

void TelemetryService::markDatagramReceived()
{
    ensureClockStarted();
    m_lastDatagramMs = m_clock.elapsed();
    m_hasDatagramTime = true;
    ++m_datagramCount;
    emit countersChanged();
    emit freshnessChanged();
}

void TelemetryService::markDecodedSample()
{
    ensureClockStarted();
    m_lastDecodedMs = m_clock.elapsed();
    m_hasDecodedTime = true;
    ++m_decodedSampleCount;
    emit countersChanged();
    updateFreshness(m_lastDecodedMs);
}

void TelemetryService::updateFreshnessForElapsedMs(qint64 elapsedMs)
{
    updateFreshness(elapsedMs);
}

void TelemetryService::updateFreshness(qint64 nowMs)
{
    const bool fresh =
        m_mockRunning || (m_hasDecodedTime && nowMs - m_lastDecodedMs <= m_freshnessTimeoutMs);
    if (m_linkFresh == fresh)
    {
        emit freshnessChanged();
        return;
    }
    m_linkFresh = fresh;
    emit freshnessChanged();
}

void TelemetryService::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

} // namespace animus
