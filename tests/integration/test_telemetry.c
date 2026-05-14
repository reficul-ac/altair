#include "telemetry_packet.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d\n", __FILE__, __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    uint8_t payload[3] = {1U, 2U, 3U};
    uint8_t encoded[TELEMETRY_PACKET_MAX_LEN];
    uint8_t oversized[TELEMETRY_MAX_PAYLOAD_LEN + 1U];
    size_t len = 0U;
    telemetry_packet_t packet;
    CHECK(telemetry_encode(
              TELEMETRY_TOPIC_STATE, 1234U, 7U, payload, 3U, encoded, sizeof(encoded), &len) == 0);
    CHECK(len == TELEMETRY_HEADER_LEN + 3U);
    CHECK(encoded[1] == TELEMETRY_PACKET_VERSION);
    CHECK(telemetry_decode(encoded, len, &packet) == 0);
    CHECK(packet.header.version == TELEMETRY_PACKET_VERSION);
    CHECK(packet.header.topic_id == TELEMETRY_TOPIC_STATE);
    CHECK(packet.header.timestamp_us == 1234U);
    CHECK(packet.header.sequence == 7U);
    CHECK(packet.header.payload_len == 3U);
    CHECK(packet.payload[2] == 3U);

    encoded[1] = (uint8_t)(TELEMETRY_PACKET_VERSION + 1U);
    CHECK(telemetry_decode(encoded, len, &packet) == TELEMETRY_DECODE_UNSUPPORTED_VERSION);
    encoded[1] = TELEMETRY_PACKET_VERSION;

    encoded[len - 1U] ^= 0x55U;
    CHECK(telemetry_decode(encoded, len, &packet) == -2);
    encoded[len - 1U] ^= 0x55U;

    CHECK(telemetry_decode(encoded, TELEMETRY_HEADER_LEN - 1U, &packet) == -1);
    CHECK(telemetry_decode(encoded, len - 1U, &packet) == -1);

    encoded[10] = (uint8_t)((TELEMETRY_MAX_PAYLOAD_LEN + 1U) & 0xffU);
    encoded[11] = (uint8_t)(((TELEMETRY_MAX_PAYLOAD_LEN + 1U) >> 8) & 0xffU);
    CHECK(telemetry_decode(encoded, len, &packet) == -1);

    memset(oversized, 0xa5, sizeof(oversized));
    CHECK(telemetry_encode(TELEMETRY_TOPIC_STATE,
                           1234U,
                           7U,
                           oversized,
                           (uint16_t)sizeof(oversized),
                           encoded,
                           sizeof(encoded),
                           &len) == -1);
    CHECK(telemetry_encode(TELEMETRY_TOPIC_STATE,
                           1234U,
                           7U,
                           payload,
                           3U,
                           encoded,
                           TELEMETRY_HEADER_LEN + 2U,
                           &len) == -1);
    return 0;
}
