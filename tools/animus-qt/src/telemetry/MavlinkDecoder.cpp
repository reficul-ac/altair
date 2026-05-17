#include "telemetry/MavlinkDecoder.h"

#include <QtEndian>
#include <cstring>

namespace animus {
namespace {

constexpr unsigned char MavlinkV1Stx = 0xfeU;
constexpr unsigned char MavlinkV2Stx = 0xfdU;
constexpr unsigned char ArmedFlag = 0x80U;

uint16_t crcAccumulate(unsigned char data, uint16_t crc) {
    unsigned char tmp = data ^ static_cast<unsigned char>(crc & 0xffU);
    tmp ^= static_cast<unsigned char>(tmp << 4U);
    return static_cast<uint16_t>((crc >> 8U) ^ (static_cast<uint16_t>(tmp) << 8U) ^
                                 (static_cast<uint16_t>(tmp) << 3U) ^
                                 (static_cast<uint16_t>(tmp) >> 4U));
}

bool crcExtra(unsigned int msgId, unsigned char *extra) {
    switch (msgId) {
    case 0:
        *extra = 50U;
        return true;
    case 24:
        *extra = 24U;
        return true;
    case 30:
        *extra = 39U;
        return true;
    case 33:
        *extra = 104U;
        return true;
    case 42:
        *extra = 28U;
        return true;
    case 136:
        *extra = 1U;
        return true;
    case 242:
        *extra = 104U;
        return true;
    default:
        return false;
    }
}

uint8_t readU8(const unsigned char *payload, int offset) { return payload[offset]; }

uint16_t readU16(const unsigned char *payload, int offset) {
    return qFromLittleEndian<uint16_t>(payload + offset);
}

int16_t readI16(const unsigned char *payload, int offset) {
    return static_cast<int16_t>(qFromLittleEndian<uint16_t>(payload + offset));
}

int32_t readI32(const unsigned char *payload, int offset) {
    return static_cast<int32_t>(qFromLittleEndian<uint32_t>(payload + offset));
}

float readFloat(const unsigned char *payload, int offset) {
    const uint32_t raw = qFromLittleEndian<uint32_t>(payload + offset);
    float value = 0.0F;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

bool verifyChecksum(const unsigned char *frame, int checksumOffset, unsigned int msgId) {
    unsigned char extra = 0U;
    if (!crcExtra(msgId, &extra)) return false;

    uint16_t checksum = 0xffffU;
    for (int i = 1; i < checksumOffset; ++i) {
        checksum = crcAccumulate(frame[i], checksum);
    }
    checksum = crcAccumulate(extra, checksum);

    const uint16_t expected = readU16(frame, checksumOffset);
    return checksum == expected;
}

} // namespace

QVector<MavlinkTelemetrySample> MavlinkDecoder::decodeDatagram(const QByteArray &datagram) const {
    QVector<MavlinkTelemetrySample> samples;
    const auto *bytes = reinterpret_cast<const unsigned char *>(datagram.constData());
    const int size = datagram.size();
    int offset = 0;
    while (offset < size) {
        const unsigned char stx = bytes[offset];
        int frameLength = 0;
        if (stx == MavlinkV1Stx && offset + 8 <= size) {
            const int payloadLength = bytes[offset + 1];
            frameLength = 6 + payloadLength + 2;
        } else if (stx == MavlinkV2Stx && offset + 12 <= size) {
            const int payloadLength = bytes[offset + 1];
            const bool signedFrame = (bytes[offset + 2] & 0x01U) != 0U;
            frameLength = 10 + payloadLength + 2 + (signedFrame ? 13 : 0);
        } else {
            ++offset;
            continue;
        }

        if (frameLength <= 0 || offset + frameLength > size) break;

        MavlinkTelemetrySample sample;
        if (decodeFrame(bytes + offset, frameLength, &sample)) samples.push_back(sample);
        offset += frameLength;
    }
    return samples;
}

bool MavlinkDecoder::decodeFrame(const unsigned char *frame, int frameLength, MavlinkTelemetrySample *sample) {
    const unsigned char stx = frame[0];
    const int payloadLength = frame[1];
    const unsigned char *payload = nullptr;
    unsigned int msgId = 0;
    int checksumOffset = 0;

    if (stx == MavlinkV1Stx) {
        if (frameLength < 8 || payloadLength > frameLength - 8) return false;
        sample->systemId = readU8(frame, 3);
        sample->componentId = readU8(frame, 4);
        msgId = readU8(frame, 5);
        payload = frame + 6;
        checksumOffset = 6 + payloadLength;
    } else if (stx == MavlinkV2Stx) {
        if (frameLength < 12 || payloadLength > frameLength - 12) return false;
        sample->systemId = readU8(frame, 5);
        sample->componentId = readU8(frame, 6);
        msgId = static_cast<unsigned int>(readU8(frame, 7)) |
                (static_cast<unsigned int>(readU8(frame, 8)) << 8U) |
                (static_cast<unsigned int>(readU8(frame, 9)) << 16U);
        payload = frame + 10;
        checksumOffset = 10 + payloadLength;
    } else {
        return false;
    }

    if (!verifyChecksum(frame, checksumOffset, msgId)) return false;

    switch (msgId) {
    case 0:
        if (payloadLength < 9) return false;
        sample->hasHeartbeat = true;
        sample->vehicleType = readU8(payload, 4);
        sample->autopilot = readU8(payload, 5);
        sample->baseMode = readU8(payload, 6);
        sample->systemStatus = readU8(payload, 7);
        sample->armed = (sample->baseMode & ArmedFlag) != 0;
        return true;
    case 24:
        if (payloadLength < 30) return false;
        sample->hasGpsRaw = true;
        sample->latitudeDeg = static_cast<double>(readI32(payload, 8)) / 10000000.0;
        sample->longitudeDeg = static_cast<double>(readI32(payload, 12)) / 10000000.0;
        sample->altitudeM = static_cast<double>(readI32(payload, 16)) / 1000.0;
        sample->gpsFixType = readU8(payload, 28);
        sample->satellitesVisible = readU8(payload, 29) == 255 ? -1 : readU8(payload, 29);
        return true;
    case 30:
        if (payloadLength < 28) return false;
        sample->hasAttitude = true;
        sample->rollRad = readFloat(payload, 4);
        sample->pitchRad = readFloat(payload, 8);
        sample->yawRad = readFloat(payload, 12);
        return true;
    case 33:
        if (payloadLength < 28) return false;
        sample->hasGlobalPosition = true;
        sample->latitudeDeg = static_cast<double>(readI32(payload, 4)) / 10000000.0;
        sample->longitudeDeg = static_cast<double>(readI32(payload, 8)) / 10000000.0;
        sample->altitudeM = static_cast<double>(readI32(payload, 12)) / 1000.0;
        sample->relativeAltitudeM = static_cast<double>(readI32(payload, 16)) / 1000.0;
        sample->vxNorthMps = static_cast<double>(readI16(payload, 20)) / 100.0;
        sample->vyEastMps = static_cast<double>(readI16(payload, 22)) / 100.0;
        sample->vzDownMps = static_cast<double>(readI16(payload, 24)) / 100.0;
        sample->headingDeg = readU16(payload, 26) == 65535U ? 0.0 : static_cast<double>(readU16(payload, 26)) / 100.0;
        return true;
    case 42:
        if (payloadLength < 2) return false;
        sample->hasMissionCurrent = true;
        sample->missionSeq = readU16(payload, 0);
        return true;
    case 136:
        if (payloadLength < 22) return false;
        sample->hasTerrainReport = true;
        sample->terrainLatitudeDeg = static_cast<double>(readI32(payload, 0)) / 10000000.0;
        sample->terrainLongitudeDeg = static_cast<double>(readI32(payload, 4)) / 10000000.0;
        sample->terrainHeightM = readFloat(payload, 10);
        sample->terrainCurrentHeightM = readFloat(payload, 14);
        sample->terrainPending = readU16(payload, 18);
        sample->terrainLoaded = readU16(payload, 20);
        return true;
    case 242:
        if (payloadLength < 52) return false;
        sample->hasHomePosition = true;
        sample->homeLatitudeDeg = static_cast<double>(readI32(payload, 0)) / 10000000.0;
        sample->homeLongitudeDeg = static_cast<double>(readI32(payload, 4)) / 10000000.0;
        sample->homeAltitudeM = static_cast<double>(readI32(payload, 8)) / 1000.0;
        return true;
    default:
        return false;
    }
}

} // namespace animus
