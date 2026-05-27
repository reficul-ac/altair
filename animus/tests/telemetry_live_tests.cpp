#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_live/live_telemetry_buffer.hpp"
#include "animus/telemetry_live/trail_decimation.hpp"
#include "animus/telemetry_live/udp_mavlink_receiver.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <asio.hpp>
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

void push_f32(std::vector<std::uint8_t> &payload, const float value)
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    push_u32(payload, bits);
}

std::vector<std::uint8_t> frame_v1(const std::uint8_t sequence,
                                   const std::uint8_t message_id,
                                   const std::vector<std::uint8_t> &payload)
{
    std::vector<std::uint8_t> frame{0xFE, static_cast<std::uint8_t>(payload.size()), sequence, 1, 1, message_id};
    frame.insert(frame.end(), payload.begin(), payload.end());
    const std::uint16_t crc = animus::telemetry_core::mavlink_crc_x25(
        std::span<const std::uint8_t>(frame).subspan(1U),
        animus::telemetry_core::mavlink_crc_extra(message_id).value_or(0U));
    push_u16(frame, crc);
    return frame;
}

std::vector<std::uint8_t> global_position_payload(const std::uint32_t time_ms,
                                                  const double lat_deg)
{
    std::vector<std::uint8_t> payload;
    push_u32(payload, time_ms);
    push_i32(payload, static_cast<std::int32_t>(std::llround(lat_deg * 1.0e7)));
    push_i32(payload, static_cast<std::int32_t>(std::llround(-120.0 * 1.0e7)));
    push_i32(payload, 1500000);
    push_i32(payload, 100000);
    push_i16(payload, 300);
    push_i16(payload, 400);
    push_i16(payload, -50);
    push_u16(payload, 12345);
    return payload;
}

std::vector<std::uint8_t> vfr_hud_payload()
{
    std::vector<std::uint8_t> payload;
    push_f32(payload, 17.0F);
    push_f32(payload, 23.5F);
    push_i16(payload, 271);
    push_u16(payload, 50U);
    push_f32(payload, 1400.0F);
    push_f32(payload, -1.25F);
    return payload;
}

} // namespace

TEST(TelemetryLiveBuffer, UsesReceiveTimeFallbackForUntimedMessages)
{
    animus::telemetry_live::LiveTelemetryBuffer buffer;
    buffer.ingest(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(1, 33, global_position_payload(1000, 39.0)), 1.0});
    buffer.ingest(
        animus::telemetry_live::UdpMavlinkDatagram{frame_v1(2, 74, vfr_hud_payload()), 3.5});

    const auto &timeline = buffer.timeline();
    ASSERT_EQ(timeline.samples.size(), 2U);
    EXPECT_DOUBLE_EQ(timeline.samples[0].time_s, 1.0);
    EXPECT_DOUBLE_EQ(timeline.samples[1].time_s, 3.5);
    EXPECT_NEAR(*timeline.samples[1].ground_speed_mps, 23.5, 1.0e-6);
}

TEST(TelemetryLiveBuffer, EnforcesHistoryAndSampleBounds)
{
    animus::telemetry_live::LiveTelemetryBuffer buffer({.history_seconds = 1.0, .max_samples = 2U});
    buffer.ingest(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(1, 33, global_position_payload(1000, 39.0)), 1.0});
    buffer.ingest(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(2, 33, global_position_payload(2000, 39.1)), 2.0});
    buffer.ingest(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(3, 33, global_position_payload(3500, 39.2)), 3.5});

    const auto &timeline = buffer.timeline();
    ASSERT_LE(timeline.samples.size(), 2U);
    EXPECT_GE(timeline.samples.front().time_s, 2.5);
    EXPECT_GE(buffer.stats().dropped_samples, 2U);
}

