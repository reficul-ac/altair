#include "selected_vehicle_card.hpp"

#include "forward_clearance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace animus::app
{
namespace
{

constexpr double rad_to_deg = 57.29577951308232;

std::string fmt(const char *format, const double value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

std::string placeholder(const std::optional<double> value, const char *format)
{
    return value ? fmt(format, *value) : std::string("--");
}

std::string format_age(const double age_s)
{
    if (age_s < 1.0)
    {
        return fmt("%.0f ms", age_s * 1000.0);
    }
    return fmt("%.1f s", age_s);
}

std::string entity_label(const animus::telemetry_core::EntityId id)
{
    return "sys " + std::to_string(static_cast<unsigned>(id.system_id)) + " comp " +
           std::to_string(static_cast<unsigned>(id.component_id));
}

double sample_age_s(const TelemetryPlaybackState &playback,
                    const animus::telemetry_core::TelemetrySample &sample)
{
    if (playback.timeline.end_time_s <= 0.0)
    {
        return 0.0;
    }
    return std::max(0.0, playback.timeline.end_time_s - sample.time_s);
}

bool sample_degraded(const animus::telemetry_core::TelemetrySample &sample)
{
    return !sample.fields.position;
}

SelectedVehicleCardStatus worst(const SelectedVehicleCardStatus lhs,
                                const SelectedVehicleCardStatus rhs)
{
    const auto rank = [](const SelectedVehicleCardStatus status)
    {
        switch (status)
        {
        case SelectedVehicleCardStatus::Warning:
            return 3;
        case SelectedVehicleCardStatus::Caution:
            return 2;
        case SelectedVehicleCardStatus::Unknown:
            return 1;
        case SelectedVehicleCardStatus::Ok:
            return 0;
        }
        return 1;
    };
    return rank(rhs) > rank(lhs) ? rhs : lhs;
}

void add_warning(SelectedVehicleCardModel &model,
                 const SelectedVehicleCardStatus status,
                 std::string warning)
{
    model.status = worst(model.status, status);
    model.warnings.push_back(std::move(warning));
}

void add_attitude_warning(SelectedVehicleCardModel &model,
                          const char *name,
                          const std::optional<double> radians,
                          const double threshold_deg)
{
    if (!radians)
    {
        return;
    }
    const double degrees = std::abs(*radians * rad_to_deg);
    if (degrees >= threshold_deg * 1.5)
    {
        add_warning(model, SelectedVehicleCardStatus::Warning, std::string(name) + " high");
    }
    else if (degrees >= threshold_deg)
    {
        add_warning(model, SelectedVehicleCardStatus::Caution, std::string(name) + " high");
    }
}

std::optional<double> link_hz(const TelemetryPlaybackState &playback)
{
    if (!playback.live || playback.timeline.end_time_s <= playback.timeline.start_time_s)
    {
        return std::nullopt;
    }
    return static_cast<double>(playback.live_stats.parsed_messages) /
           (playback.timeline.end_time_s - playback.timeline.start_time_s);
}

std::vector<SelectedVehicleCardMetric> default_position_metrics()
{
    return {
        {"Alt MSL", "--"},
        {"Alt Rel", "--"},
        {"Terrain", "--"},
        {"Clearance", "--"},
    };
}

std::vector<SelectedVehicleCardMetric> default_motion_metrics()
{
    return {
        {"Ground", "--"},
        {"Climb", "--"},
        {"Heading", "--"},
        {"Roll", "--"},
        {"Pitch", "--"},
        {"Yaw", "--"},
    };
}

std::string forward_clearance_summary(
    const std::vector<TelemetryPlaybackState::ForwardClearanceSample> &samples)
{
    if (samples.empty())
    {
        return "unavailable";
    }
    const auto worst_sample =
        std::min_element(samples.begin(),
                         samples.end(),
                         [](const TelemetryPlaybackState::ForwardClearanceSample &lhs,
                            const TelemetryPlaybackState::ForwardClearanceSample &rhs)
                         {
                             if (!lhs.terrain_clearance_m)
                             {
                                 return false;
                             }
                             if (!rhs.terrain_clearance_m)
                             {
                                 return true;
                             }
                             return *lhs.terrain_clearance_m < *rhs.terrain_clearance_m;
                         });
    if (worst_sample == samples.end() || !worst_sample->terrain_clearance_m)
    {
        return "terrain unavailable";
    }
    return fmt("%.0f m", *worst_sample->terrain_clearance_m) + " at " +
           fmt("%.0f s", worst_sample->horizon_s) + " / " +
           terrain_clearance_status_label(worst_forward_clearance_status(samples));
}

std::string target_summary(const SelectedVehicleTestMetadata &metadata)
{
    std::vector<std::string> parts;
    if (!metadata.target_speed.empty())
    {
        parts.push_back("speed " + metadata.target_speed);
    }
    if (!metadata.target_altitude.empty())
    {
        parts.push_back("alt " + metadata.target_altitude);
    }
    if (!metadata.target_heading.empty())
    {
        parts.push_back("hdg " + metadata.target_heading);
    }
    if (parts.empty())
    {
        return "--";
    }
    std::string text = parts.front();
    for (std::size_t index = 1U; index < parts.size(); ++index)
    {
        text += ", " + parts[index];
    }
    return text;
}

} // namespace

const char *selected_vehicle_card_status_label(const SelectedVehicleCardStatus status)
{
    switch (status)
    {
    case SelectedVehicleCardStatus::Ok:
        return "OK";
    case SelectedVehicleCardStatus::Caution:
        return "Caution";
    case SelectedVehicleCardStatus::Warning:
        return "Warning";
    case SelectedVehicleCardStatus::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

SelectedVehicleCardModel
build_selected_vehicle_card_model(const TelemetryPlaybackState &playback,
                                  const UiState &ui_state,
                                  const VehicleRuntimeStatus &vehicle_status,
                                  const AppConfigStatusThresholds &thresholds)
{
    SelectedVehicleCardModel model;
    model.mode = playback.loaded ? (playback.live ? "live UDP" : "offline playback") : "--";
    model.detected_type = vehicle_status.selected_detected_type;
    model.visual_assignment =
        vehicle_status.selected_vehicle_name + " / " + vehicle_status.selected_vehicle_type;
    model.visual_status =
        vehicle_status.selected_model_status.empty() ? "--" : vehicle_status.selected_model_status;
    model.visual_fallback = vehicle_status.selected_fallback_reason.empty()
                                ? "--"
                                : vehicle_status.selected_fallback_reason;
    model.heading_source = vehicle_status.selected_heading_source;
    model.altitude_placement = vehicle_status.selected_altitude_placement;
    model.visual_scale = vehicle_status.selected_scale;
    model.force_icon_only = vehicle_status.selected_force_icon_only;
    model.test = ui_state.selected_vehicle_test.test_name.empty()
                     ? "--"
                     : ui_state.selected_vehicle_test.test_name;
    model.phase =
        ui_state.selected_vehicle_test.phase.empty() ? "--" : ui_state.selected_vehicle_test.phase;
    model.target = target_summary(ui_state.selected_vehicle_test);
    model.terrain_confidence =
        terrain_confidence_label(playback.selected_entity_terrain.confidence);
    model.forward_clearance_summary =
        forward_clearance_summary(playback.selected_entity_terrain.forward_clearance);
    model.position_metrics = default_position_metrics();
    model.motion_metrics = default_motion_metrics();

    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        model.warnings.push_back("No selected entity");
        return model;
    }

    model.entity_label = entity_label(playback.selected_entity);
    const auto sample =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!sample)
    {
        model.telemetry_state = "no current sample";
        model.warnings.push_back("No current sample for selected entity");
        return model;
    }

    model.status = SelectedVehicleCardStatus::Ok;
    model.telemetry_state = sample_degraded(*sample) ? "degraded" : "valid";
    const double age_s = sample_age_s(playback, *sample);
    model.telemetry_age = format_age(age_s);

    model.position_metrics = {
        {"Alt MSL", placeholder(sample->altitude_msl_m, "%.0f m")},
        {"Alt Rel", placeholder(sample->altitude_relative_m, "%.0f m")},
        {"Terrain", placeholder(playback.selected_entity_terrain.terrain_elevation_m, "%.0f m")},
        {"Clearance", placeholder(playback.selected_entity_terrain.terrain_clearance_m, "%.0f m")},
    };
    model.motion_metrics = {
        {"Ground", placeholder(sample->ground_speed_mps, "%.1f m/s")},
        {"Climb", placeholder(sample->climb_rate_mps, "%.1f m/s")},
        {"Heading", placeholder(sample->heading_deg, "%.1f deg")},
        {"Roll",
         placeholder(sample->roll_rad ? std::optional<double>(*sample->roll_rad * rad_to_deg)
                                      : std::nullopt,
                     "%.1f deg")},
        {"Pitch",
         placeholder(sample->pitch_rad ? std::optional<double>(*sample->pitch_rad * rad_to_deg)
                                       : std::nullopt,
                     "%.1f deg")},
        {"Yaw",
         placeholder(sample->yaw_rad ? std::optional<double>(*sample->yaw_rad * rad_to_deg)
                                     : std::nullopt,
                     "%.1f deg")},
    };

    if (playback.live &&
        (playback.receiver_stats.stale || age_s >= thresholds.telemetry_gap_critical_s))
    {
        add_warning(model, SelectedVehicleCardStatus::Warning, "Telemetry is stale");
    }
    else if (playback.live && age_s >= thresholds.telemetry_gap_warning_s)
    {
        add_warning(model, SelectedVehicleCardStatus::Caution, "Telemetry age is elevated");
    }
    if (sample_degraded(*sample))
    {
        add_warning(model, SelectedVehicleCardStatus::Caution, "Telemetry position is degraded");
    }

    if (playback.selected_entity_terrain.terrain_clearance_m)
    {
        const double clearance = *playback.selected_entity_terrain.terrain_clearance_m;
        if (clearance <= thresholds.terrain_clearance_critical_m)
        {
            add_warning(model, SelectedVehicleCardStatus::Warning, "Terrain clearance critical");
        }
        else if (clearance <= thresholds.terrain_clearance_warning_m)
        {
            add_warning(model, SelectedVehicleCardStatus::Caution, "Terrain clearance low");
        }
    }

    const TelemetryPlaybackState::TerrainClearanceStatus forward_status =
        worst_forward_clearance_status(playback.selected_entity_terrain.forward_clearance);
    if (forward_status == TelemetryPlaybackState::TerrainClearanceStatus::Warning)
    {
        add_warning(
            model, SelectedVehicleCardStatus::Warning, "Forward terrain clearance critical");
    }
    else if (forward_status == TelemetryPlaybackState::TerrainClearanceStatus::Caution)
    {
        add_warning(model, SelectedVehicleCardStatus::Caution, "Forward terrain clearance low");
    }

    switch (playback.selected_entity_terrain.confidence)
    {
    case TelemetryPlaybackState::TerrainConfidence::FallbackResidentTile:
        add_warning(model, SelectedVehicleCardStatus::Caution, "Terrain uses fallback tile");
        break;
    case TelemetryPlaybackState::TerrainConfidence::SyntheticResidentTile:
        add_warning(model, SelectedVehicleCardStatus::Caution, "Terrain is synthetic");
        break;
    case TelemetryPlaybackState::TerrainConfidence::DatumUncertain:
        add_warning(model, SelectedVehicleCardStatus::Warning, "Altitude datum is uncertain");
        break;
    case TelemetryPlaybackState::TerrainConfidence::ExactResidentTile:
    case TelemetryPlaybackState::TerrainConfidence::Unavailable:
        break;
    }

    if (!vehicle_status.selected_model_loaded)
    {
        add_warning(model, SelectedVehicleCardStatus::Caution, "Vehicle model fallback is active");
    }

    add_attitude_warning(model, "Roll", sample->roll_rad, thresholds.roll_warning_deg);
    add_attitude_warning(model, "Pitch", sample->pitch_rad, thresholds.pitch_warning_deg);

    if (const auto hz = link_hz(playback))
    {
        if (*hz < thresholds.link_hz_warning)
        {
            add_warning(model, SelectedVehicleCardStatus::Caution, "Link rate is low");
        }
    }
    else if (playback.live &&
             (!playback.receiver_stats.connected || playback.receiver_stats.datagrams == 0U))
    {
        add_warning(model, SelectedVehicleCardStatus::Warning, "Live link has no packets");
    }

    model.status_label = selected_vehicle_card_status_label(model.status);
    return model;
}

} // namespace animus::app
