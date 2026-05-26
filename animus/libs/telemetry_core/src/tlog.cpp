#include "animus/telemetry_core/telemetry.hpp"

#include "animus/telemetry_core/mavlink.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace animus::telemetry_core
{
namespace
{

struct PartialState
{
    EntityId id;
    std::optional<double> lat_deg;
    std::optional<double> lon_deg;
    std::optional<double> altitude_msl_m;
    std::optional<double> altitude_relative_m;
    std::optional<double> roll_rad;
    std::optional<double> pitch_rad;
    std::optional<double> yaw_rad;
    std::optional<double> ground_speed_mps;
    std::optional<double> climb_rate_mps;
    std::optional<double> heading_deg;
    SourceFields fields;
};

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
    if (message.message_id == 74U && payload.size() >= 20U)
    {
        return std::nullopt;
    }
    return std::nullopt;
}

bool has_position(const PartialState &state)
{
    return state.lat_deg && state.lon_deg;
}

TelemetrySample to_sample(const PartialState &state, const double time_s)
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

bool entity_less(const EntityId a, const EntityId b)
{
    if (a.system_id != b.system_id)
    {
        return a.system_id < b.system_id;
    }
    return a.component_id < b.component_id;
}

void add_event(Timeline &timeline,
               const double time_s,
               const EntityId id,
               const std::uint32_t message_id,
               const EventSeverity severity,
               std::string message)
{
    timeline.events.push_back(Event{time_s, id, message_id, severity, std::move(message)});
}

} // namespace

Timeline load_tlog_bytes(const std::vector<std::uint8_t> &bytes)
{
    Timeline timeline;
    MavlinkParseResult parsed = parse_mavlink_stream(bytes);
    timeline.diagnostics = parsed.diagnostics;

    std::map<EntityId, PartialState, decltype(&entity_less)> states(entity_less);
    double active_time_s = 0.0;
    bool have_active_time = false;

    for (const MavlinkMessage &message : parsed.messages)
    {
        const std::optional<double> decoded_time_s = message_time_s(message);
        const double time_s = decoded_time_s.value_or(active_time_s);
        if (decoded_time_s)
        {
            active_time_s = *decoded_time_s;
            have_active_time = true;
        }
        PartialState &state = states[message.entity_id];
        state.id = message.entity_id;

        bool sample_ready = false;
        switch (message.message_id)
        {
        case 0:
            add_event(timeline,
                      have_active_time ? active_time_s : 0.0,
                      message.entity_id,
                      0U,
                      EventSeverity::Info,
                      "HEARTBEAT");
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
            add_event(timeline,
                      have_active_time ? time_s : 0.0,
                      message.entity_id,
                      message.message_id,
                      EventSeverity::Info,
                      "Unsupported MAVLink message preserved");
            break;
        }

        if (sample_ready && has_position(state))
        {
            timeline.samples.push_back(to_sample(state, time_s));
        }
    }

    std::sort(timeline.samples.begin(),
              timeline.samples.end(),
              [](const TelemetrySample &a, const TelemetrySample &b)
              {
                  if (a.time_s != b.time_s)
                  {
                      return a.time_s < b.time_s;
                  }
                  if (a.entity_id.system_id != b.entity_id.system_id)
                  {
                      return a.entity_id.system_id < b.entity_id.system_id;
                  }
                  return a.entity_id.component_id < b.entity_id.component_id;
              });
    timeline.samples.erase(
        std::unique(timeline.samples.begin(),
                    timeline.samples.end(),
                    [](const TelemetrySample &a, const TelemetrySample &b)
                    {
                        return a.time_s == b.time_s && a.entity_id == b.entity_id &&
                               a.lat_deg == b.lat_deg && a.lon_deg == b.lon_deg &&
                               a.altitude_msl_m == b.altitude_msl_m &&
                               a.altitude_relative_m == b.altitude_relative_m &&
                               a.roll_rad == b.roll_rad && a.pitch_rad == b.pitch_rad &&
                               a.yaw_rad == b.yaw_rad && a.ground_speed_mps == b.ground_speed_mps &&
                               a.climb_rate_mps == b.climb_rate_mps &&
                               a.heading_deg == b.heading_deg;
                    }),
        timeline.samples.end());

    std::map<EntityId, std::vector<TelemetrySample>, decltype(&entity_less)> by_entity(entity_less);
    for (const TelemetrySample &sample : timeline.samples)
    {
        by_entity[sample.entity_id].push_back(sample);
    }
    for (auto &[id, samples] : by_entity)
    {
        timeline.entities.push_back(Entity{id, samples.back()});
        timeline.tracks.push_back(Track{id, std::move(samples)});
    }
    if (!timeline.samples.empty())
    {
        timeline.start_time_s = timeline.samples.front().time_s;
        timeline.end_time_s = timeline.samples.back().time_s;
    }
    std::sort(timeline.events.begin(),
              timeline.events.end(),
              [](const Event &a, const Event &b)
              {
                  if (a.time_s != b.time_s)
                  {
                      return a.time_s < b.time_s;
                  }
                  if (a.entity_id.system_id != b.entity_id.system_id)
                  {
                      return a.entity_id.system_id < b.entity_id.system_id;
                  }
                  if (a.entity_id.component_id != b.entity_id.component_id)
                  {
                      return a.entity_id.component_id < b.entity_id.component_id;
                  }
                  return a.message_id < b.message_id;
              });
    return timeline;
}

Timeline load_tlog(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open telemetry tlog: " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    return load_tlog_bytes(bytes);
}

} // namespace animus::telemetry_core