TEST(TelemetryLiveBuffer, BatchIngestKeepsLatestTrackSemantics)
{
    std::vector<animus::telemetry_live::UdpMavlinkDatagram> datagrams;
    datagrams.push_back(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(1, 33, global_position_payload(1000, 39.0)), 1.0});
    datagrams.push_back(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(2, 33, global_position_payload(2000, 39.1)), 2.0});
    datagrams.push_back(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(3, 74, vfr_hud_payload()), 2.5});

    animus::telemetry_live::LiveTelemetryBuffer sequential;
    for (const auto &datagram : datagrams)
    {
        sequential.ingest(datagram);
    }

    animus::telemetry_live::LiveTelemetryBuffer batch;
    batch.ingest(datagrams);

    const auto &sequential_timeline = sequential.timeline();
    const auto &batch_timeline = batch.timeline();
    ASSERT_EQ(batch_timeline.samples.size(), sequential_timeline.samples.size());
    ASSERT_EQ(batch_timeline.tracks.size(), sequential_timeline.tracks.size());
    EXPECT_DOUBLE_EQ(batch_timeline.end_time_s, sequential_timeline.end_time_s);
    ASSERT_TRUE(batch_timeline.entities.front().latest);
    ASSERT_TRUE(sequential_timeline.entities.front().latest);
    EXPECT_DOUBLE_EQ(batch_timeline.entities.front().latest->time_s,
                     sequential_timeline.entities.front().latest->time_s);
    EXPECT_EQ(batch.stats().last_batch_datagrams, datagrams.size());
    EXPECT_EQ(batch.stats().last_batch_messages, datagrams.size());
    EXPECT_EQ(batch.stats().last_batch_samples, 3U);
}

TEST(TelemetryLiveBuffer, DoesNotPreservePacketMessagesAsTrajectoryEvents)
{
    animus::telemetry_live::LiveTelemetryBuffer buffer;
    buffer.ingest(animus::telemetry_live::UdpMavlinkDatagram{
        frame_v1(1, 33, global_position_payload(1000, 39.0)), 1.0});
    buffer.ingest(animus::telemetry_live::UdpMavlinkDatagram{frame_v1(2, 0, std::vector<std::uint8_t>(9U, 0U)), 1.1});

    EXPECT_TRUE(buffer.timeline().events.empty());
}

TEST(UdpMavlinkReceiver, ReceivesAndDrainsLoopbackDatagram)
{
    animus::telemetry_live::UdpMavlinkReceiver receiver({"127.0.0.1", 0U, 512U});
    receiver.start();
    const std::string endpoint = receiver.local_endpoint();
    const std::size_t colon = endpoint.rfind(':');
    ASSERT_NE(colon, std::string::npos);
    const auto port = static_cast<unsigned short>(std::stoul(endpoint.substr(colon + 1U)));

    asio::io_context io;
    asio::ip::udp::socket socket(io);
    socket.open(asio::ip::udp::v4());
    const auto datagram = frame_v1(1, 33, global_position_payload(1000, 39.0));
    socket.send_to(asio::buffer(datagram),
                   asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), port));

    std::vector<animus::telemetry_live::UdpMavlinkDatagram> drained;
    for (int attempt = 0; attempt < 50 && drained.empty(); ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        drained = receiver.drain();
    }
    receiver.stop();

    ASSERT_EQ(drained.size(), 1U);
    EXPECT_EQ(drained[0].bytes, datagram);
    EXPECT_EQ(receiver.stats().datagrams, 1U);
    EXPECT_GE(receiver.stats().queue_high_water, 1U);
    EXPECT_EQ(receiver.stats().last_drain_datagrams, 1U);
    EXPECT_EQ(receiver.stats().last_drain_queue_before, 1U);
}

TEST(TelemetryLiveTrailDecimation, CapsIncludesEndpointsAndPreservesOrder)
{
    const auto indices = animus::telemetry_live::decimated_trail_indices(10U, 4U);

    ASSERT_LE(indices.size(), 4U);
    ASSERT_FALSE(indices.empty());
    EXPECT_EQ(indices.front(), 0U);
    EXPECT_EQ(indices.back(), 9U);
    for (std::size_t index = 1U; index < indices.size(); ++index)
    {
        EXPECT_LT(indices[index - 1U], indices[index]);
    }
}

TEST(TelemetryLiveTrailDecimation, SinglePointKeepsNewest)
{
    const auto indices = animus::telemetry_live::decimated_trail_indices(10U, 1U);

    ASSERT_EQ(indices.size(), 1U);
    EXPECT_EQ(indices.front(), 9U);
}
