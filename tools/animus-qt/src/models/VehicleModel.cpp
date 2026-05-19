#include "models/VehicleModel.h"

#include <QtGlobal>

namespace animus
{

VehicleModel::VehicleModel(QObject *parent)
    : QObject(parent), m_vehicleId(QStringLiteral("vehicle-1")), m_connected(false),
      m_latitudeDeg(37.4275), m_longitudeDeg(-122.1697), m_altitudeM(30.0), m_headingDeg(0.0),
      m_rollRad(0.0), m_pitchRad(0.0), m_yawRad(0.0), m_rollRateRps(0.0), m_pitchRateRps(0.0),
      m_yawRateRps(0.0), m_groundspeedMps(0.0), m_vxNorthMps(0.0), m_vyEastMps(0.0),
      m_vzDownMps(0.0), m_systemId(0), m_componentId(0), m_autopilot(0), m_vehicleType(0),
      m_baseMode(0), m_systemStatus(0), m_armed(false), m_gpsFixType(0), m_satellitesVisible(-1),
      m_missionSeq(-1), m_homeLatitudeDeg(37.4275), m_homeLongitudeDeg(-122.1697),
      m_homeAltitudeM(0.0), m_terrainLatitudeDeg(0.0), m_terrainLongitudeDeg(0.0),
      m_terrainHeightM(0.0), m_terrainCurrentHeightM(0.0), m_terrainPending(0), m_terrainLoaded(0),
      m_heartbeatValid(false), m_attitudeValid(false), m_positionValid(false),
      m_velocityValid(false), m_gpsValid(false), m_missionValid(false), m_homeValid(false),
      m_terrainValid(false), m_servoOutputPwm{}, m_servoOutputValid{}
{
}

QString VehicleModel::vehicleId() const
{
    return m_vehicleId;
}

void VehicleModel::setVehicleId(const QString &vehicleId)
{
    if (m_vehicleId == vehicleId)
        return;
    m_vehicleId = vehicleId;
    emit vehicleChanged();
}

bool VehicleModel::connected() const
{
    return m_connected;
}

void VehicleModel::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit vehicleChanged();
}

double VehicleModel::latitudeDeg() const
{
    return m_latitudeDeg;
}

void VehicleModel::setLatitudeDeg(double latitudeDeg)
{
    if (qFuzzyCompare(m_latitudeDeg, latitudeDeg))
        return;
    m_latitudeDeg = latitudeDeg;
    emit positionChanged();
}

double VehicleModel::longitudeDeg() const
{
    return m_longitudeDeg;
}

void VehicleModel::setLongitudeDeg(double longitudeDeg)
{
    if (qFuzzyCompare(m_longitudeDeg, longitudeDeg))
        return;
    m_longitudeDeg = longitudeDeg;
    emit positionChanged();
}

double VehicleModel::altitudeM() const
{
    return m_altitudeM;
}

void VehicleModel::setAltitudeM(double altitudeM)
{
    if (qFuzzyCompare(m_altitudeM, altitudeM))
        return;
    m_altitudeM = altitudeM;
    emit positionChanged();
}

double VehicleModel::headingDeg() const
{
    return m_headingDeg;
}

void VehicleModel::setHeadingDeg(double headingDeg)
{
    if (qFuzzyCompare(m_headingDeg, headingDeg))
        return;
    m_headingDeg = headingDeg;
    emit attitudeChanged();
}

double VehicleModel::rollRad() const
{
    return m_rollRad;
}

void VehicleModel::setRollRad(double rollRad)
{
    if (qFuzzyCompare(m_rollRad, rollRad))
        return;
    m_rollRad = rollRad;
    emit attitudeChanged();
}

double VehicleModel::pitchRad() const
{
    return m_pitchRad;
}

void VehicleModel::setPitchRad(double pitchRad)
{
    if (qFuzzyCompare(m_pitchRad, pitchRad))
        return;
    m_pitchRad = pitchRad;
    emit attitudeChanged();
}

