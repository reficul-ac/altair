#include "animus/telemetry_core/mavlink_reducer.hpp"

#include "timeline_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>

namespace animus::telemetry_core
{
namespace
{

std::uint16_t u16(const std::vector<std::uint8_t> &payload, const std::size_t offset)
{
    return static_cast<std::uint16_t>(payload[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(payload[offset + 1U]) << 8U);
}

std::uint32_t u32(const std::vector<std::uint8_t> &payload, const std::size_t offset)
{
    return static_cast<std::uint32_t>(payload[offset]) |
           (static_cast<std::uint32_t>(payload[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(payload[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(payload[offset + 3U]) << 24U);
}

std::uint64_t u64(const std::vector<std::uint8_t> &payload, const std::size_t offset)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        value |= static_cast<std::uint64_t>(payload[offset + index]) << (index * 8U);
    }
    return value;
}

std::int16_t i16(const std::vector<std::uint8_t> &payload, const std::size_t offset)
{
    return static_cast<std::int16_t>(u16(payload, offset));
}

std::int32_t i32(const std::vector<std::uint8_t> &payload, const std::size_t offset)
{
    return static_cast<std::int32_t>(u32(payload, offset));
}

float f32(const std::vector<std::uint8_t> &payload, const std::size_t offset)
{
    float value = 0.0F;
    const std::uint32_t bits = u32(payload, offset);
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::optional<double> message_time_s(const MavlinkMessage &message)
{
    const auto &payload = message.payload;
    if (message.message_id == 24U && payload.size() >= 8U)
    {
        return static_cast<double>(u64(payload, 0U)) * 0.000001;
    }
    if ((message.message_id == 30U || message.message_id == 33U) && payload.size() >= 4U)
    {
        return static_cast<double>(u32(payload, 0U)) * 0.001;
    }
    return std::nullopt;
}

bool has_position(const MavlinkTelemetryReducer::PartialState &state)
{
    return state.lat_deg && state.lon_deg;
}

TelemetrySample to_sample(const MavlinkTelemetryReducer::PartialState &state, const double time_s)
{
    TelemetrySample sample;
    sample.time_s = time_s;
    sample.entity_id = state.id;
    sample.lat_deg = *state.lat_deg;
    sample.lon_deg = *state.lon_deg;
    sample.altitude_msl_m = state.altitude_msl_m;
    sample.altitude_relative_m = state.altitude_relative_m;
    sample.roll_rad = state.roll_rad;
    sample.pitch_rad = state.pitch_rad;
    sample.yaw_rad = state.yaw_rad;
    sample.ground_speed_mps = state.ground_speed_mps;
    sample.climb_rate_mps = state.climb_rate_mps;
    sample.heading_deg = state.heading_deg;
    sample.fields = state.fields;
    return sample;
}

} // namespace

MavlinkTelemetryReducer::MavlinkTelemetryReducer(MavlinkTelemetryReducerConfig config)
    : config_(config), states_(entity_less)
{
    timeline_.source_format = TelemetryImportFormat::Tlog;
}

std::size_t MavlinkTelemetryReducer::ingest_parse_result(const MavlinkParseResult &parsed,
                                                         std::optional<double> receive_time_s)
{
    timeline_.diagnostics.frames_decoded += parsed.diagnostics.frames_decoded;
    timeline_.diagnostics.unsupported_messages += parsed.diagnostics.unsupported_messages;
    timeline_.diagnostics.crc_failures += parsed.diagnostics.crc_failures;
    timeline_.diagnostics.truncated_frames += parsed.diagnostics.truncated_frames;
    timeline_.diagnostics.signed_v2_frames += parsed.diagnostics.signed_v2_frames;
    timeline_.diagnostics.unsupported_versions += parsed.diagnostics.unsupported_versions;
    timeline_.diagnostics.malformed_frames += parsed.diagnostics.malformed_frames;
    return ingest(parsed.messages, receive_time_s);
}

std::size_t MavlinkTelemetryReducer::ingest(std::span<const MavlinkMessage> messages,
                                            std::optional<double> receive_time_s)
{
    std::size_t samples_added = 0U;
    for (const MavlinkMessage &message : messages)
    {
        const std::optional<double> decoded_time_s = message_time_s(message);
        double time_s = active_time_s_;
        if (decoded_time_s)
        {
            time_s = *decoded_time_s;
            active_time_s_ = *decoded_time_s;
            have_active_time_ = true;
        }
        else if (config_.use_receive_time_for_untimed_messages && receive_time_s)
        {
            time_s = *receive_time_s;
        }

        PartialState &state = states_[message.entity_id];
        state.id = message.entity_id;

        bool sample_ready = false;
        switch (message.message_id)
        {
        case 0:
            if (config_.preserve_message_events)
            {
                add_event(timeline_,
                          have_active_time_ ? active_time_s_ : time_s,
                          message.entity_id,
                          0U,
                          EventSeverity::Info,
                          "HEARTBEAT");
            }
            break;
        case 24:
            if (message.payload.size() >= 30U)
            {
                state.lat_deg = static_cast<double>(i32(message.payload, 8U)) * 1.0e-7;
                state.lon_deg = static_cast<double>(i32(message.payload, 12U)) * 1.0e-7;
                state.altitude_msl_m = static_cast<double>(i32(message.payload, 16U)) * 0.001;
                state.fields.position = true;
                state.fields.altitude_msl = true;
                sample_ready = has_position(state);
            }
            break;
        case 30:
            if (message.payload.size() >= 28U)
            {
                state.roll_rad = f32(message.payload, 4U);
                state.pitch_rad = f32(message.payload, 8U);
                state.yaw_rad = f32(message.payload, 12U);
                state.fields.attitude = true;
                sample_ready = has_position(state);
            }
            break;
        case 33:
            if (message.payload.size() >= 28U)
            {
                state.lat_deg = static_cast<double>(i32(message.payload, 4U)) * 1.0e-7;
                state.lon_deg = static_cast<double>(i32(message.payload, 8U)) * 1.0e-7;
                state.altitude_msl_m = static_cast<double>(i32(message.payload, 12U)) * 0.001;
                state.altitude_relative_m = static_cast<double>(i32(message.payload, 16U)) * 0.001;
                const double vx = static_cast<double>(i16(message.payload, 20U)) * 0.01;
                const double vy = static_cast<double>(i16(message.payload, 22U)) * 0.01;
                state.ground_speed_mps = std::sqrt(vx * vx + vy * vy);
                state.climb_rate_mps = -static_cast<double>(i16(message.payload, 24U)) * 0.01;
                const std::uint16_t hdg = u16(message.payload, 26U);
                if (hdg != UINT16_MAX)
                {
                    state.heading_deg = static_cast<double>(hdg) * 0.01;
                    state.fields.heading = true;
                }
                state.fields.position = true;
                state.fields.altitude_msl = true;
                state.fields.altitude_relative = true;
                state.fields.velocity = true;
                sample_ready = true;
            }
            break;
        case 74:
            if (message.payload.size() >= 20U)
            {
                state.ground_speed_mps = f32(message.payload, 4U);
                state.heading_deg = static_cast<double>(i16(message.payload, 8U));
                state.climb_rate_mps = f32(message.payload, 16U);
                state.fields.velocity = true;
                state.fields.heading = true;
                sample_ready = has_position(state);
            }
            break;
        default:
            if (config_.preserve_message_events)
            {
                add_event(timeline_,
                          have_active_time_ ? time_s : 0.0,
                          message.entity_id,
                          message.message_id,
                          EventSeverity::Info,
                          "Unsupported MAVLink message preserved");
            }
            break;
        }

        if (sample_ready && has_position(state))
        {
            timeline_.samples.push_back(to_sample(state, time_s));
            ++samples_added;
        }
    }
    if (config_.finalize_after_ingest)
    {
        finalize_timeline(timeline_);
    }
    return samples_added;
}

void MavlinkTelemetryReducer::finalize()
{
    finalize_timeline(timeline_);
}

const Timeline &MavlinkTelemetryReducer::timeline() const
{
    return timeline_;
}

Timeline MavlinkTelemetryReducer::release_timeline()
{
    finalize();
    return timeline_;
}

std::size_t MavlinkTelemetryReducer::prune(const double history_seconds,
                                           const std::size_t max_samples)
{
    if (timeline_.samples.empty())
    {
        return 0U;
    }
    const double newest_time_s = timeline_.samples.back().time_s;
    const double oldest_time_s =
        history_seconds > 0.0 ? newest_time_s - history_seconds : newest_time_s;
    const std::size_t before = timeline_.samples.size();

    timeline_.samples.erase(std::remove_if(timeline_.samples.begin(),
                                           timeline_.samples.end(),
                                           [oldest_time_s](const TelemetrySample &sample)
                                           { return sample.time_s < oldest_time_s; }),
                            timeline_.samples.end());
    if (max_samples > 0U && timeline_.samples.size() > max_samples)
    {
        timeline_.samples.erase(timeline_.samples.begin(),
                                timeline_.samples.end() - static_cast<std::ptrdiff_t>(max_samples));
    }

    const double pruned_start =
        timeline_.samples.empty() ? newest_time_s : timeline_.samples.front().time_s;
    timeline_.events.erase(std::remove_if(timeline_.events.begin(),
                                          timeline_.events.end(),
                                          [pruned_start](const Event &event)
                                          { return event.time_s < pruned_start; }),
                           timeline_.events.end());
    finalize();
    return before - timeline_.samples.size();
}

Timeline reduce_mavlink_messages(std::span<const MavlinkMessage> messages,
                                 ParserDiagnostics diagnostics)
{
    MavlinkTelemetryReducer reducer;
    (void)reducer.ingest(messages);
    Timeline timeline = reducer.release_timeline();
    timeline.diagnostics = diagnostics;
    return timeline;
}

} // namespace animus::telemetry_core
