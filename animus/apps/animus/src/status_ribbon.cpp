#include "status_ribbon.hpp"

#include "forward_clearance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>

namespace animus::app
{
namespace
{

std::string fmt(const char *format, const double value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

std::string entity_label(const animus::telemetry_core::EntityId id)
{
    return "sys " + std::to_string(static_cast<unsigned>(id.system_id)) + " comp " +
           std::to_string(static_cast<unsigned>(id.component_id));
}

std::string event_summary(const TelemetryPlaybackState &playback)
{
    if (playback.timeline.events.empty())
    {
        return "no events";
    }
    const auto &event = playback.timeline.events.back();
    return fmt("%.1f s", event.time_s) + " " + event.message;
}

std::string screenshot_output_path(const ScreenshotToolState &tool)
{
    const std::string path(tool.png_path.data());
    return path.empty() ? std::string("artifacts/animus/screenshots/manual_screenshot.png") : path;
}

std::string recorder_mp4_path(const Mp4RecorderState &recorder)
{
    const std::string path(recorder.mp4_path.data());
    return path.empty() ? std::string("artifacts/animus/videos/manual_recording.mp4") : path;
}

std::optional<animus::telemetry_core::TelemetrySample>
selected_sample(const TelemetryPlaybackState &playback, const UiState &ui_state)
{
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return std::nullopt;
    }
    return playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
}

bool sample_degraded(const animus::telemetry_core::TelemetrySample &sample)
{
    return !sample.fields.position;
}

bool sample_stale(const TelemetryPlaybackState &playback,
                  const animus::telemetry_core::TelemetrySample &sample,
                  const AppConfigStatusThresholds &thresholds)
{
    if (!playback.live)
    {
        return false;
    }
    const double age_s = std::max(0.0, playback.timeline.end_time_s - sample.time_s);
    return playback.receiver_stats.stale || age_s >= thresholds.telemetry_gap_warning_s;
}

std::uint64_t parser_problem_count(const animus::telemetry_core::ParserDiagnostics &diag)
{
    return diag.unsupported_messages + diag.crc_failures + diag.truncated_frames +
           diag.signed_v2_frames + diag.unsupported_versions + diag.malformed_frames +
           diag.schema_mismatches + diag.unsupported_channels + diag.unsupported_layouts +
           diag.decode_failures + diag.skipped_records + diag.non_monotonic_timestamps +
           diag.missing_required_fields;
}

StatusRibbonPill make_pill(std::string id, std::string label)
{
    StatusRibbonPill pill;
    pill.id = std::move(id);
    pill.label = std::move(label);
    return pill;
}

StatusRibbonLevel worst(StatusRibbonLevel lhs, StatusRibbonLevel rhs)
{
    const auto rank = [](const StatusRibbonLevel level)
    {
        switch (level)
        {
        case StatusRibbonLevel::Warning:
            return 3;
        case StatusRibbonLevel::Caution:
            return 2;
        case StatusRibbonLevel::Unknown:
            return 1;
        case StatusRibbonLevel::Ok:
            return 0;
        }
        return 1;
    };
    return rank(rhs) > rank(lhs) ? rhs : lhs;
}

StatusRibbonPill make_test_pill(const TelemetryPlaybackState &playback, const UiState &ui_state)
{
    StatusRibbonPill pill = make_pill("test_status", "TEST");
    if (!playback.loaded)
    {
        pill.level = StatusRibbonLevel::Unknown;
        pill.summary = "idle";
        pill.action = "Load a log or start live telemetry.";
    }
    else if (!ui_state.telemetry_entity_selected)
    {
        pill.level = StatusRibbonLevel::Caution;
        pill.summary = playback.live ? "live no entity" : "log no entity";
        pill.action = "Select an entity to inspect vehicle state.";
    }
    else
    {
        pill.level = playback.clock.paused() ? StatusRibbonLevel::Caution : StatusRibbonLevel::Ok;
        pill.summary =
            std::string(playback.live ? "live " : "log ") + fmt("%.1f s", playback.clock.time_s());
        pill.action = playback.clock.paused() ? "Resume playback when ready." : "Monitor.";
    }
    pill.details.push_back(playback.live ? "source: live UDP" : "source: offline log");
    pill.details.push_back("range: " + fmt("%.1f..", playback.timeline.start_time_s) +
                           fmt("%.1f s", playback.timeline.end_time_s));
    pill.details.push_back("latest event: " + event_summary(playback));
    return pill;
}

StatusRibbonPill make_link_pill(const RuntimeSignalInputs &runtime,
                                const TelemetryPlaybackState &playback,
                                const AppConfigStatusThresholds &thresholds)
{
    StatusRibbonPill pill = make_pill("link_status", "LINK");
    if (!playback.live)
    {
        pill.level = playback.loaded ? StatusRibbonLevel::Unknown : StatusRibbonLevel::Unknown;
        pill.summary = playback.loaded ? "offline" : "idle";
        pill.action = playback.loaded ? "Live link is not active for this log."
                                      : "Start live telemetry to monitor link health.";
    }
    else if (!playback.receiver_stats.connected || runtime.packet_count == 0U)
    {
        pill.level = StatusRibbonLevel::Warning;
        pill.summary = "no packets";
        pill.action = "Check UDP endpoint and telemetry source.";
    }
    else
    {
        pill.level = StatusRibbonLevel::Ok;
        if (runtime.link_hz && *runtime.link_hz < thresholds.link_hz_warning)
        {
            pill.level = StatusRibbonLevel::Caution;
        }
        if (runtime.telemetry_gap_s &&
            *runtime.telemetry_gap_s >= thresholds.telemetry_gap_critical_s)
        {
            pill.level = StatusRibbonLevel::Warning;
        }
        else if (runtime.telemetry_gap_s &&
                 *runtime.telemetry_gap_s >= thresholds.telemetry_gap_warning_s)
        {
            pill.level = worst(pill.level, StatusRibbonLevel::Caution);
        }
        if (runtime.drop_count > 0U || playback.receiver_stats.receive_errors > 0U)
        {
            pill.level = worst(pill.level, StatusRibbonLevel::Caution);
        }
        pill.summary = runtime.link_hz ? fmt("%.1f Hz", *runtime.link_hz) : "rate unknown";
        pill.action =
            pill.level == StatusRibbonLevel::Ok ? "Monitor." : "Check radio/network load and gaps.";
    }
    pill.details.push_back("packet age: " + (runtime.telemetry_age_s
                                                 ? fmt("%.2f s", *runtime.telemetry_age_s)
                                                 : std::string("n/a")));
    pill.details.push_back("packets: " + std::to_string(runtime.packet_count));
    pill.details.push_back("drops: " + std::to_string(runtime.drop_count));
    pill.details.push_back("parser rejects: " + std::to_string(parser_problem_count(
                                                    playback.live_stats.parser_diagnostics)));
    pill.details.push_back("heartbeat age: n/a");
    return pill;
}

StatusRibbonPill make_vehicle_pill(const TelemetryPlaybackState &playback,
                                   const UiState &ui_state,
                                   const VehicleRuntimeStatus &vehicle_status,
                                   const AppConfigStatusThresholds &thresholds)
{
    StatusRibbonPill pill = make_pill("vehicle_status", "VEHICLE");
    const auto sample = selected_sample(playback, ui_state);
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        pill.level = StatusRibbonLevel::Unknown;
        pill.summary = "none selected";
        pill.action = "Select an entity.";
    }
    else if (!sample)
    {
        pill.level = StatusRibbonLevel::Warning;
        pill.summary = entity_label(playback.selected_entity) + " no sample";
        pill.action = "Jump to available telemetry or select another entity.";
    }
    else
    {
        pill.level = StatusRibbonLevel::Ok;
        if (sample_stale(playback, *sample, thresholds) || sample_degraded(*sample))
        {
            pill.level = StatusRibbonLevel::Caution;
        }
        if (!vehicle_status.model_loaded)
        {
            pill.level = worst(pill.level, StatusRibbonLevel::Caution);
        }
        pill.summary = entity_label(playback.selected_entity);
        pill.action = pill.level == StatusRibbonLevel::Ok
                          ? "Monitor."
                          : "Inspect selected telemetry and visuals.";
    }
    pill.details.push_back("default: " + vehicle_status.default_vehicle_name);
    pill.details.push_back("type: " + vehicle_status.default_vehicle_type);
    pill.details.push_back("visual: " + vehicle_status.model_status);
    pill.details.push_back(std::string("model: ") +
                           (vehicle_status.model_loaded ? "loaded" : "fallback"));
    return pill;
}

StatusRibbonPill make_terrain_pill(const Options &options,
                                   const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                                   const RuntimeSignalInputs &runtime,
                                   const TelemetryPlaybackState &playback,
                                   const AppConfigStatusThresholds &thresholds)
{
    StatusRibbonPill pill = make_pill("terrain_status", "TERRAIN");
    const bool elevation_configured = !options.elevation_geotiff.empty() ||
                                      snapshot.cache_stats.persisted_tiles > 0U ||
                                      snapshot.resident_gpu_tiles > 0U;
    pill.level = elevation_configured ? StatusRibbonLevel::Ok : StatusRibbonLevel::Unknown;
    if (snapshot.failed_tiles > 0U || playback.terrain_height_unavailable)
    {
        pill.level = StatusRibbonLevel::Warning;
    }
    if (playback.unknown_datum_relative_fallback || playback.geoid_correction_unavailable ||
        snapshot.cache_stats.synthesized_tiles > 0U)
    {
        pill.level = worst(pill.level, StatusRibbonLevel::Caution);
    }
    if (runtime.terrain_clearance_m)
    {
        if (*runtime.terrain_clearance_m <= thresholds.terrain_clearance_critical_m)
        {
            pill.level = StatusRibbonLevel::Warning;
        }
        else if (*runtime.terrain_clearance_m <= thresholds.terrain_clearance_warning_m)
        {
            pill.level = worst(pill.level, StatusRibbonLevel::Caution);
        }
        pill.summary = fmt("%.0f m clearance", *runtime.terrain_clearance_m);
    }
    else
    {
        pill.summary = elevation_configured ? "elevation ready" : "terrain unknown";
    }
    const TelemetryPlaybackState::TerrainClearanceStatus current_confidence_status =
        runtime.terrain_clearance_m
            ? terrain_confidence_status(playback.selected_entity_terrain.confidence)
            : TelemetryPlaybackState::TerrainClearanceStatus::Unknown;
    if (current_confidence_status == TelemetryPlaybackState::TerrainClearanceStatus::Warning)
    {
        pill.level = StatusRibbonLevel::Warning;
    }
    else if (current_confidence_status == TelemetryPlaybackState::TerrainClearanceStatus::Caution)
    {
        pill.level = worst(pill.level, StatusRibbonLevel::Caution);
    }
    const TelemetryPlaybackState::TerrainClearanceStatus forward_status =
        worst_forward_clearance_status(playback.selected_entity_terrain.forward_clearance);
    if (forward_status == TelemetryPlaybackState::TerrainClearanceStatus::Warning)
    {
        pill.level = StatusRibbonLevel::Warning;
        if (!runtime.terrain_clearance_m ||
            *runtime.terrain_clearance_m > thresholds.terrain_clearance_critical_m)
        {
            pill.summary = "forward clearance critical";
        }
    }
    else if (forward_status == TelemetryPlaybackState::TerrainClearanceStatus::Caution)
    {
        pill.level = worst(pill.level, StatusRibbonLevel::Caution);
        if (!runtime.terrain_clearance_m ||
            *runtime.terrain_clearance_m > thresholds.terrain_clearance_warning_m)
        {
            pill.summary = "forward clearance low";
        }
    }
    pill.action = pill.level == StatusRibbonLevel::Ok ? "Monitor terrain clearance."
                                                      : "Verify elevation source and datum.";
    pill.details.push_back("resident tiles: " + std::to_string(snapshot.resident_gpu_tiles));
    pill.details.push_back("failed tiles: " + std::to_string(snapshot.failed_tiles));
    pill.details.push_back("synthetic tiles: " +
                           std::to_string(snapshot.cache_stats.synthesized_tiles));
    pill.details.push_back(std::string("datum/geoid: ") +
                           (playback.unknown_datum_relative_fallback ? "datum fallback"
                            : playback.geoid_correction_unavailable  ? "geoid unavailable"
                                                                     : "nominal"));
    pill.details.push_back("confidence: " + std::string(terrain_confidence_label(
                                                playback.selected_entity_terrain.confidence)));
    pill.details.push_back("forward: " +
                           std::string(terrain_clearance_status_label(forward_status)));
    return pill;
}

StatusRibbonPill make_plan_pill(const PlanVisualizationState &plan_state)
{
    StatusRibbonPill pill = make_pill("plan_status", "PLAN");
    if (plan_state.data)
    {
        pill.level = plan_state.error.empty() ? StatusRibbonLevel::Ok : StatusRibbonLevel::Caution;
        pill.summary = "loaded";
        pill.action =
            plan_state.error.empty() ? "Monitor route overlay." : "Review plan diagnostics.";
    }
    else if (!plan_state.error.empty())
    {
        pill.level = StatusRibbonLevel::Warning;
        pill.summary = "load failed";
        pill.action = "Review plan file and diagnostics.";
    }
    else
    {
        pill.level = StatusRibbonLevel::Unknown;
        pill.summary = "not loaded";
        pill.action = "Load a plan when route context is needed.";
    }
    pill.details.push_back("path: " + (plan_state.loaded_path.empty()
                                           ? std::string("none")
                                           : plan_state.loaded_path.string()));
    pill.details.push_back("diagnostics: " + std::to_string(plan_state.diagnostics.size()));
    if (plan_state.data)
    {
        pill.details.push_back("waypoints: " +
                               std::to_string(plan_state.data->mission_waypoints.size()));
        pill.details.push_back("unsupported: " +
                               std::to_string(plan_state.data->unsupported_item_count));
    }
    return pill;
}

StatusRibbonPill make_rec_pill(const ScreenshotToolState &screenshot_tool,
                               const Mp4RecorderState &recorder)
{
    StatusRibbonPill pill = make_pill("rec_status", "REC");
    if (recorder.recording)
    {
        pill.level = StatusRibbonLevel::Caution;
        pill.summary = "recording " + std::to_string(recorder.frame_count);
        pill.action = "Recording is active.";
    }
    else if (recorder.pending_stop)
    {
        pill.level = StatusRibbonLevel::Caution;
        pill.summary = "encoding";
        pill.action = "Wait for MP4 export to finish.";
    }
    else if (screenshot_tool.pending_png)
    {
        pill.level = StatusRibbonLevel::Caution;
        pill.summary = "saving PNG";
        pill.action = "Wait for screenshot write to finish.";
    }
    else
    {
        pill.level = StatusRibbonLevel::Ok;
        pill.summary = "idle";
        pill.action = "Capture idle.";
    }
    pill.details.push_back("PNG: " + screenshot_output_path(screenshot_tool));
    pill.details.push_back("MP4: " + recorder_mp4_path(recorder));
    pill.details.push_back("status: " + recorder.status);
    return pill;
}

StatusRibbonPill make_perf_pill(const RuntimeSignalInputs &runtime,
                                const AppConfigStatusThresholds &thresholds,
                                std::size_t resident_gpu_bytes)
{
    StatusRibbonPill pill = make_pill("perf_status", "PERF");
    pill.level = runtime.frame_time_ms > thresholds.frame_time_warning_ms
                     ? StatusRibbonLevel::Warning
                     : StatusRibbonLevel::Ok;
    if (runtime.upload_bytes_this_frame > 32U * 1024U * 1024U)
    {
        pill.level = worst(pill.level, StatusRibbonLevel::Caution);
    }
    pill.summary = fmt("%.1f ms", runtime.frame_time_ms);
    pill.action =
        pill.level == StatusRibbonLevel::Ok ? "Monitor." : "Reduce layer load or tile churn.";
    pill.details.push_back("upload: " + std::to_string(runtime.upload_bytes_this_frame) + " B");
    pill.details.push_back("resident tiles: " + std::to_string(runtime.resident_tile_count));
    pill.details.push_back(
        "GPU memory: " +
        fmt("%.2f MiB", static_cast<double>(resident_gpu_bytes) / (1024.0 * 1024.0)));
    return pill;
}

StatusRibbonPill make_data_pill(const TelemetryPlaybackState &playback)
{
    const auto &timeline_diag = playback.timeline.diagnostics;
    const auto &live_diag = playback.live_stats.parser_diagnostics;
    const std::uint64_t problems =
        parser_problem_count(timeline_diag) + parser_problem_count(live_diag);
    StatusRibbonPill pill = make_pill("data_status", "DATA");
    if (!playback.loaded)
    {
        pill.level = StatusRibbonLevel::Unknown;
        pill.summary = "no data";
        pill.action = "Load telemetry or start live telemetry.";
    }
    else if (problems > 0U)
    {
        pill.level = StatusRibbonLevel::Warning;
        pill.summary = std::to_string(problems) + " rejects";
        pill.action = "Review parser/import diagnostics in Developer.";
    }
    else
    {
        pill.level = StatusRibbonLevel::Ok;
        pill.summary = std::to_string(playback.timeline.samples.size()) + " samples";
        pill.action = "Monitor.";
    }
    pill.details.push_back("parsed messages: " +
                           std::to_string(playback.live_stats.parsed_messages));
    pill.details.push_back("unsupported: " + std::to_string(timeline_diag.unsupported_messages +
                                                            live_diag.unsupported_messages));
    pill.details.push_back("signed-v2: " + std::to_string(timeline_diag.signed_v2_frames +
                                                          live_diag.signed_v2_frames));
    pill.details.push_back(
        "CRC/truncated/malformed: " +
        std::to_string(timeline_diag.crc_failures + live_diag.crc_failures) + "/" +
        std::to_string(timeline_diag.truncated_frames + live_diag.truncated_frames) + "/" +
        std::to_string(timeline_diag.malformed_frames + live_diag.malformed_frames));
    pill.details.push_back(
        "skipped/import: " +
        std::to_string(timeline_diag.skipped_records + timeline_diag.schema_mismatches +
                       timeline_diag.unsupported_channels + timeline_diag.unsupported_layouts +
                       timeline_diag.decode_failures));
    return pill;
}

} // namespace

