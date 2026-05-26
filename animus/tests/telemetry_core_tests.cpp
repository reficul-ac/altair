#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_core/telemetry.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using animus::telemetry_core::load_tlog;
using animus::telemetry_core::load_tlog_bytes;
using animus::telemetry_core::mavlink_crc_extra;
using animus::telemetry_core::mavlink_crc_x25;
using animus::telemetry_core::parse_mavlink_stream;

void push_u16(std::vector<std::uint8_t> &payload, const std::uint16_t value)
{
    payload.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    payload.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void push_i16(std::vector<std::uint8_t> &payload, const std::int16_t value)
{
    push_u16(payload, static_cast<std::uint16_t>(value));
}

void push_u32(std::vector<std::uint8_t> &payload, const std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        payload.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void push_i32(std::vector<std::uint8_t> &payload, const std::int32_t value)
{
    push_u32(payload, static_cast<std::uint32_t>(value));
}

void push_f32(std::vector<std::uint8_t> &payload, const float value)
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    push_u32(payload, bits);
}

std::vector<std::uint8_t> frame_v1(const std::uint8_t sequence,
                                   const std::uint8_t system_id,
                                   const std::uint8_t component_id,
                                   const std::uint8_t message_id,
                                   const std::vector<std::uint8_t> &payload)
{
    std::vector<std::uint8_t> frame{0xFE,
                                    static_cast<std::uint8_t>(payload.size()),
                                    sequence,
                                    system_id,
                                    component_id,
                                    message_id};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint16_t crc = mavlink_crc_x25(std::span<const std::uint8_t>(frame).subspan(1U),
                                              mavlink_crc_extra(message_id).value_or(0U));
    push_u16(frame, crc);
    return frame;
}

std::vector<std::uint8_t> frame_v2(const std::uint8_t sequence,
                                   const std::uint8_t system_id,
                                   const std::uint8_t component_id,
                                   const std::uint32_t message_id,
                                   const std::vector<std::uint8_t> &payload)
{
    std::vector<std::uint8_t> frame{0xFD,
                                    static_cast<std::uint8_t>(payload.size()),
                                    0U,
                                    0U,
                                    sequence,
                                    system_id,
                                    component_id,
                                    static_cast<std::uint8_t>(message_id & 0xFFU),
                                    static_cast<std::uint8_t>((message_id >> 8U) & 0xFFU),
                                    static_cast<std::uint8_t>((message_id >> 16U) & 0xFFU)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint16_t crc = mavlink_crc_x25(std::span<const std::uint8_t>(frame).subspan(1U),
                                              mavlink_crc_extra(message_id).value_or(0U));
    push_u16(frame, crc);
    return frame;
}

std::vector<std::uint8_t> signed_frame_v2(const std::uint8_t sequence,
                                          const std::uint8_t system_id,
                                          const std::uint8_t component_id,
                                          const std::uint32_t message_id,
                                          const std::vector<std::uint8_t> &payload)
{
    auto frame = frame_v2(sequence, system_id, component_id, message_id, payload);
    frame[2] = 0x01U;
    frame.insert(frame.end(), 13U, 0U);
    return frame;
}

std::vector<std::uint8_t> malformed_frame_v2(const std::uint8_t sequence,
                                             const std::uint8_t system_id,
                                             const std::uint8_t component_id,
                                             const std::uint32_t message_id,
                                             const std::vector<std::uint8_t> &payload)
{
    auto frame = frame_v2(sequence, system_id, component_id, message_id, payload);
    frame[2] = 0x80U;
    return frame;
}

std::vector<std::uint8_t> heartbeat_payload()
{
    return {2U, 3U, 4U, 5U, 6U, 7U, 8U, 3U, 0U};
}

std::vector<std::uint8_t> gps_raw_int_payload(const std::uint64_t time_us,
                                              const double lat_deg,
                                              const double lon_deg,
                                              const std::int32_t alt_mm)
{
    std::vector<std::uint8_t> payload;
    for (int shift = 0; shift < 64; shift += 8)
    {
        payload.push_back(static_cast<std::uint8_t>((time_us >> shift) & 0xFFU));
    }
    push_i32(payload, static_cast<std::int32_t>(std::llround(lat_deg * 1.0e7)));
    push_i32(payload, static_cast<std::int32_t>(std::llround(lon_deg * 1.0e7)));
    push_i32(payload, alt_mm);
    push_u16(payload, 110U);
    push_u16(payload, 220U);
    push_u16(payload, 330U);
    push_u16(payload, 440U);
    payload.push_back(12U);
    payload.push_back(3U);
    payload.push_back(0U);
    payload.push_back(0U);
    payload.push_back(0U);
    payload.push_back(0U);
    return payload;
}

std::vector<std::uint8_t> global_position_payload(const std::uint32_t time_ms,
                                                  const double lat_deg,
                                                  const double lon_deg,
                                                  const std::int32_t alt_mm,
                                                  const std::int32_t rel_alt_mm)
{
    std::vector<std::uint8_t> payload;
    push_u32(payload, time_ms);
    push_i32(payload, static_cast<std::int32_t>(std::llround(lat_deg * 1.0e7)));
    push_i32(payload, static_cast<std::int32_t>(std::llround(lon_deg * 1.0e7)));
    push_i32(payload, alt_mm);
    push_i32(payload, rel_alt_mm);
    push_i16(payload, 300);
    push_i16(payload, 400);
    push_i16(payload, -50);
    push_u16(payload, 12345);
    return payload;
}

std::vector<std::uint8_t>
attitude_payload(const std::uint32_t time_ms, const float roll, const float pitch, const float yaw)
{
    std::vector<std::uint8_t> payload;
    push_u32(payload, time_ms);
    push_f32(payload, roll);
    push_f32(payload, pitch);
    push_f32(payload, yaw);
    push_f32(payload, 0.0F);
    push_f32(payload, 0.0F);
    push_f32(payload, 0.0F);
    return payload;
}

std::vector<std::uint8_t>
vfr_hud_payload(const float ground_speed, const std::int16_t heading_deg, const float climb_mps)
{
    std::vector<std::uint8_t> payload;
    push_f32(payload, 17.0F);
    push_f32(payload, ground_speed);
    push_i16(payload, heading_deg);
    push_u16(payload, 50U);
    push_f32(payload, 1400.0F);
    push_f32(payload, climb_mps);
    return payload;
}

} // namespace

TEST(TelemetryCoreTlog, EmptyLogProducesEmptyTimeline)
{
    const auto timeline = load_tlog_bytes({});
    EXPECT_TRUE(timeline.samples.empty());
    EXPECT_TRUE(timeline.entities.empty());
    EXPECT_TRUE(timeline.tracks.empty());
    EXPECT_TRUE(timeline.events.empty());
    EXPECT_EQ(timeline.start_time_s, 0.0);
    EXPECT_EQ(timeline.end_time_s, 0.0);
}

TEST(TelemetryCoreMavlink, DecodesValidV1Frame)
{
    const auto frame =
        frame_v1(7, 1, 2, 33, global_position_payload(1000, 39.1, -120.2, 1500, 400));

    const auto parsed = parse_mavlink_stream(frame);
    ASSERT_EQ(parsed.messages.size(), 1U);
    EXPECT_EQ(parsed.messages[0].version, 1);
    EXPECT_EQ(parsed.messages[0].entity_id.system_id, 1);
    EXPECT_EQ(parsed.messages[0].message_id, 33U);
    EXPECT_EQ(parsed.diagnostics.frames_decoded, 1U);
}

TEST(TelemetryCoreMavlink, DecodesValidUnsignedV2Frame)
{
    const auto frame = frame_v2(8, 3, 4, 30, attitude_payload(2000, 0.1F, -0.2F, 0.3F));

    const auto parsed = parse_mavlink_stream(frame);
    ASSERT_EQ(parsed.messages.size(), 1U);
    EXPECT_EQ(parsed.messages[0].version, 2);
    EXPECT_EQ(parsed.messages[0].entity_id.component_id, 4);
    EXPECT_EQ(parsed.messages[0].message_id, 30U);
}

TEST(TelemetryCoreMavlink, RejectsSignedV2Frames)
{
    const auto frame = signed_frame_v2(8, 3, 4, 30, attitude_payload(2000, 0.1F, -0.2F, 0.3F));

    const auto parsed = parse_mavlink_stream(frame);
    EXPECT_TRUE(parsed.messages.empty());
    EXPECT_EQ(parsed.diagnostics.signed_v2_frames, 1U);
}

TEST(TelemetryCoreMavlink, RejectsMalformedV2Frames)
{
    const auto frame = malformed_frame_v2(8, 3, 4, 30, attitude_payload(2000, 0.1F, -0.2F, 0.3F));

    const auto parsed = parse_mavlink_stream(frame);
    EXPECT_TRUE(parsed.messages.empty());
    EXPECT_EQ(parsed.diagnostics.malformed_frames, 1U);
}

TEST(TelemetryCoreMavlink, RejectsBadCrcAndRecoversTruncatedFrames)
{
    auto frame = frame_v1(1, 1, 1, 33, global_position_payload(100, 1.0, 2.0, 3, 4));
    frame.back() ^= 0x55U;
    frame.push_back(0xFE);

    const auto parsed = parse_mavlink_stream(frame);
    EXPECT_TRUE(parsed.messages.empty());
    EXPECT_EQ(parsed.diagnostics.crc_failures, 1U);
    EXPECT_EQ(parsed.diagnostics.truncated_frames, 1U);
}

TEST(TelemetryCoreMavlink, PreservesUnknownMessagesAsEvents)
{
    const std::vector<std::uint8_t> unknown{0xFE, 1, 9, 1, 1, 200, 42, 0, 0};

    const auto timeline = load_tlog_bytes(unknown);
    EXPECT_EQ(timeline.events.size(), 1U);
    EXPECT_EQ(timeline.diagnostics.unsupported_messages, 1U);
}

TEST(TelemetryCoreTlog, DecodesHeartbeatGpsRawIntAndUntimedVfrHud)
{
    std::vector<std::uint8_t> stream;
    const auto heartbeat = frame_v1(1, 1, 1, 0, heartbeat_payload());
    const auto gps = frame_v1(2, 1, 1, 24, gps_raw_int_payload(1'500'000U, 39.5, -120.5, 1550000));
    const auto vfr = frame_v1(3, 1, 1, 74, vfr_hud_payload(23.5F, 271, -1.25F));
    stream.insert(stream.end(), heartbeat.begin(), heartbeat.end());
    stream.insert(stream.end(), gps.begin(), gps.end());
    stream.insert(stream.end(), vfr.begin(), vfr.end());

    const auto timeline = load_tlog_bytes(stream);
    ASSERT_EQ(timeline.events.size(), 1U);
    EXPECT_EQ(timeline.events[0].message, "HEARTBEAT");
    ASSERT_EQ(timeline.samples.size(), 2U);
    EXPECT_DOUBLE_EQ(timeline.samples[0].time_s, 1.5);
    EXPECT_NEAR(timeline.samples[0].lat_deg, 39.5, 1.0e-7);
    EXPECT_NEAR(*timeline.samples[0].altitude_msl_m, 1550.0, 1.0e-6);
    EXPECT_DOUBLE_EQ(timeline.samples[1].time_s, 1.5);
    EXPECT_NEAR(*timeline.samples[1].ground_speed_mps, 23.5, 1.0e-6);
    EXPECT_NEAR(*timeline.samples[1].climb_rate_mps, -1.25, 1.0e-6);
    EXPECT_NEAR(*timeline.samples[1].heading_deg, 271.0, 1.0e-6);
}

TEST(TelemetryCoreTlog, DecodesCommonMessagesAndInterpolates)
{
    std::vector<std::uint8_t> stream;
    const auto first =
        frame_v1(1, 1, 1, 33, global_position_payload(1000, 39.0, -120.0, 1500000, 100000));
    const auto attitude = frame_v1(2, 1, 1, 30, attitude_payload(1500, 0.1F, 0.2F, 0.3F));
    const auto second =
        frame_v2(3, 1, 1, 33, global_position_payload(2000, 39.2, -120.2, 1510000, 110000));
    stream.insert(stream.end(), first.begin(), first.end());
    stream.insert(stream.end(), attitude.begin(), attitude.end());
    stream.insert(stream.end(), second.begin(), second.end());

    const auto timeline = load_tlog_bytes(stream);
    ASSERT_EQ(timeline.entities.size(), 1U);
    ASSERT_GE(timeline.samples.size(), 3U);
    EXPECT_DOUBLE_EQ(timeline.start_time_s, 1.0);
    EXPECT_DOUBLE_EQ(timeline.end_time_s, 2.0);

    const auto sample = timeline.sample_at({1, 1}, 1.75);
    ASSERT_TRUE(sample);
    EXPECT_NEAR(sample->lat_deg, 39.1, 1.0e-6);
    EXPECT_TRUE(sample->fields.attitude);
}

TEST(TelemetryCoreTlog, SupportsMultipleEntitiesAndFileLoad)
{
    std::vector<std::uint8_t> stream;
    const auto a = frame_v1(1, 1, 1, 33, global_position_payload(1000, 10.0, 20.0, 1000, 500));
    const auto b = frame_v1(2, 2, 1, 33, global_position_payload(900, 11.0, 21.0, 1000, 500));
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), b.begin(), b.end());

    const auto path = std::filesystem::temp_directory_path() / "animus_generated_test.tlog";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(stream.data()),
                     static_cast<std::streamsize>(stream.size()));
    }
    const auto timeline = load_tlog(path);
    std::filesystem::remove(path);

    EXPECT_EQ(timeline.entities.size(), 2U);
    ASSERT_EQ(timeline.samples.size(), 2U);
    EXPECT_LE(timeline.samples[0].time_s, timeline.samples[1].time_s);
}
