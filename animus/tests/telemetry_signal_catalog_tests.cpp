#include "telemetry_signal_catalog.hpp"

#include "animus/telemetry_core/mavlink.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{

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

std::vector<std::uint8_t> frame_v1(const std::uint8_t sequence,
                                   const std::uint8_t message_id,
                                   const std::vector<std::uint8_t> &payload)
{
    std::vector<std::uint8_t> frame{
        0xFE, static_cast<std::uint8_t>(payload.size()), sequence, 1, 1, message_id};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint16_t crc = animus::telemetry_core::mavlink_crc_x25(
        std::span<const std::uint8_t>(frame).subspan(1U),
        animus::telemetry_core::mavlink_crc_extra(message_id).value_or(0U));
    push_u16(frame, crc);
    return frame;
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

std::vector<std::uint8_t> heartbeat_payload()
{
    return {2U, 3U, 4U, 5U, 6U, 7U, 8U, 3U, 0U};
}

animus::telemetry_core::TelemetrySample sample_fixture()
{
    animus::telemetry_core::TelemetrySample sample;
    sample.time_s = 12.0;
    sample.entity_id = {1U, 1U};
    sample.lat_deg = 39.0;
    sample.lon_deg = -120.0;
    sample.altitude_msl_m = 1500.0;
    sample.roll_rad = 0.5;
    sample.ground_speed_mps = 20.0;
    sample.fields.position = true;
    sample.fields.altitude_msl = true;
    sample.fields.attitude = true;
    sample.fields.velocity = true;
    return sample;
}

animus::app::SignalRef sample_ref(const char *path)
{
    animus::app::SignalRef ref;
    ref.source = animus::app::SignalSource::Sample;
    ref.field_path = path;
    return ref;
}

animus::app::SignalRef runtime_ref(const char *path)
{
    animus::app::SignalRef ref;
    ref.source = animus::app::SignalSource::Runtime;
    ref.field_path = path;
    return ref;
}

animus::app::SignalRef derived_ref(const char *path)
{
    animus::app::SignalRef ref;
    ref.source = animus::app::SignalSource::Derived;
    ref.field_path = path;
    return ref;
}

animus::app::SignalRef mavlink_ref(const char *message, const char *field)
{
    animus::app::SignalRef ref;
    ref.source = animus::app::SignalSource::Mavlink;
    ref.mavlink_message = message;
    ref.mavlink_field = field;
    return ref;
}

} // namespace

TEST(TelemetrySignalCatalog, ListsSampleAndMavlinkSignalsDeterministically)
{
    const animus::app::SignalCatalog catalog;

    ASSERT_GT(catalog.signals().size(), 20U);
    EXPECT_EQ(catalog.signals()[0].ref.field_path, "time_s");
    EXPECT_NE(catalog.lookup(sample_ref("roll_rad")), nullptr);
    const auto *mavlink = catalog.lookup(mavlink_ref("GLOBAL_POSITION_INT", "relative_alt"));
    ASSERT_NE(mavlink, nullptr);
    EXPECT_EQ(mavlink->unit, "m");
}

TEST(TelemetrySignalCatalog, LookupRejectsUnknownFields)
{
    const animus::app::SignalCatalog catalog;

    EXPECT_EQ(catalog.lookup(sample_ref("not_real")), nullptr);
    EXPECT_EQ(catalog.lookup(mavlink_ref("SYS_STATUS", "voltage_battery")), nullptr);
}

TEST(TelemetrySignalCatalog, AppliesTransforms)
{
    EXPECT_NEAR(animus::app::SignalCatalog::apply_transform(3.14159265358979323846,
                                                            animus::app::SignalTransform::RadToDeg),
                180.0,
                1.0e-9);
    EXPECT_NEAR(animus::app::SignalCatalog::apply_transform(
                    10.0, animus::app::SignalTransform::MetersToFeet),
                32.80839895,
                1.0e-8);
    EXPECT_DOUBLE_EQ(
        animus::app::SignalCatalog::apply_transform(-4.0, animus::app::SignalTransform::Abs), 4.0);
}

