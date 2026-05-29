#include "status_ribbon.hpp"

#include "forward_clearance.hpp"

#include <algorithm>
#include <span>

#include <gtest/gtest.h>

namespace
{

using animus::app::RuntimeSignalInputs;
using animus::app::StatusRibbonLevel;
using animus::app::StatusRibbonModel;
using animus::app::TelemetryPlaybackState;
using animus::telemetry_core::Entity;
using animus::telemetry_core::EntityId;
using animus::telemetry_core::TelemetrySample;
using animus::telemetry_core::Track;

const animus::app::StatusRibbonPill &pill(const StatusRibbonModel &model, const char *label)
{
    const auto it = std::find_if(model.begin(),
                                 model.end(),
                                 [label](const animus::app::StatusRibbonPill &value)
                                 { return value.label == label; });
    EXPECT_NE(it, model.end());
    return *it;
}

TelemetrySample sample(EntityId id, double time_s, bool position = true)
{
    TelemetrySample value;
    value.entity_id = id;
    value.time_s = time_s;
    value.lat_deg = 39.0;
    value.lon_deg = -120.0;
    value.fields.position = position;
    return value;
}

TelemetryPlaybackState loaded_playback()
{
    constexpr EntityId id{1, 1};
    TelemetryPlaybackState playback;
    playback.loaded = true;
    playback.selected_entity = id;
    playback.timeline.start_time_s = 0.0;
    playback.timeline.end_time_s = 10.0;
    playback.timeline.entities.push_back(Entity{id, sample(id, 10.0)});
    playback.timeline.tracks.push_back(Track{id, {sample(id, 0.0), sample(id, 10.0)}});
    playback.timeline.samples.push_back(sample(id, 0.0));
    playback.timeline.samples.push_back(sample(id, 10.0));
    playback.clock.set_range(0.0, 10.0);
    playback.clock.seek(10.0);
    return playback;
}

StatusRibbonModel build(const animus::app::Options &options,
                        const animus::app::AppConfigStatusThresholds &thresholds,
                        const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                        const RuntimeSignalInputs &runtime,
                        const TelemetryPlaybackState &playback,
                        const animus::app::UiState &ui = {})
{
    return animus::app::build_status_ribbon_model(options,
                                                  thresholds,
                                                  snapshot,
                                                  runtime,
                                                  playback,
                                                  ui,
                                                  animus::app::PlanVisualizationState{},
                                                  animus::app::VehicleRuntimeStatus{},
                                                  animus::app::ScreenshotToolState{},
                                                  animus::app::Mp4RecorderState{},
                                                  0U);
}

TelemetryPlaybackState::ForwardClearanceSample
forward_sample(const double clearance_m,
               const TelemetryPlaybackState::TerrainConfidence confidence =
                   TelemetryPlaybackState::TerrainConfidence::ExactResidentTile)
{
    TelemetryPlaybackState::ForwardClearanceSample value;
    value.horizon_s = 10.0;
    value.lat_deg = 39.0;
    value.lon_deg = -120.0;
    value.terrain_elevation_m = 1380.0;
    value.terrain_clearance_m = clearance_m;
    value.confidence = confidence;
    value.status = animus::app::terrain_clearance_status(clearance_m, confidence, {});
    return value;
}

} // namespace

TEST(AnimusStatusRibbon, LinkStatesFromRateGapAndDrops)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    playback.live = true;
    playback.receiver_stats.connected = true;
    playback.live_stats.parsed_messages = 100;
    runtime.packet_count = 100;
    runtime.link_hz = 10.0;
    runtime.telemetry_age_s = 0.1;
    runtime.telemetry_gap_s = 0.1;

    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "LINK").level,
              StatusRibbonLevel::Ok);

    runtime.link_hz = 1.0;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "LINK").level,
              StatusRibbonLevel::Caution);

    runtime.telemetry_gap_s = 6.0;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "LINK").level,
              StatusRibbonLevel::Warning);

    playback.live = false;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "LINK").level,
              StatusRibbonLevel::Unknown);
}

TEST(AnimusStatusRibbon, LinkDetailsExposeHeartbeatAge)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    playback.live = true;
    playback.receiver_stats.connected = true;
    runtime.packet_count = 10;
    runtime.link_hz = 10.0;
    runtime.telemetry_gap_s = 0.1;
    animus::telemetry_core::MavlinkMessage heartbeat;
    heartbeat.entity_id = playback.selected_entity;
    heartbeat.message_id = 0U;
    playback.mavlink_values.ingest_messages(std::span<const animus::telemetry_core::MavlinkMessage>(
                                                &heartbeat, static_cast<std::size_t>(1U)),
                                            9.0);

    const auto link = pill(build(options, thresholds, snapshot, runtime, playback), "LINK");

    EXPECT_EQ(link.level, StatusRibbonLevel::Ok);
    EXPECT_TRUE(std::any_of(link.details.begin(),
                            link.details.end(),
                            [](const std::string &detail)
                            { return detail.find("heartbeat age: 1.00 s") != std::string::npos; }));
}