double VehicleModel::yawRad() const
{
    return m_yawRad;
}

void VehicleModel::setYawRad(double yawRad)
{
    if (qFuzzyCompare(m_yawRad, yawRad))
        return;
    m_yawRad = yawRad;
    emit attitudeChanged();
}

double VehicleModel::rollRateRps() const
{
    return m_rollRateRps;
}

void VehicleModel::setRollRateRps(double rollRateRps)
{
    if (qFuzzyCompare(m_rollRateRps, rollRateRps))
        return;
    m_rollRateRps = rollRateRps;
    emit attitudeChanged();
}

double VehicleModel::pitchRateRps() const
{
    return m_pitchRateRps;
}

void VehicleModel::setPitchRateRps(double pitchRateRps)
{
    if (qFuzzyCompare(m_pitchRateRps, pitchRateRps))
        return;
    m_pitchRateRps = pitchRateRps;
    emit attitudeChanged();
}

double VehicleModel::yawRateRps() const
{
    return m_yawRateRps;
}

void VehicleModel::setYawRateRps(double yawRateRps)
{
    if (qFuzzyCompare(m_yawRateRps, yawRateRps))
        return;
    m_yawRateRps = yawRateRps;
    emit attitudeChanged();
}

double VehicleModel::groundspeedMps() const
{
    return m_groundspeedMps;
}

void VehicleModel::setGroundspeedMps(double groundspeedMps)
{
    if (qFuzzyCompare(m_groundspeedMps, groundspeedMps))
        return;
    m_groundspeedMps = groundspeedMps;
    emit vehicleChanged();
}

double VehicleModel::vxNorthMps() const
{
    return m_vxNorthMps;
}

void VehicleModel::setVxNorthMps(double vxNorthMps)
{
    if (qFuzzyCompare(m_vxNorthMps, vxNorthMps))
        return;
    m_vxNorthMps = vxNorthMps;
    emit velocityChanged();
}

double VehicleModel::vyEastMps() const
{
    return m_vyEastMps;
}

void VehicleModel::setVyEastMps(double vyEastMps)
{
    if (qFuzzyCompare(m_vyEastMps, vyEastMps))
        return;
    m_vyEastMps = vyEastMps;
    emit velocityChanged();
}

double VehicleModel::vzDownMps() const
{
    return m_vzDownMps;
}

void VehicleModel::setVzDownMps(double vzDownMps)
{
    if (qFuzzyCompare(m_vzDownMps, vzDownMps))
        return;
    m_vzDownMps = vzDownMps;
    emit velocityChanged();
}

int VehicleModel::systemId() const
{
    return m_systemId;
}

void VehicleModel::setSystemId(int systemId)
{
    if (m_systemId == systemId)
        return;
    m_systemId = systemId;
    emit statusChanged();
}

int VehicleModel::componentId() const
{
    return m_componentId;
}

void VehicleModel::setComponentId(int componentId)
{
    if (m_componentId == componentId)
        return;
    m_componentId = componentId;
    emit statusChanged();
}

int VehicleModel::autopilot() const
{
    return m_autopilot;
}

void VehicleModel::setAutopilot(int autopilot)
{
    if (m_autopilot == autopilot)
        return;
    m_autopilot = autopilot;
    emit statusChanged();
}

int VehicleModel::vehicleType() const
{
    return m_vehicleType;
}

void VehicleModel::setVehicleType(int vehicleType)
{
    if (m_vehicleType == vehicleType)
        return;
    m_vehicleType = vehicleType;
    emit statusChanged();
}

int VehicleModel::baseMode() const
{
    return m_baseMode;
}

void VehicleModel::setBaseMode(int baseMode)
{
    if (m_baseMode == baseMode)
        return;
    m_baseMode = baseMode;
    emit statusChanged();
}

