#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_core/mavlink_reducer.hpp"
#include "animus/telemetry_core/telemetry.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#if defined(ANIMUS_TESTS_HAVE_IMPORTS)
#include "animus_telemetry_v1.pb.h"

#include <hdf5.h>
#include <mcap/writer.hpp>
#endif

namespace
{

using animus::telemetry_core::load_hdf5;
using animus::telemetry_core::load_mcap_protobuf;
using animus::telemetry_core::load_tlog;
using animus::telemetry_core::load_tlog_bytes;
using animus::telemetry_core::mavlink_crc_extra;
using animus::telemetry_core::mavlink_crc_x25;
using animus::telemetry_core::mavlink_decode_numeric_field;
using animus::telemetry_core::mavlink_field_definition;
using animus::telemetry_core::mavlink_field_status;
using animus::telemetry_core::mavlink_supported_fields;
using animus::telemetry_core::parse_mavlink_stream;
using animus::telemetry_core::reduce_mavlink_messages;

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

#if defined(ANIMUS_TESTS_HAVE_IMPORTS)
animus::telemetry::v1::TelemetrySample
canonical_proto_sample(const double time_s, const double lat_deg, const double lon_deg)
{
    animus::telemetry::v1::TelemetrySample sample;
    sample.set_timestamp_s(time_s);
    sample.set_timestamp_valid(true);
    sample.set_system_id(1U);
    sample.set_component_id(42U);
    sample.set_lat_deg(lat_deg);
    sample.set_lon_deg(lon_deg);
    sample.set_altitude_msl_m(1510.0 + time_s);
    sample.set_altitude_relative_m(110.0 + time_s);
    sample.set_roll_rad(0.1 + time_s);
    sample.set_pitch_rad(-0.2);
    sample.set_yaw_rad(0.3);
    sample.set_ground_speed_mps(25.0 + time_s);
    sample.set_climb_rate_mps(-1.5);
    sample.set_heading_deg(123.0);
    sample.set_altitude_datum(animus::telemetry::v1::ALTITUDE_DATUM_MSL_ORTHOMETRIC);
    auto *fields = sample.mutable_fields();
    fields->set_position(true);
    fields->set_altitude_msl(true);
    fields->set_altitude_relative(true);
    fields->set_attitude(true);
    fields->set_velocity(true);
    fields->set_heading(true);
    return sample;
}

std::filesystem::path temp_file(const char *name)
{
    return std::filesystem::temp_directory_path() / name;
}

void write_mcap_fixture(const std::filesystem::path &path, const bool include_bad_payload = false)
{
    mcap::McapWriter writer;
    const auto open_status = writer.open(path.string(), mcap::McapWriterOptions(""));
    ASSERT_TRUE(open_status.ok()) << open_status.message;

    mcap::Schema schema("animus.telemetry.v1.TelemetrySample", "protobuf", "");
    writer.addSchema(schema);
    mcap::Channel channel("/animus/telemetry/v1/samples", "protobuf", schema.id);
    writer.addChannel(channel);

    mcap::Schema unsupported_schema("animus.telemetry.v1.Unsupported", "protobuf", "");
    writer.addSchema(unsupported_schema);
    mcap::Channel unsupported_channel("/unsupported", "protobuf", unsupported_schema.id);
    writer.addChannel(unsupported_channel);

    const auto first = canonical_proto_sample(1.0, 39.0, -120.0).SerializeAsString();
    const auto second = canonical_proto_sample(2.0, 39.2, -120.2).SerializeAsString();
    const std::vector<std::string> payloads =
        include_bad_payload ? std::vector<std::string>{first, "not protobuf", second}
                            : std::vector<std::string>{first, second};

    std::uint32_t sequence = 1U;
    for (const std::string &payload : payloads)
    {
        mcap::Message message;
        message.channelId = channel.id;
        message.sequence = sequence++;
        message.logTime = static_cast<mcap::Timestamp>(message.sequence) * 1000000000ULL;
        message.publishTime = message.logTime;
        message.data = reinterpret_cast<const std::byte *>(payload.data());
        message.dataSize = payload.size();
        ASSERT_TRUE(writer.write(message).ok());
    }

    const std::string unsupported_payload = "ignored";
    mcap::Message unsupported;
    unsupported.channelId = unsupported_channel.id;
    unsupported.sequence = sequence;
    unsupported.logTime = 1500000000ULL;
    unsupported.publishTime = unsupported.logTime;
    unsupported.data = reinterpret_cast<const std::byte *>(unsupported_payload.data());
    unsupported.dataSize = unsupported_payload.size();
    ASSERT_TRUE(writer.write(unsupported).ok());
    writer.close();
}

void write_attr_string(const hid_t group, const char *name, const char *value)
{
    const hid_t type = H5Tcopy(H5T_C_S1);
    ASSERT_GE(type, 0);
    ASSERT_GE(H5Tset_size(type, std::strlen(value) + 1U), 0);
    const hid_t space = H5Screate(H5S_SCALAR);
    ASSERT_GE(space, 0);
    const hid_t attr = H5Acreate2(group, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(attr, 0);
    EXPECT_GE(H5Awrite(attr, type, value), 0);
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(type);
}

void write_attr_int(const hid_t group, const char *name, const int value)
{
    const hid_t space = H5Screate(H5S_SCALAR);
    ASSERT_GE(space, 0);
    const hid_t attr = H5Acreate2(group, name, H5T_NATIVE_INT, space, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(attr, 0);
    EXPECT_GE(H5Awrite(attr, H5T_NATIVE_INT, &value), 0);
    H5Aclose(attr);
    H5Sclose(space);
}

void write_hdf5_fixture(const std::filesystem::path &path,
                        const bool bad_schema = false,
                        const bool missing_required = false)
{
    const hid_t file = H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(file, 0);
    const hid_t root = H5Gcreate2(file, "/animus", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(root, 0);
    const hid_t telemetry = H5Gcreate2(root, "telemetry", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(telemetry, 0);
    const hid_t version_group = H5Gcreate2(telemetry, "v1", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(version_group, 0);
    write_attr_string(version_group,
                      "schema",
                      bad_schema ? "animus.telemetry.v1.Wrong"
                                 : "animus.telemetry.v1.TelemetrySample");
    write_attr_int(version_group, "version", 1);

    constexpr std::uint32_t mask =
        (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U) | (1U << 4U) | (1U << 5U);
    std::vector<double> values{
        1.0,
        1.0,
        42.0,
        39.0,
        -120.0,
        1511.0,
        111.0,
        1.1,
        -0.2,
        0.3,
        26.0,
        -1.5,
        123.0,
        1.0,
        static_cast<double>(missing_required ? 0U : mask),
        missing_required ? 0.0 : 1.0,
        2.0,
        1.0,
        42.0,
        39.2,
        -120.2,
        1512.0,
        112.0,
        2.1,
        -0.2,
        0.3,
        27.0,
        -1.5,
        123.0,
        1.0,
        static_cast<double>(mask),
        1.0,
    };
    hsize_t dims[2] = {2U, 16U};
    const hid_t space = H5Screate_simple(2, dims, nullptr);
    ASSERT_GE(space, 0);
    const hid_t dataset = H5Dcreate2(
        version_group, "samples", H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(dataset, 0);
    EXPECT_GE(H5Dwrite(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()),
              0);
    H5Dclose(dataset);
    H5Sclose(space);
    H5Gclose(version_group);
    H5Gclose(telemetry);
    H5Gclose(root);
    H5Fclose(file);
}
#endif

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

TEST(TelemetryCoreMavlinkFields, ListsSupportedFieldDefinitions)
{
    const auto fields = mavlink_supported_fields();

    ASSERT_FALSE(fields.empty());
    const auto *roll = mavlink_field_definition("ATTITUDE", "roll");
    ASSERT_NE(roll, nullptr);
    EXPECT_EQ(roll->message_id, 30U);
    EXPECT_EQ(roll->unit, "rad");
    EXPECT_TRUE(roll->numeric);

    const auto *groundspeed = mavlink_field_definition("VFR_HUD", "groundspeed");
    ASSERT_NE(groundspeed, nullptr);
    EXPECT_EQ(groundspeed->message_id, 74U);
    EXPECT_EQ(groundspeed->unit, "m/s");
    EXPECT_TRUE(groundspeed->numeric);

    EXPECT_EQ(mavlink_field_definition("SYS_STATUS", "voltage_battery"), nullptr);
    EXPECT_EQ(mavlink_field_definition(33U, "unknown"), nullptr);
}

TEST(TelemetryCoreMavlinkFields, DecodesKnownNumericPayloads)
{
    const auto attitude = frame_v1(1, 1, 1, 30, attitude_payload(2500U, 0.25F, -0.5F, 1.25F));
    const auto parsed_attitude = parse_mavlink_stream(attitude);
    ASSERT_EQ(parsed_attitude.messages.size(), 1U);
    EXPECT_NEAR(
        *mavlink_decode_numeric_field(parsed_attitude.messages.front(), "roll"), 0.25, 1.0e-6);
    EXPECT_NEAR(
        *mavlink_decode_numeric_field(parsed_attitude.messages.front(), "pitch"), -0.5, 1.0e-6);

    const auto global =
        frame_v1(2, 1, 1, 33, global_position_payload(1000U, 39.25, -120.5, 1500000, 120000));
    const auto parsed_global = parse_mavlink_stream(global);
    ASSERT_EQ(parsed_global.messages.size(), 1U);
    EXPECT_NEAR(
        *mavlink_decode_numeric_field(parsed_global.messages.front(), "lat"), 39.25, 1.0e-7);
    EXPECT_NEAR(*mavlink_decode_numeric_field(parsed_global.messages.front(), "relative_alt"),
                120.0,
                1.0e-6);
}

TEST(TelemetryCoreMavlinkFields, IdentifiesNonNumericFields)
{
    const auto parsed = parse_mavlink_stream(frame_v1(1, 1, 1, 0, heartbeat_payload()));
    ASSERT_EQ(parsed.messages.size(), 1U);

    EXPECT_EQ(mavlink_field_status(parsed.messages.front(), "type"),
              animus::telemetry_core::MavlinkFieldObservationStatus::ObservedNonNumeric);
    EXPECT_FALSE(mavlink_decode_numeric_field(parsed.messages.front(), "type"));
    EXPECT_EQ(mavlink_field_status(parsed.messages.front(), "custom_mode"),
              animus::telemetry_core::MavlinkFieldObservationStatus::ObservedNumeric);
    EXPECT_EQ(mavlink_field_status(parsed.messages.front(), "not_a_field"),
              animus::telemetry_core::MavlinkFieldObservationStatus::Unsupported);
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

TEST(TelemetryCoreTlog, ReducerMatchesTlogTimeline)
{
    std::vector<std::uint8_t> stream;
    const auto first =
        frame_v1(1, 1, 1, 33, global_position_payload(1000, 39.0, -120.0, 1500000, 100000));
    const auto attitude = frame_v1(2, 1, 1, 30, attitude_payload(1500, 0.1F, 0.2F, 0.3F));
    const auto vfr = frame_v1(3, 1, 1, 74, vfr_hud_payload(23.5F, 271, -1.25F));
    stream.insert(stream.end(), first.begin(), first.end());
    stream.insert(stream.end(), attitude.begin(), attitude.end());
    stream.insert(stream.end(), vfr.begin(), vfr.end());

    const auto parsed = parse_mavlink_stream(stream);
    const auto direct = reduce_mavlink_messages(parsed.messages, parsed.diagnostics);
    const auto tlog = load_tlog_bytes(stream);

    ASSERT_EQ(direct.samples.size(), tlog.samples.size());
    ASSERT_EQ(direct.events.size(), tlog.events.size());
    for (std::size_t index = 0U; index < tlog.samples.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(direct.samples[index].time_s, tlog.samples[index].time_s);
        EXPECT_DOUBLE_EQ(direct.samples[index].lat_deg, tlog.samples[index].lat_deg);
        EXPECT_DOUBLE_EQ(direct.samples[index].lon_deg, tlog.samples[index].lon_deg);
    }
    EXPECT_EQ(direct.diagnostics.frames_decoded, tlog.diagnostics.frames_decoded);
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

#if defined(ANIMUS_TESTS_HAVE_IMPORTS)
TEST(TelemetryCoreImports, LoadsDeterministicMcapProtobufFixture)
{
    const auto path = temp_file("animus_generated_test.mcap");
    write_mcap_fixture(path);
    const auto timeline = load_mcap_protobuf(path);
    std::filesystem::remove(path);

    EXPECT_EQ(timeline.source_format, animus::telemetry_core::TelemetryImportFormat::McapProtobuf);
    ASSERT_EQ(timeline.samples.size(), 2U);
    EXPECT_EQ(timeline.entities.size(), 1U);
    EXPECT_EQ(timeline.events.size(), 1U);
    EXPECT_EQ(timeline.diagnostics.unsupported_channels, 1U);
    EXPECT_DOUBLE_EQ(timeline.samples[0].time_s, 1.0);
    EXPECT_NEAR(timeline.samples[1].lat_deg, 39.2, 1.0e-9);
    EXPECT_NEAR(*timeline.samples[0].altitude_msl_m, 1511.0, 1.0e-9);
    EXPECT_TRUE(timeline.samples[0].fields.attitude);
}

TEST(TelemetryCoreImports, LoadsDeterministicHdf5FixtureWithTimelineParity)
{
    const auto mcap_path = temp_file("animus_generated_parity.mcap");
    const auto hdf5_path = temp_file("animus_generated_parity.h5");
    write_mcap_fixture(mcap_path);
    write_hdf5_fixture(hdf5_path);

    const auto mcap = load_mcap_protobuf(mcap_path);
    const auto hdf5 = load_hdf5(hdf5_path);
    std::filesystem::remove(mcap_path);
    std::filesystem::remove(hdf5_path);

    ASSERT_EQ(mcap.samples.size(), hdf5.samples.size());
    EXPECT_EQ(hdf5.source_format, animus::telemetry_core::TelemetryImportFormat::Hdf5Animus);
    for (std::size_t index = 0U; index < mcap.samples.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(mcap.samples[index].time_s, hdf5.samples[index].time_s);
        EXPECT_DOUBLE_EQ(mcap.samples[index].lat_deg, hdf5.samples[index].lat_deg);
        EXPECT_DOUBLE_EQ(mcap.samples[index].lon_deg, hdf5.samples[index].lon_deg);
        EXPECT_DOUBLE_EQ(*mcap.samples[index].ground_speed_mps,
                         *hdf5.samples[index].ground_speed_mps);
    }
}

TEST(TelemetryCoreImports, ReportsMalformedMcapPayload)
{
    const auto path = temp_file("animus_generated_bad_payload.mcap");
    write_mcap_fixture(path, true);
    const auto timeline = load_mcap_protobuf(path);
    std::filesystem::remove(path);

    EXPECT_EQ(timeline.samples.size(), 2U);
    EXPECT_EQ(timeline.diagnostics.decode_failures, 1U);
}

TEST(TelemetryCoreImports, RejectsHdf5SchemaAndMissingRequiredFields)
{
    const auto bad_schema = temp_file("animus_generated_bad_schema.h5");
    write_hdf5_fixture(bad_schema, true, false);
    const auto schema_timeline = load_hdf5(bad_schema);
    std::filesystem::remove(bad_schema);
    EXPECT_TRUE(schema_timeline.samples.empty());
    EXPECT_EQ(schema_timeline.diagnostics.schema_mismatches, 1U);

    const auto missing_required = temp_file("animus_generated_missing_required.h5");
    write_hdf5_fixture(missing_required, false, true);
    const auto missing_timeline = load_hdf5(missing_required);
    std::filesystem::remove(missing_required);
    EXPECT_EQ(missing_timeline.samples.size(), 1U);
    EXPECT_EQ(missing_timeline.diagnostics.missing_required_fields, 1U);
}

TEST(TelemetryCoreImports, RejectsUnsupportedHdf5Layout)
{
    const auto path = temp_file("animus_generated_bad_layout.h5");
    const hid_t file = H5Fcreate(path.string().c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    ASSERT_GE(file, 0);
    H5Fclose(file);

    const auto timeline = load_hdf5(path);
    std::filesystem::remove(path);
    EXPECT_TRUE(timeline.samples.empty());
    EXPECT_EQ(timeline.diagnostics.unsupported_layouts, 1U);
}
#endif
