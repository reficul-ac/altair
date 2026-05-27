#include "timeline_builder.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace animus::telemetry_core
{

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

void finalize_timeline(Timeline &timeline)
{
    timeline.entities.clear();
    timeline.tracks.clear();
    timeline.start_time_s = 0.0;
    timeline.end_time_s = 0.0;

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
                               a.heading_deg == b.heading_deg &&
                               a.altitude_datum == b.altitude_datum;
                    }),
        timeline.samples.end());

    std::map<EntityId, double, decltype(&entity_less)> last_time_by_entity(entity_less);
    for (const TelemetrySample &sample : timeline.samples)
    {
        auto [it, inserted] = last_time_by_entity.emplace(sample.entity_id, sample.time_s);
        if (!inserted && sample.time_s < it->second)
        {
            ++timeline.diagnostics.non_monotonic_timestamps;
        }
        it->second = sample.time_s;
    }

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
}

} // namespace animus::telemetry_core