int VehicleModel::systemStatus() const
{
    return m_systemStatus;
}

void VehicleModel::setSystemStatus(int systemStatus)
{
    if (m_systemStatus == systemStatus)
        return;
    m_systemStatus = systemStatus;
    emit statusChanged();
}

bool VehicleModel::armed() const
{
    return m_armed;
}

void VehicleModel::setArmed(bool armed)
{
    if (m_armed == armed)
        return;
    m_armed = armed;
    emit statusChanged();
}

int VehicleModel::gpsFixType() const
{
    return m_gpsFixType;
}

void VehicleModel::setGpsFixType(int gpsFixType)
{
    if (m_gpsFixType == gpsFixType)
        return;
    m_gpsFixType = gpsFixType;
    emit gpsChanged();
}

int VehicleModel::satellitesVisible() const
{
    return m_satellitesVisible;
}

void VehicleModel::setSatellitesVisible(int satellitesVisible)
{
    if (m_satellitesVisible == satellitesVisible)
        return;
    m_satellitesVisible = satellitesVisible;
    emit gpsChanged();
}

int VehicleModel::missionSeq() const
{
    return m_missionSeq;
}

void VehicleModel::setMissionSeq(int missionSeq)
{
    if (m_missionSeq == missionSeq)
        return;
    m_missionSeq = missionSeq;
    emit missionChanged();
}

double VehicleModel::homeLatitudeDeg() const
{
    return m_homeLatitudeDeg;
}

void VehicleModel::setHomeLatitudeDeg(double homeLatitudeDeg)
{
    if (qFuzzyCompare(m_homeLatitudeDeg, homeLatitudeDeg))
        return;
    m_homeLatitudeDeg = homeLatitudeDeg;
    emit homeChanged();
}

double VehicleModel::homeLongitudeDeg() const
{
    return m_homeLongitudeDeg;
}

void VehicleModel::setHomeLongitudeDeg(double homeLongitudeDeg)
{
    if (qFuzzyCompare(m_homeLongitudeDeg, homeLongitudeDeg))
        return;
    m_homeLongitudeDeg = homeLongitudeDeg;
    emit homeChanged();
}

double VehicleModel::homeAltitudeM() const
{
    return m_homeAltitudeM;
}

void VehicleModel::setHomeAltitudeM(double homeAltitudeM)
{
    if (qFuzzyCompare(m_homeAltitudeM, homeAltitudeM))
        return;
    m_homeAltitudeM = homeAltitudeM;
    emit homeChanged();
}

double VehicleModel::terrainLatitudeDeg() const
{
    return m_terrainLatitudeDeg;
}

void VehicleModel::setTerrainLatitudeDeg(double terrainLatitudeDeg)
{
    if (qFuzzyCompare(m_terrainLatitudeDeg, terrainLatitudeDeg))
        return;
    m_terrainLatitudeDeg = terrainLatitudeDeg;
    emit terrainChanged();
}

double VehicleModel::terrainLongitudeDeg() const
{
    return m_terrainLongitudeDeg;
}

void VehicleModel::setTerrainLongitudeDeg(double terrainLongitudeDeg)
{
    if (qFuzzyCompare(m_terrainLongitudeDeg, terrainLongitudeDeg))
        return;
    m_terrainLongitudeDeg = terrainLongitudeDeg;
    emit terrainChanged();
}

double VehicleModel::terrainHeightM() const
{
    return m_terrainHeightM;
}

void VehicleModel::setTerrainHeightM(double terrainHeightM)
{
    if (qFuzzyCompare(m_terrainHeightM, terrainHeightM))
        return;
    m_terrainHeightM = terrainHeightM;
    emit terrainChanged();
}

double VehicleModel::terrainCurrentHeightM() const
{
    return m_terrainCurrentHeightM;
}

