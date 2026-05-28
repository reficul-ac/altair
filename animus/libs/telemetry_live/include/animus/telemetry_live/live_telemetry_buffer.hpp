#pragma once

#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_core/mavlink_reducer.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/telemetry_live/udp_mavlink_receiver.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace animus::telemetry_live
{

struct LiveTelemetryBufferConfig
{
    double history_seconds = 120.0;
    std::size_t max_samples = 20000U;
};

struct LiveTelemetryBufferStats
{
    std::uint64_t datagrams = 0;
    std::uint64_t bytes = 0;
    std::uint64_t dropped_samples = 0;
    std::uint64_t parsed_messages = 0;
    std::uint64_t produced_samples = 0;
    std::size_t last_batch_datagrams = 0;
    std::size_t last_batch_messages = 0;
    std::size_t last_batch_samples = 0;
    std::size_t retained_samples = 0;
    double last_batch_ingest_ms = 0.0;
    double last_batch_prune_finalize_ms = 0.0;
    animus::telemetry_core::ParserDiagnostics parser_diagnostics;
};

struct ParsedUdpMavlinkDatagram
{
    double receive_time_s = 0.0;
    std::size_t byte_count = 0U;
    animus::telemetry_core::MavlinkParseResult parsed;
};

class LiveTelemetryBuffer
{
  public:
    explicit LiveTelemetryBuffer(LiveTelemetryBufferConfig config = {});

    void ingest(const UdpMavlinkDatagram &datagram);
    void ingest(std::span<const UdpMavlinkDatagram> datagrams);
    void ingest_parsed(const ParsedUdpMavlinkDatagram &datagram);
    void ingest_parsed(std::span<const ParsedUdpMavlinkDatagram> datagrams);

    [[nodiscard]] const animus::telemetry_core::Timeline &timeline() const;
    [[nodiscard]] LiveTelemetryBufferStats stats() const;

  private:
    LiveTelemetryBufferConfig config_;
    animus::telemetry_core::MavlinkTelemetryReducer reducer_;
    LiveTelemetryBufferStats stats_;
};

} // namespace animus::telemetry_live