const char *status_ribbon_level_label(const StatusRibbonLevel level)
{
    switch (level)
    {
    case StatusRibbonLevel::Ok:
        return "OK";
    case StatusRibbonLevel::Caution:
        return "Caution";
    case StatusRibbonLevel::Warning:
        return "Warning";
    case StatusRibbonLevel::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

StatusRibbonModel
build_status_ribbon_model(const Options &options,
                          const AppConfigStatusThresholds &thresholds,
                          const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                          const RuntimeSignalInputs &runtime,
                          const TelemetryPlaybackState &playback,
                          const UiState &ui_state,
                          const PlanVisualizationState &plan_state,
                          const VehicleRuntimeStatus &vehicle_status,
                          const ScreenshotToolState &screenshot_tool,
                          const Mp4RecorderState &recorder,
                          const std::size_t resident_gpu_bytes)
{
    return {make_test_pill(playback, ui_state),
            make_link_pill(runtime, playback, thresholds),
            make_vehicle_pill(playback, ui_state, vehicle_status, thresholds),
            make_terrain_pill(options, snapshot, runtime, playback, thresholds),
            make_plan_pill(plan_state),
            make_rec_pill(screenshot_tool, recorder),
            make_perf_pill(runtime, thresholds, resident_gpu_bytes),
            make_data_pill(playback)};
}

} // namespace animus::app
