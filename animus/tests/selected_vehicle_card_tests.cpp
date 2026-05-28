#include "selected_vehicle_card.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <gtest/gtest.h>

namespace
{

using animus::app::SelectedVehicleCardStatus;
using animus::app::TelemetryPlaybackState;
using animus::telemetry_core::Entity;
using animus::telemetry_core::EntityId;
using animus::telemetry_core::TelemetrySample;
using animus::telemetry_core::Track;

constexpr EntityId entity_id{7, 1};
constexpr double deg_to_rad = 0.017453292519943295;

TelemetrySample sample(const double time_s)
{
    TelemetrySample value;
    value.time_s = time_s;
    value.entity_id = entity_id;
    value.lat_deg = 39.0;
    value.lon_deg = -120.0;
    value.altitude_msl_m = 1500.0;
    value.altitude_relative_m = 120.0;
    value.ground_speed_mps = 22.5;
    value.climb_rate_mps = 1.2;
    value.heading_deg = 91.0;
    value.roll_rad = 10.0 * deg_to_rad;
    value.pitch_rad = -5.0 * deg_to_rad;
    value.yaw_rad = 90.0 * deg_to_rad;
    value.fields.position = true;
    value.fields.altitude_msl = true;
    value.fields.altitude_relative = true;
    value.fields.velocity = true;
    value.fields.heading = true;
    value.fields.attitude = true;
    return value;
}

TelemetryPlaybackState playback_with_sample(TelemetrySample value = sample(10.0))
{
    TelemetryPlaybackState playback;
    playback.loaded = true;
    playback.selected_entity = entity_id;
    playback.timeline.start_time_s = 0.0;
    playback.timeline.end_time_s = 10.0;
    playback.timeline.entities.push_back(Entity{entity_id, value});
    playback.timeline.tracks.push_back(Track{entity_id, {value}});
    playback.timeline.samples.push_back(value);
    playback.clock.set_range(0.0, 10.0);
    playback.clock.seek(10.0);
    playback.selected_entity_terrain.terrain_elevation_m = 1380.0;
    playback.selected_entity_terrain.terrain_clearance_m = 120.0;
    playback.selected_entity_terrain.confidence =
        TelemetryPlaybackState::TerrainConfidence::ExactResidentTile;
    return playback;
}

animus::app::UiState selected_ui()
{
    animus::app::UiState ui;
    ui.telemetry_entity_selected = true;
    return ui;
}

animus::app::VehicleRuntimeStatus loaded_vehicle()
{
    animus::app::VehicleRuntimeStatus vehicle;
    vehicle.default_vehicle_name = "Generic RC Plane";
    vehicle.default_vehicle_type = "fixed_wing";
    vehicle.model_loaded = true;
    vehicle.model_status = "loaded";
    return vehicle;
}

animus::app::SelectedVehicleCardModel
build(const TelemetryPlaybackState &playback,
      const animus::app::UiState &ui,
      const animus::app::VehicleRuntimeStatus &vehicle = loaded_vehicle(),
      const animus::app::AppConfigStatusThresholds &thresholds = {})
{
    return animus::app::build_selected_vehicle_card_model(playback, ui, vehicle, thresholds);
}

bool has_warning(const animus::app::SelectedVehicleCardModel &model, const std::string &needle)
{
    return std::any_of(model.warnings.begin(),
                       model.warnings.end(),
                       [&needle](const std::string &warning)
                       { return warning.find(needle) != std::string::npos; });
}

std::string metric_value(const std::vector<animus::app::SelectedVehicleCardMetric> &metrics,
                         const std::string &label)
{
    const auto it = std::find_if(metrics.begin(),
                                 metrics.end(),
                                 [&label](const animus::app::SelectedVehicleCardMetric &metric)
                                 { return metric.label == label; });
    EXPECT_NE(it, metrics.end());
    return it == metrics.end() ? std::string{} : it->value;
}

} // namespace

TEST(SelectedVehicleCard, NoSelectedEntityIsUnknownWithPlaceholders)
{
    const TelemetryPlaybackState playback;
    const animus::app::UiState ui;
    const auto model = build(playback, ui);

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Unknown);
    EXPECT_EQ(model.entity_label, "--");
    EXPECT_EQ(model.telemetry_age, "--");
    EXPECT_EQ(model.test, "--");
    EXPECT_EQ(model.phase, "--");
    EXPECT_EQ(model.target, "--");
    EXPECT_EQ(metric_value(model.position_metrics, "Alt MSL"), "--");
    EXPECT_EQ(metric_value(model.motion_metrics, "Ground"), "--");
    EXPECT_EQ(metric_value(model.motion_metrics, "Roll"), "--");
}

