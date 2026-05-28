#include "animus/telemetry_live/live_telemetry_buffer.hpp"

#include "animus/telemetry_core/mavlink.hpp"

#include <chrono>
#include <vector>

namespace animus::telemetry_live
{
namespace
{

double steady_time_s()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

LiveTelemetryBuffer::LiveTelemetryBuffer(LiveTelemetryBufferConfig config)
    : config_(config), reducer_(animus::telemetry_core::MavlinkTelemetryReducerConfig{
                           .use_receive_time_for_untimed_messages = true,
                           .preserve_message_events = false,
                           .finalize_after_ingest = false})
{
}

void LiveTelemetryBuffer::ingest(const UdpMavlinkDatagram &datagram)
{
    ParsedUdpMavlinkDatagram parsed;
    parsed.receive_time_s = datagram.receive_time_s;
    parsed.byte_count = datagram.bytes.size();
    parsed.parsed = animus::telemetry_core::parse_mavlink_stream(datagram.bytes);
    ingest_parsed(parsed);
}

void LiveTelemetryBuffer::ingest(std::span<const UdpMavlinkDatagram> datagrams)
{
    if (datagrams.empty())
    {
        return;
    }
    std::vector<ParsedUdpMavlinkDatagram> parsed_datagrams;
    parsed_datagrams.reserve(datagrams.size());
    for (const UdpMavlinkDatagram &datagram : datagrams)
    {
        ParsedUdpMavlinkDatagram parsed;
        parsed.receive_time_s = datagram.receive_time_s;
        parsed.byte_count = datagram.bytes.size();
        parsed.parsed = animus::telemetry_core::parse_mavlink_stream(datagram.bytes);
        parsed_datagrams.push_back(std::move(parsed));
    }
    ingest_parsed(parsed_datagrams);
}

void LiveTelemetryBuffer::ingest_parsed(const ParsedUdpMavlinkDatagram &datagram)
{
    ingest_parsed(std::span<const ParsedUdpMavlinkDatagram>(&datagram, 1U));
}

void LiveTelemetryBuffer::ingest_parsed(std::span<const ParsedUdpMavlinkDatagram> datagrams)
{
    if (datagrams.empty())
    {
        return;
    }
    std::size_t batch_messages = 0U;
    std::size_t batch_samples = 0U;
    const double ingest_start_s = steady_time_s();
    for (const ParsedUdpMavlinkDatagram &datagram : datagrams)
    {
        ++stats_.datagrams;
        stats_.bytes += datagram.byte_count;
        batch_messages += datagram.parsed.messages.size();
        batch_samples += reducer_.ingest_parse_result(datagram.parsed, datagram.receive_time_s);
    }
    const double prune_start_s = steady_time_s();
    stats_.parsed_messages += batch_messages;
    stats_.produced_samples += batch_samples;
    stats_.last_batch_datagrams = datagrams.size();
    stats_.last_batch_messages = batch_messages;
    stats_.last_batch_samples = batch_samples;
    stats_.dropped_samples += reducer_.prune(config_.history_seconds, config_.max_samples);
    stats_.last_batch_ingest_ms = (prune_start_s - ingest_start_s) * 1000.0;
    stats_.last_batch_prune_finalize_ms = (steady_time_s() - prune_start_s) * 1000.0;
    stats_.parser_diagnostics = reducer_.timeline().diagnostics;
    stats_.retained_samples = reducer_.timeline().samples.size();
}

const animus::telemetry_core::Timeline &LiveTelemetryBuffer::timeline() const
{
    return reducer_.timeline();
}

LiveTelemetryBufferStats LiveTelemetryBuffer::stats() const
{
    return stats_;
}

} // namespace animus::telemetry_live