void VehicleModel::setTerrainCurrentHeightM(double terrainCurrentHeightM)
{
    if (qFuzzyCompare(m_terrainCurrentHeightM, terrainCurrentHeightM))
        return;
    m_terrainCurrentHeightM = terrainCurrentHeightM;
    emit terrainChanged();
}

int VehicleModel::terrainPending() const
{
    return m_terrainPending;
}

void VehicleModel::setTerrainPending(int terrainPending)
{
    if (m_terrainPending == terrainPending)
        return;
    m_terrainPending = terrainPending;
    emit terrainChanged();
}

int VehicleModel::terrainLoaded() const
{
    return m_terrainLoaded;
}

void VehicleModel::setTerrainLoaded(int terrainLoaded)
{
    if (m_terrainLoaded == terrainLoaded)
        return;
    m_terrainLoaded = terrainLoaded;
    emit terrainChanged();
}

bool VehicleModel::heartbeatValid() const
{
    return m_heartbeatValid;
}

void VehicleModel::setHeartbeatValid(bool heartbeatValid)
{
    if (m_heartbeatValid == heartbeatValid)
        return;
    m_heartbeatValid = heartbeatValid;
    emit statusChanged();
}

bool VehicleModel::attitudeValid() const
{
    return m_attitudeValid;
}

void VehicleModel::setAttitudeValid(bool attitudeValid)
{
    if (m_attitudeValid == attitudeValid)
        return;
    m_attitudeValid = attitudeValid;
    emit attitudeChanged();
}

bool VehicleModel::positionValid() const
{
    return m_positionValid;
}

void VehicleModel::setPositionValid(bool positionValid)
{
    if (m_positionValid == positionValid)
        return;
    m_positionValid = positionValid;
    emit positionChanged();
}

bool VehicleModel::velocityValid() const
{
    return m_velocityValid;
}

void VehicleModel::setVelocityValid(bool velocityValid)
{
    if (m_velocityValid == velocityValid)
        return;
    m_velocityValid = velocityValid;
    emit velocityChanged();
}

bool VehicleModel::gpsValid() const
{
    return m_gpsValid;
}

void VehicleModel::setGpsValid(bool gpsValid)
{
    if (m_gpsValid == gpsValid)
        return;
    m_gpsValid = gpsValid;
    emit gpsChanged();
}

bool VehicleModel::missionValid() const
{
    return m_missionValid;
}

void VehicleModel::setMissionValid(bool missionValid)
{
    if (m_missionValid == missionValid)
        return;
    m_missionValid = missionValid;
    emit missionChanged();
}

bool VehicleModel::homeValid() const
{
    return m_homeValid;
}

void VehicleModel::setHomeValid(bool homeValid)
{
    if (m_homeValid == homeValid)
        return;
    m_homeValid = homeValid;
    emit homeChanged();
}

bool VehicleModel::terrainValid() const
{
    return m_terrainValid;
}

void VehicleModel::setTerrainValid(bool terrainValid)
{
    if (m_terrainValid == terrainValid)
        return;
    m_terrainValid = terrainValid;
    emit terrainChanged();
}

int VehicleModel::servoOutputPwm(int channel) const
{
    if (channel < 1 || channel > MaxServoOutputs)
        return 0;
    return m_servoOutputPwm[static_cast<std::size_t>(channel - 1)];
}

bool VehicleModel::servoOutputValid(int channel) const
{
    if (channel < 1 || channel > MaxServoOutputs)
        return false;
    return m_servoOutputValid[static_cast<std::size_t>(channel - 1)];
}

void VehicleModel::setServoOutputPwm(int channel, int pwm, bool valid)
{
    if (channel < 1 || channel > MaxServoOutputs)
        return;
    const auto index = static_cast<std::size_t>(channel - 1);
    if (m_servoOutputPwm[index] == pwm && m_servoOutputValid[index] == valid)
        return;
    m_servoOutputPwm[index] = pwm;
    m_servoOutputValid[index] = valid;
    emit actuatorChanged();
}

} // namespace animus