TEST(AnimusStatusRibbon, TerrainStatesFromClearanceConfidenceAndTiles)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    snapshot.resident_gpu_tiles = 4U;
    runtime.terrain_clearance_m = 80.0;

    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN").level,
              StatusRibbonLevel::Ok);

    runtime.terrain_clearance_m = 20.0;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN").level,
              StatusRibbonLevel::Caution);

    runtime.terrain_clearance_m = 5.0;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN").level,
              StatusRibbonLevel::Warning);

    runtime.terrain_clearance_m = 80.0;
    snapshot.failed_tiles = 1U;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN").level,
              StatusRibbonLevel::Warning);
}

TEST(AnimusStatusRibbon, TerrainEscalatesFromForwardClearance)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    snapshot.resident_gpu_tiles = 4U;
    runtime.terrain_clearance_m = 80.0;

    playback.selected_entity_terrain.forward_clearance = {forward_sample(20.0)};
    auto terrain = pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN");
    EXPECT_EQ(terrain.level, StatusRibbonLevel::Caution);
    EXPECT_EQ(terrain.summary, "forward clearance low");

    playback.selected_entity_terrain.forward_clearance = {forward_sample(5.0)};
    terrain = pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN");
    EXPECT_EQ(terrain.level, StatusRibbonLevel::Warning);
    EXPECT_EQ(terrain.summary, "forward clearance critical");
}

TEST(AnimusStatusRibbon, CurrentCriticalClearanceKeepsPrecedenceOverForward)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    snapshot.resident_gpu_tiles = 4U;
    runtime.terrain_clearance_m = 5.0;
    playback.selected_entity_terrain.forward_clearance = {forward_sample(20.0)};

    const auto terrain = pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN");

    EXPECT_EQ(terrain.level, StatusRibbonLevel::Warning);
    EXPECT_EQ(terrain.summary, "5 m clearance");
}

TEST(AnimusStatusRibbon, UnavailableForwardProjectionDoesNotWarn)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    snapshot.resident_gpu_tiles = 4U;
    runtime.terrain_clearance_m = 80.0;
    TelemetryPlaybackState::ForwardClearanceSample unavailable;
    unavailable.horizon_s = 10.0;
    playback.selected_entity_terrain.forward_clearance = {unavailable};

    const auto terrain = pill(build(options, thresholds, snapshot, runtime, playback), "TERRAIN");

    EXPECT_EQ(terrain.level, StatusRibbonLevel::Ok);
    EXPECT_EQ(terrain.summary, "80 m clearance");
}

TEST(AnimusStatusRibbon, VehicleReflectsSelectionStaleDataAndModelFallback)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    animus::app::UiState ui;
    ui.telemetry_entity_selected = true;
    animus::app::VehicleRuntimeStatus vehicle;
    vehicle.model_loaded = true;
    vehicle.model_status = "loaded";

    auto model = animus::app::build_status_ribbon_model(options,
                                                        thresholds,
                                                        snapshot,
                                                        runtime,
                                                        playback,
                                                        ui,
                                                        animus::app::PlanVisualizationState{},
                                                        vehicle,
                                                        animus::app::ScreenshotToolState{},
                                                        animus::app::Mp4RecorderState{},
                                                        0U);
    EXPECT_EQ(pill(model, "VEHICLE").level, StatusRibbonLevel::Ok);

    vehicle.model_loaded = false;
    model = animus::app::build_status_ribbon_model(options,
                                                   thresholds,
                                                   snapshot,
                                                   runtime,
                                                   playback,
                                                   ui,
                                                   animus::app::PlanVisualizationState{},
                                                   vehicle,
                                                   animus::app::ScreenshotToolState{},
                                                   animus::app::Mp4RecorderState{},
                                                   0U);
    EXPECT_EQ(pill(model, "VEHICLE").level, StatusRibbonLevel::Caution);

    playback.live = true;
    playback.receiver_stats.stale = true;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback, ui), "VEHICLE").level,
              StatusRibbonLevel::Caution);
}

TEST(AnimusStatusRibbon, PerfWarnsOnFrameTimeAndUploadPressure)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();
    runtime.frame_time_ms = 12.0;

    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "PERF").level,
              StatusRibbonLevel::Ok);

    runtime.frame_time_ms = 40.0;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "PERF").level,
              StatusRibbonLevel::Warning);

    runtime.frame_time_ms = 12.0;
    runtime.upload_bytes_this_frame = 40U * 1024U * 1024U;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "PERF").level,
              StatusRibbonLevel::Caution);
}

TEST(AnimusStatusRibbon, DataWarnsFromParserAndImportDiagnostics)
{
    animus::app::Options options;
    animus::app::AppConfigStatusThresholds thresholds;
    animus::terrain_core::TerrainStreamSnapshot snapshot;
    RuntimeSignalInputs runtime;
    TelemetryPlaybackState playback = loaded_playback();

    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "DATA").level,
              StatusRibbonLevel::Ok);

    playback.timeline.diagnostics.crc_failures = 1U;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "DATA").level,
              StatusRibbonLevel::Warning);

    playback.timeline.diagnostics = {};
    playback.live_stats.parser_diagnostics.signed_v2_frames = 1U;
    EXPECT_EQ(pill(build(options, thresholds, snapshot, runtime, playback), "DATA").level,
              StatusRibbonLevel::Warning);
}
