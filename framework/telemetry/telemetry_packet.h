#ifndef ALTAIR_TELEMETRY_PACKET_H
#define ALTAIR_TELEMETRY_PACKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_MAX_PAYLOAD_LEN (128U)
#define TELEMETRY_HEADER_LEN (13U)
#define TELEMETRY_PACKET_MAX_LEN (TELEMETRY_HEADER_LEN + TELEMETRY_MAX_PAYLOAD_LEN)

typedef enum {
  TELEMETRY_TOPIC_HEARTBEAT = 1,
  TELEMETRY_TOPIC_STATE = 2,
  TELEMETRY_TOPIC_ACTUATOR = 3,
  TELEMETRY_TOPIC_ALTAIR_BASE = 1000
} telemetry_topic_t;

typedef struct {
  uint16_t topic_id;
  uint32_t timestamp_us;
  uint16_t sequence;
  uint16_t payload_len;
  uint16_t crc;
} telemetry_header_t;

typedef struct {
  telemetry_header_t header;
  uint8_t payload[TELEMETRY_MAX_PAYLOAD_LEN];
} telemetry_packet_t;

uint16_t telemetry_crc16(const uint8_t *data, size_t len);
int telemetry_encode(uint16_t topic_id,
                     uint32_t timestamp_us,
                     uint16_t sequence,
                     const uint8_t *payload,
                     uint16_t payload_len,
                     uint8_t *out,
                     size_t out_capacity,
                     size_t *encoded_len);
int telemetry_decode(const uint8_t *data, size_t len, telemetry_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif
