#pragma once

#include "animus/telemetry_core/telemetry.hpp"
#include "plan_visualization.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace animus::app
{

struct TelemetryRunSummary
{
    bool available = false;
    double duration_s = 0.0;
    double distance_m = 0.0;
    std::optional<double> route_completion_ratio;
    std::optional<double> max_speed_mps;
    std::optional<double> min_terrain_clearance_m;
    std::optional<double> max_roll_deg;
    std::optional<double> max_pitch_deg;
    std::size_t telemetry_gap_count = 0U;
    std::optional<double> max_plan_deviation_m;
};

struct GhostReplayComparison
{
    bool baseline_loaded = false;
    std::filesystem::path baseline_path;
    std::string diagnostic;
    TelemetryRunSummary current;
    TelemetryRunSummary baseline;
    double duration_delta_s = 0.0;
    double distance_delta_m = 0.0;
    std::optional<double> route_completion_delta;
    std::optional<double> max_speed_delta_mps;
    std::optional<double> min_clearance_delta_m;
    std::optional<double> max_plan_deviation_delta_m;
};

[[nodiscard]] TelemetryRunSummary summarize_telemetry_run(
    const animus::telemetry_core::Timeline &timeline,
    animus::telemetry_core::EntityId entity_id,
    const PlanVisualizationData *plan = nullptr,
    double telemetry_gap_warning_s = 2.0);

[[nodiscard]] GhostReplayComparison compare_ghost_replay(
    const animus::telemetry_core::Timeline &current,
    const animus::telemetry_core::Timeline &baseline,
    animus::telemetry_core::EntityId entity_id,
    const PlanVisualizationData *plan = nullptr,
    double telemetry_gap_warning_s = 2.0);

[[nodiscard]] GhostReplayComparison load_and_compare_ghost_replay(
    const animus::telemetry_core::Timeline &current,
    const std::filesystem::path &baseline_path,
    animus::telemetry_core::TelemetryImportFormat format,
    animus::telemetry_core::EntityId entity_id,
    const PlanVisualizationData *plan = nullptr,
    double telemetry_gap_warning_s = 2.0);

} // namespace animus::app
