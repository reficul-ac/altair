#include "telemetry_packet.h"

#include <stdio.h>

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
    size_t len = 0U;
    telemetry_packet_t packet;
    CHECK(telemetry_encode(
              TELEMETRY_TOPIC_STATE, 1234U, 7U, payload, 3U, encoded, sizeof(encoded), &len) == 0);
    CHECK(telemetry_decode(encoded, len, &packet) == 0);
    CHECK(packet.header.topic_id == TELEMETRY_TOPIC_STATE);
    CHECK(packet.header.timestamp_us == 1234U);
    CHECK(packet.header.sequence == 7U);
    CHECK(packet.header.payload_len == 3U);
    CHECK(packet.payload[2] == 3U);
    encoded[len - 1U] ^= 0x55U;
    CHECK(telemetry_decode(encoded, len, &packet) == -2);
    return 0;
}
