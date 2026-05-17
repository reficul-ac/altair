#pragma once

#include <QByteArray>
#include <QVector>

namespace animus {

struct MavlinkTelemetrySample {
    bool hasHeartbeat = false;
    bool hasAttitude = false;
    bool hasGlobalPosition = false;
    bool hasGpsRaw = false;
    bool hasMissionCurrent = false;
    bool hasHomePosition = false;
    bool hasTerrainReport = false;

    int systemId = 0;
    int componentId = 0;
    int vehicleType = 0;
    int autopilot = 0;
    int baseMode = 0;
    int systemStatus = 0;
    bool armed = false;

    double rollRad = 0.0;
    double pitchRad = 0.0;
    double yawRad = 0.0;

    double latitudeDeg = 0.0;
    double longitudeDeg = 0.0;
    double altitudeM = 0.0;
    double relativeAltitudeM = 0.0;
    double vxNorthMps = 0.0;
    double vyEastMps = 0.0;
    double vzDownMps = 0.0;
    double headingDeg = 0.0;

    int gpsFixType = 0;
    int satellitesVisible = -1;

    int missionSeq = -1;

    double homeLatitudeDeg = 0.0;
    double homeLongitudeDeg = 0.0;
    double homeAltitudeM = 0.0;

    double terrainLatitudeDeg = 0.0;
    double terrainLongitudeDeg = 0.0;
    double terrainHeightM = 0.0;
    double terrainCurrentHeightM = 0.0;
    int terrainPending = 0;
    int terrainLoaded = 0;
};

class MavlinkDecoder final {
public:
    QVector<MavlinkTelemetrySample> decodeDatagram(const QByteArray &datagram) const;

private:
    static bool decodeFrame(const unsigned char *frame, int frameLength, MavlinkTelemetrySample *sample);
};

} // namespace animus