TEST(SelectedVehicleCard, ValidSelectedEntityFormatsTelemetry)
{
    const auto model = build(playback_with_sample(), selected_ui());

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Ok);
    EXPECT_EQ(model.status_label, "OK");
    EXPECT_EQ(model.entity_label, "sys 7 comp 1");
    EXPECT_EQ(model.mode, "offline playback");
    EXPECT_EQ(model.telemetry_state, "valid");
    EXPECT_EQ(model.telemetry_age, "0 ms");
    EXPECT_EQ(model.visual_assignment, "Generic RC Plane / fixed_wing");
    EXPECT_EQ(model.visual_status, "loaded");
    EXPECT_EQ(model.terrain_confidence, "exact resident tile");
    EXPECT_EQ(metric_value(model.position_metrics, "Alt MSL"), "1500 m");
    EXPECT_EQ(metric_value(model.position_metrics, "Clearance"), "120 m");
    EXPECT_EQ(metric_value(model.motion_metrics, "Ground"), "22.5 m/s");
    EXPECT_EQ(metric_value(model.motion_metrics, "Roll"), "10.0 deg");
    EXPECT_EQ(metric_value(model.motion_metrics, "Pitch"), "-5.0 deg");
    EXPECT_EQ(metric_value(model.motion_metrics, "Yaw"), "90.0 deg");
}

TEST(SelectedVehicleCard, MissingOptionalTelemetryUsesDashPlaceholders)
{
    TelemetrySample value = sample(10.0);
    value.altitude_msl_m.reset();
    value.ground_speed_mps.reset();
    value.roll_rad.reset();
    const auto model = build(playback_with_sample(value), selected_ui());

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Ok);
    EXPECT_EQ(metric_value(model.position_metrics, "Alt MSL"), "--");
    EXPECT_EQ(metric_value(model.motion_metrics, "Ground"), "--");
    EXPECT_EQ(metric_value(model.motion_metrics, "Roll"), "--");
}

TEST(SelectedVehicleCard, StaleAndDegradedSamplesRaiseStatus)
{
    TelemetrySample value = sample(3.0);
    value.fields.position = false;
    TelemetryPlaybackState playback = playback_with_sample(value);
    playback.live = true;
    playback.receiver_stats.connected = true;

    const auto model = build(playback, selected_ui());

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Warning);
    EXPECT_TRUE(has_warning(model, "Telemetry is stale"));
    EXPECT_TRUE(has_warning(model, "degraded"));
}

TEST(SelectedVehicleCard, LowTerrainClearanceUsesConfiguredThresholds)
{
    TelemetryPlaybackState playback = playback_with_sample();
    playback.selected_entity_terrain.terrain_clearance_m = 20.0;
    EXPECT_EQ(build(playback, selected_ui()).status, SelectedVehicleCardStatus::Caution);

    playback.selected_entity_terrain.terrain_clearance_m = 5.0;
    const auto model = build(playback, selected_ui());
    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Warning);
    EXPECT_TRUE(has_warning(model, "critical"));
}

TEST(SelectedVehicleCard, TerrainConfidenceWarningsReflectFallbacks)
{
    TelemetryPlaybackState playback = playback_with_sample();
    playback.selected_entity_terrain.confidence =
        TelemetryPlaybackState::TerrainConfidence::FallbackResidentTile;
    EXPECT_EQ(build(playback, selected_ui()).status, SelectedVehicleCardStatus::Caution);

    playback.selected_entity_terrain.confidence =
        TelemetryPlaybackState::TerrainConfidence::SyntheticResidentTile;
    EXPECT_TRUE(has_warning(build(playback, selected_ui()), "synthetic"));

    playback.selected_entity_terrain.confidence =
        TelemetryPlaybackState::TerrainConfidence::DatumUncertain;
    const auto model = build(playback, selected_ui());
    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Warning);
    EXPECT_TRUE(has_warning(model, "datum"));
}

TEST(SelectedVehicleCard, ModelFallbackRaisesCaution)
{
    animus::app::VehicleRuntimeStatus vehicle = loaded_vehicle();
    vehicle.model_loaded = false;
    vehicle.model_status = "fallback icon";

    const auto model = build(playback_with_sample(), selected_ui(), vehicle);

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Caution);
    EXPECT_TRUE(has_warning(model, "model fallback"));
    EXPECT_EQ(model.visual_status, "fallback icon");
}

TEST(SelectedVehicleCard, HighAttitudeUsesConfiguredThresholds)
{
    TelemetrySample value = sample(10.0);
    value.roll_rad = 50.0 * deg_to_rad;
    value.pitch_rad = 50.0 * deg_to_rad;
    animus::app::AppConfigStatusThresholds thresholds;
    thresholds.roll_warning_deg = 45.0;
    thresholds.pitch_warning_deg = 30.0;

    const auto model =
        build(playback_with_sample(value), selected_ui(), loaded_vehicle(), thresholds);

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Warning);
    EXPECT_TRUE(has_warning(model, "Roll"));
    EXPECT_TRUE(has_warning(model, "Pitch"));
}

TEST(SelectedVehicleCard, LowLinkRateRaisesCaution)
{
    TelemetryPlaybackState playback = playback_with_sample();
    playback.live = true;
    playback.receiver_stats.connected = true;
    playback.receiver_stats.stale = false;
    playback.receiver_stats.datagrams = 20U;
    playback.live_stats.parsed_messages = 10U;
    playback.timeline.start_time_s = 0.0;
    playback.timeline.end_time_s = 10.0;

    const auto model = build(playback, selected_ui());

    EXPECT_EQ(model.status, SelectedVehicleCardStatus::Caution);
    EXPECT_TRUE(has_warning(model, "Link rate"));
}
