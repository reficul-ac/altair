#include "ghost_replay.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>

namespace animus::app
{
namespace
{

constexpr double rad_to_deg = 57.29577951308232;

void update_max_abs(std::optional<double> &target, const std::optional<double> radians)
{
    if (!radians)
    {
        return;
    }
    const double value = std::abs(*radians * rad_to_deg);
    if (!target || value > *target)
    {
        target = value;
    }
}

void update_max(std::optional<double> &target, const std::optional<double> value)
{
    if (value && (!target || *value > *target))
    {
        target = *value;
    }
}

std::optional<double> delta(const std::optional<double> lhs, const std::optional<double> rhs)
{
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }
    return *lhs - *rhs;
}

} // namespace

TelemetryRunSummary summarize_telemetry_run(const animus::telemetry_core::Timeline &timeline,
                                            const animus::telemetry_core::EntityId entity_id,
                                            const PlanVisualizationData *plan,
                                            const double telemetry_gap_warning_s)
{
    TelemetryRunSummary summary;
    const auto *track = timeline.track_for(entity_id);
    if (track == nullptr || track->samples.empty())
    {
        return summary;
    }
    summary.available = true;
    summary.duration_s = std::max(0.0, track->samples.back().time_s - track->samples.front().time_s);
    for (std::size_t index = 0U; index < track->samples.size(); ++index)
    {
        const auto &sample = track->samples[index];
        update_max(summary.max_speed_mps, sample.ground_speed_mps);
        update_max_abs(summary.max_roll_deg, sample.roll_rad);
        update_max_abs(summary.max_pitch_deg, sample.pitch_rad);
        if (index > 0U)
        {
            const auto &prev = track->samples[index - 1U];
            if (sample.fields.position && prev.fields.position)
            {
                summary.distance_m += geo_distance_m(
                    prev.lat_deg, prev.lon_deg, sample.lat_deg, sample.lon_deg);
            }
            if (sample.time_s - prev.time_s >= telemetry_gap_warning_s)
            {
                ++summary.telemetry_gap_count;
            }
        }
    }
    if (plan != nullptr)
    {
        const PlanActualAggregate aggregate = compare_plan_actual(*plan, *track, timeline.end_time_s);
        summary.route_completion_ratio = aggregate.route_completion_ratio;
        summary.max_plan_deviation_m = aggregate.max_cross_track_error_m;
    }
    return summary;
}

GhostReplayComparison compare_ghost_replay(const animus::telemetry_core::Timeline &current,
                                           const animus::telemetry_core::Timeline &baseline,
                                           const animus::telemetry_core::EntityId entity_id,
                                           const PlanVisualizationData *plan,
                                           const double telemetry_gap_warning_s)
{
    GhostReplayComparison comparison;
    comparison.baseline_loaded = true;
    comparison.current = summarize_telemetry_run(current, entity_id, plan, telemetry_gap_warning_s);
    comparison.baseline =
        summarize_telemetry_run(baseline, entity_id, plan, telemetry_gap_warning_s);
    if (!comparison.current.available)
    {
        comparison.diagnostic = "current telemetry has no selected-entity track";
    }
    else if (!comparison.baseline.available)
    {
        comparison.diagnostic = "baseline telemetry has no selected-entity track";
    }
    comparison.duration_delta_s = comparison.current.duration_s - comparison.baseline.duration_s;
    comparison.distance_delta_m = comparison.current.distance_m - comparison.baseline.distance_m;
    comparison.route_completion_delta =
        delta(comparison.current.route_completion_ratio, comparison.baseline.route_completion_ratio);
    comparison.max_speed_delta_mps =
        delta(comparison.current.max_speed_mps, comparison.baseline.max_speed_mps);
    comparison.min_clearance_delta_m =
        delta(comparison.current.min_terrain_clearance_m, comparison.baseline.min_terrain_clearance_m);
    comparison.max_plan_deviation_delta_m =
        delta(comparison.current.max_plan_deviation_m, comparison.baseline.max_plan_deviation_m);
    return comparison;
}

GhostReplayComparison load_and_compare_ghost_replay(
    const animus::telemetry_core::Timeline &current,
    const std::filesystem::path &baseline_path,
    const animus::telemetry_core::TelemetryImportFormat format,
    const animus::telemetry_core::EntityId entity_id,
    const PlanVisualizationData *plan,
    const double telemetry_gap_warning_s)
{
    GhostReplayComparison comparison;
    comparison.baseline_path = baseline_path;
    if (baseline_path.empty() || !std::filesystem::exists(baseline_path))
    {
        comparison.diagnostic = "baseline path does not exist";
        return comparison;
    }
    try
    {
        const animus::telemetry_core::Timeline baseline =
            animus::telemetry_core::load_telemetry(baseline_path, format);
        comparison =
            compare_ghost_replay(current, baseline, entity_id, plan, telemetry_gap_warning_s);
        comparison.baseline_path = baseline_path;
    }
    catch (const std::exception &error)
    {
        comparison.diagnostic = error.what();
    }
    return comparison;
}

} // namespace animus::app
