#pragma once

#include <array>

#include <QObject>
#include <QString>

namespace animus
{

class VehicleModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString vehicleId READ vehicleId WRITE setVehicleId NOTIFY vehicleChanged)
    Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY vehicleChanged)
    Q_PROPERTY(double latitudeDeg READ latitudeDeg WRITE setLatitudeDeg NOTIFY positionChanged)
    Q_PROPERTY(double longitudeDeg READ longitudeDeg WRITE setLongitudeDeg NOTIFY positionChanged)
    Q_PROPERTY(double altitudeM READ altitudeM WRITE setAltitudeM NOTIFY positionChanged)
    Q_PROPERTY(double headingDeg READ headingDeg WRITE setHeadingDeg NOTIFY attitudeChanged)
    Q_PROPERTY(double rollRad READ rollRad WRITE setRollRad NOTIFY attitudeChanged)
    Q_PROPERTY(double pitchRad READ pitchRad WRITE setPitchRad NOTIFY attitudeChanged)
    Q_PROPERTY(double yawRad READ yawRad WRITE setYawRad NOTIFY attitudeChanged)
    Q_PROPERTY(
        double groundspeedMps READ groundspeedMps WRITE setGroundspeedMps NOTIFY vehicleChanged)
    Q_PROPERTY(double vxNorthMps READ vxNorthMps WRITE setVxNorthMps NOTIFY velocityChanged)
    Q_PROPERTY(double vyEastMps READ vyEastMps WRITE setVyEastMps NOTIFY velocityChanged)
    Q_PROPERTY(double vzDownMps READ vzDownMps WRITE setVzDownMps NOTIFY velocityChanged)
    Q_PROPERTY(int systemId READ systemId WRITE setSystemId NOTIFY statusChanged)
    Q_PROPERTY(int componentId READ componentId WRITE setComponentId NOTIFY statusChanged)
    Q_PROPERTY(int autopilot READ autopilot WRITE setAutopilot NOTIFY statusChanged)
    Q_PROPERTY(int vehicleType READ vehicleType WRITE setVehicleType NOTIFY statusChanged)
    Q_PROPERTY(int baseMode READ baseMode WRITE setBaseMode NOTIFY statusChanged)
    Q_PROPERTY(int systemStatus READ systemStatus WRITE setSystemStatus NOTIFY statusChanged)
    Q_PROPERTY(bool armed READ armed WRITE setArmed NOTIFY statusChanged)
    Q_PROPERTY(int gpsFixType READ gpsFixType WRITE setGpsFixType NOTIFY gpsChanged)
    Q_PROPERTY(
        int satellitesVisible READ satellitesVisible WRITE setSatellitesVisible NOTIFY gpsChanged)
    Q_PROPERTY(int missionSeq READ missionSeq WRITE setMissionSeq NOTIFY missionChanged)
    Q_PROPERTY(
        double homeLatitudeDeg READ homeLatitudeDeg WRITE setHomeLatitudeDeg NOTIFY homeChanged)
    Q_PROPERTY(
        double homeLongitudeDeg READ homeLongitudeDeg WRITE setHomeLongitudeDeg NOTIFY homeChanged)
    Q_PROPERTY(double homeAltitudeM READ homeAltitudeM WRITE setHomeAltitudeM NOTIFY homeChanged)
    Q_PROPERTY(double terrainLatitudeDeg READ terrainLatitudeDeg WRITE setTerrainLatitudeDeg NOTIFY
                   terrainChanged)
    Q_PROPERTY(double terrainLongitudeDeg READ terrainLongitudeDeg WRITE setTerrainLongitudeDeg
                   NOTIFY terrainChanged)
    Q_PROPERTY(
        double terrainHeightM READ terrainHeightM WRITE setTerrainHeightM NOTIFY terrainChanged)
    Q_PROPERTY(double terrainCurrentHeightM READ terrainCurrentHeightM WRITE
                   setTerrainCurrentHeightM NOTIFY terrainChanged)
    Q_PROPERTY(int terrainPending READ terrainPending WRITE setTerrainPending NOTIFY terrainChanged)
    Q_PROPERTY(int terrainLoaded READ terrainLoaded WRITE setTerrainLoaded NOTIFY terrainChanged)
    Q_PROPERTY(bool heartbeatValid READ heartbeatValid WRITE setHeartbeatValid NOTIFY statusChanged)
    Q_PROPERTY(bool attitudeValid READ attitudeValid WRITE setAttitudeValid NOTIFY attitudeChanged)
    Q_PROPERTY(bool positionValid READ positionValid WRITE setPositionValid NOTIFY positionChanged)
    Q_PROPERTY(bool velocityValid READ velocityValid WRITE setVelocityValid NOTIFY velocityChanged)
    Q_PROPERTY(bool gpsValid READ gpsValid WRITE setGpsValid NOTIFY gpsChanged)
    Q_PROPERTY(bool missionValid READ missionValid WRITE setMissionValid NOTIFY missionChanged)
    Q_PROPERTY(bool homeValid READ homeValid WRITE setHomeValid NOTIFY homeChanged)
    Q_PROPERTY(bool terrainValid READ terrainValid WRITE setTerrainValid NOTIFY terrainChanged)

  public:
    static constexpr int MaxServoOutputs = 16;

    explicit VehicleModel(QObject *parent = nullptr);

    QString vehicleId() const;
    void setVehicleId(const QString &vehicleId);

    bool connected() const;
    void setConnected(bool connected);

    double latitudeDeg() const;
    void setLatitudeDeg(double latitudeDeg);

    double longitudeDeg() const;
    void setLongitudeDeg(double longitudeDeg);

    double altitudeM() const;
    void setAltitudeM(double altitudeM);

    double headingDeg() const;
    void setHeadingDeg(double headingDeg);

    double rollRad() const;
    void setRollRad(double rollRad);

    double pitchRad() const;
    void setPitchRad(double pitchRad);

    double yawRad() const;
    void setYawRad(double yawRad);

    double groundspeedMps() const;
    void setGroundspeedMps(double groundspeedMps);

    double vxNorthMps() const;
    void setVxNorthMps(double vxNorthMps);

    double vyEastMps() const;
    void setVyEastMps(double vyEastMps);

    double vzDownMps() const;
    void setVzDownMps(double vzDownMps);

    int systemId() const;
    void setSystemId(int systemId);

    int componentId() const;
    void setComponentId(int componentId);

    int autopilot() const;
    void setAutopilot(int autopilot);

    int vehicleType() const;
    void setVehicleType(int vehicleType);

    int baseMode() const;
    void setBaseMode(int baseMode);

    int systemStatus() const;
    void setSystemStatus(int systemStatus);

    bool armed() const;
    void setArmed(bool armed);

    int gpsFixType() const;
    void setGpsFixType(int gpsFixType);

    int satellitesVisible() const;
    void setSatellitesVisible(int satellitesVisible);

    int missionSeq() const;
    void setMissionSeq(int missionSeq);

    double homeLatitudeDeg() const;
    void setHomeLatitudeDeg(double homeLatitudeDeg);

    double homeLongitudeDeg() const;
    void setHomeLongitudeDeg(double homeLongitudeDeg);

    double homeAltitudeM() const;
    void setHomeAltitudeM(double homeAltitudeM);

    double terrainLatitudeDeg() const;
    void setTerrainLatitudeDeg(double terrainLatitudeDeg);

    double terrainLongitudeDeg() const;
    void setTerrainLongitudeDeg(double terrainLongitudeDeg);

    double terrainHeightM() const;
    void setTerrainHeightM(double terrainHeightM);

    double terrainCurrentHeightM() const;
    void setTerrainCurrentHeightM(double terrainCurrentHeightM);

    int terrainPending() const;
    void setTerrainPending(int terrainPending);

    int terrainLoaded() const;
    void setTerrainLoaded(int terrainLoaded);

    bool heartbeatValid() const;
    void setHeartbeatValid(bool heartbeatValid);

    bool attitudeValid() const;
    void setAttitudeValid(bool attitudeValid);

    bool positionValid() const;
    void setPositionValid(bool positionValid);

    bool velocityValid() const;
    void setVelocityValid(bool velocityValid);

    bool gpsValid() const;
    void setGpsValid(bool gpsValid);

    bool missionValid() const;
    void setMissionValid(bool missionValid);

    bool homeValid() const;
    void setHomeValid(bool homeValid);

    bool terrainValid() const;
    void setTerrainValid(bool terrainValid);

    int servoOutputPwm(int channel) const;
    bool servoOutputValid(int channel) const;
    void setServoOutputPwm(int channel, int pwm, bool valid);

  signals:
    void vehicleChanged();
    void positionChanged();
    void attitudeChanged();
    void velocityChanged();
    void statusChanged();
    void gpsChanged();
    void missionChanged();
    void homeChanged();
    void terrainChanged();
    void actuatorChanged();

  private:
    QString m_vehicleId;
    bool m_connected;
    double m_latitudeDeg;
    double m_longitudeDeg;
    double m_altitudeM;
    double m_headingDeg;
    double m_rollRad;
    double m_pitchRad;
    double m_yawRad;
    double m_groundspeedMps;
    double m_vxNorthMps;
    double m_vyEastMps;
    double m_vzDownMps;
    int m_systemId;
    int m_componentId;
    int m_autopilot;
    int m_vehicleType;
    int m_baseMode;
    int m_systemStatus;
    bool m_armed;
    int m_gpsFixType;
    int m_satellitesVisible;
    int m_missionSeq;
    double m_homeLatitudeDeg;
    double m_homeLongitudeDeg;
    double m_homeAltitudeM;
    double m_terrainLatitudeDeg;
    double m_terrainLongitudeDeg;
    double m_terrainHeightM;
    double m_terrainCurrentHeightM;
    int m_terrainPending;
    int m_terrainLoaded;
    bool m_heartbeatValid;
    bool m_attitudeValid;
    bool m_positionValid;
    bool m_velocityValid;
    bool m_gpsValid;
    bool m_missionValid;
    bool m_homeValid;
    bool m_terrainValid;
    std::array<int, MaxServoOutputs> m_servoOutputPwm;
    std::array<bool, MaxServoOutputs> m_servoOutputValid;
};

} // namespace animus
