#include "telemetry_packet.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xffU);
  p[1] = (uint8_t)((v >> 8) & 0xffU);
}

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xffU);
  p[1] = (uint8_t)((v >> 8) & 0xffU);
  p[2] = (uint8_t)((v >> 16) & 0xffU);
  p[3] = (uint8_t)((v >> 24) & 0xffU);
}

static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t telemetry_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xffffU;
  size_t i;
  for (i = 0; i < len; ++i) {
    uint8_t bit;
    crc ^= (uint16_t)data[i] << 8;
    for (bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

int telemetry_encode(uint16_t topic_id,
                     uint32_t timestamp_us,
                     uint16_t sequence,
                     const uint8_t *payload,
                     uint16_t payload_len,
                     uint8_t *out,
                     size_t out_capacity,
                     size_t *encoded_len) {
  size_t total_len = TELEMETRY_HEADER_LEN + payload_len;
  if (!out || !encoded_len || payload_len > TELEMETRY_MAX_PAYLOAD_LEN || out_capacity < total_len) {
    return -1;
  }
  out[0] = 0xa5U;
  put_u16(&out[1], topic_id);
  put_u32(&out[3], timestamp_us);
  put_u16(&out[7], sequence);
  put_u16(&out[9], payload_len);
  if (payload_len > 0U && payload) {
    memcpy(&out[TELEMETRY_HEADER_LEN], payload, payload_len);
  }
  put_u16(&out[11], 0U);
  put_u16(&out[11], telemetry_crc16(out, total_len));
  *encoded_len = total_len;
  return 0;
}

int telemetry_decode(const uint8_t *data, size_t len, telemetry_packet_t *packet) {
  uint16_t payload_len;
  uint16_t expected_crc;
  uint16_t actual_crc;
  uint8_t temp[TELEMETRY_PACKET_MAX_LEN];
  if (!data || !packet || len < TELEMETRY_HEADER_LEN || data[0] != 0xa5U) {
    return -1;
  }
  payload_len = get_u16(&data[9]);
  if (payload_len > TELEMETRY_MAX_PAYLOAD_LEN || len != TELEMETRY_HEADER_LEN + payload_len) {
    return -1;
  }
  memcpy(temp, data, len);
  expected_crc = get_u16(&temp[11]);
  put_u16(&temp[11], 0U);
  actual_crc = telemetry_crc16(temp, len);
  if (actual_crc != expected_crc) {
    return -2;
  }
  packet->header.topic_id = get_u16(&data[1]);
  packet->header.timestamp_us = get_u32(&data[3]);
  packet->header.sequence = get_u16(&data[7]);
  packet->header.payload_len = payload_len;
  packet->header.crc = expected_crc;
  if (payload_len > 0U) {
    memcpy(packet->payload, &data[TELEMETRY_HEADER_LEN], payload_len);
  }
  return 0;
}