TEST(TelemetrySignalCatalog, ExtractsOptionalSampleFields)
{
    const animus::app::SignalCatalog catalog;
    auto sample = sample_fixture();

    const auto valid = catalog.extract_sample(
        sample_ref("roll_rad"), sample, animus::app::SignalTransform::RadToDeg);
    EXPECT_EQ(valid.status, animus::app::SignalSampleStatus::Valid);
    EXPECT_NEAR(valid.value, 28.6478897565, 1.0e-9);

    const auto missing =
        catalog.extract_sample(sample_ref("pitch_rad"), sample, animus::app::SignalTransform::None);
    EXPECT_EQ(missing.status, animus::app::SignalSampleStatus::Unavailable);
}

TEST(TelemetrySignalCatalog, ExtractsRuntimeAndDerivedInputs)
{
    const animus::app::SignalCatalog catalog;
    animus::app::RuntimeSignalInputs runtime;
    runtime.terrain_elevation_m = 1400.0;
    runtime.terrain_clearance_m = 100.0;
    runtime.link_hz = 25.0;
    runtime.frame_time_ms = 16.0;
    runtime.resident_tile_count = 42U;
    runtime.upload_bytes_this_frame = 4096U;

    const auto clearance = catalog.extract_runtime(
        derived_ref("terrain_clearance_m"), runtime, 1.0, animus::app::SignalTransform::None);
    EXPECT_EQ(clearance.status, animus::app::SignalSampleStatus::Valid);
    EXPECT_DOUBLE_EQ(clearance.value, 100.0);

    const auto age = catalog.extract_runtime(
        runtime_ref("telemetry_age_s"), runtime, 1.0, animus::app::SignalTransform::None);
    EXPECT_EQ(age.status, animus::app::SignalSampleStatus::Unavailable);
}

TEST(TelemetrySignalCatalog, MavlinkValueStoreBoundsHistoryAndStats)
{
    animus::app::MavlinkValueStore store({.max_samples_per_field = 2U, .history_seconds = 1.0});
    for (int index = 0; index < 4; ++index)
    {
        const auto frame =
            frame_v1(static_cast<std::uint8_t>(index),
                     33,
                     global_position_payload(static_cast<std::uint32_t>(index) * 1000U,
                                             39.0 + index,
                                             -120.0,
                                             1500000,
                                             100000 + index * 1000));
        animus::telemetry_live::ParsedUdpMavlinkDatagram datagram;
        datagram.receive_time_s = static_cast<double>(index);
        datagram.byte_count = frame.size();
        datagram.parsed = animus::telemetry_core::parse_mavlink_stream(frame);
        store.ingest(datagram);
    }

    const auto stats = store.stats({1U, 1U}, 33U, "relative_alt", 4.0);
    EXPECT_EQ(stats.status, animus::telemetry_core::MavlinkFieldObservationStatus::ObservedNumeric);
    EXPECT_EQ(stats.count, 4U);
    EXPECT_EQ(stats.retained_samples, 2U);
    EXPECT_NEAR(*stats.latest_value, 103.0, 1.0e-9);
    EXPECT_NEAR(stats.approximate_hz, 1.0, 1.0e-9);
    EXPECT_NEAR(*stats.min_value, 100.0, 1.0e-9);
    EXPECT_NEAR(*stats.max_value, 103.0, 1.0e-9);
}

TEST(TelemetrySignalCatalog, MavlinkExtractionRejectsNonNumeric)
{
    animus::app::MavlinkValueStore store;
    const auto frame = frame_v1(1, 0, heartbeat_payload());
    animus::telemetry_live::ParsedUdpMavlinkDatagram datagram;
    datagram.receive_time_s = 1.0;
    datagram.byte_count = frame.size();
    datagram.parsed = animus::telemetry_core::parse_mavlink_stream(frame);
    store.ingest(datagram);

    const animus::app::SignalCatalog catalog;
    animus::app::SignalRef ref = mavlink_ref("HEARTBEAT", "type");
    ref.entity_id = animus::telemetry_core::EntityId{1U, 1U};
    const auto sample =
        catalog.extract_mavlink(ref, store, 1.0, animus::app::SignalTransform::None);
    EXPECT_EQ(sample.status, animus::app::SignalSampleStatus::NonNumeric);
}
