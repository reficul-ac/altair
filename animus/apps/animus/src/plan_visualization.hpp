#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace animus::app
{

struct PlanGeoPoint
{
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    std::optional<double> alt_m;
};

struct PlanWaypoint
{
    PlanGeoPoint point;
    std::string label;
};

struct PlanPolyline
{
    std::vector<PlanGeoPoint> points;
    std::string label;
};

struct PlanCircle
{
    PlanGeoPoint center;
    double radius_m = 0.0;
};

struct PlanVisualizationData
{
    std::vector<PlanWaypoint> mission_waypoints;
    std::vector<PlanPolyline> complex_outlines;
    std::vector<PlanPolyline> geofence_polygons;
    std::vector<PlanCircle> geofence_circles;
    std::vector<PlanWaypoint> rally_points;
    double route_distance_m = 0.0;
    std::size_t unsupported_item_count = 0;
};

struct PlanVisualizationLoadResult
{
    std::optional<PlanVisualizationData> data;
    std::vector<std::string> diagnostics;
    std::string error;
};

struct PlanTrackComparison
{
    double planned_route_m = 0.0;
    double selected_track_m = 0.0;
    std::optional<double> first_waypoint_nearest_track_m;
    std::optional<double> last_waypoint_nearest_track_m;
};

struct PlanActualRouteProgress
{
    std::size_t segment_index = 0U;
    std::size_t from_waypoint_index = 0U;
    std::size_t to_waypoint_index = 0U;
    double segment_fraction = 0.0;
    double along_route_m = 0.0;
    double route_completion_ratio = 0.0;
    std::size_t next_waypoint_index = 0U;
};

struct PlanActualDeviation
{
    double time_s = 0.0;
    PlanGeoPoint nearest_route_point;
    PlanActualRouteProgress progress;
    double cross_track_error_m = 0.0;
    std::optional<double> altitude_error_m;
};

struct PlanActualAggregate
{
    double planned_route_m = 0.0;
    double selected_track_m = 0.0;
    std::size_t compared_samples = 0U;
    std::optional<PlanActualDeviation> current;
    std::optional<double> average_cross_track_error_m;
    std::optional<double> max_cross_track_error_m;
    std::optional<double> max_cross_track_error_time_s;
    std::optional<double> average_altitude_error_m;
    std::optional<double> max_altitude_error_m;
    std::optional<double> max_altitude_error_time_s;
    double route_completion_ratio = 0.0;
    double route_completion_m = 0.0;
};

struct GhostReplayCurrentRunSummary
{
    bool baseline_loaded = false;
    double selected_track_m = 0.0;
    double route_completion_ratio = 0.0;
    std::optional<double> max_cross_track_error_m;
    std::optional<double> max_altitude_error_m;
};

struct PlanRouteProfileSummary
{
    bool plan_loaded = false;
    bool telemetry_compared = false;
    double route_distance_m = 0.0;
    double route_completion_ratio = 0.0;
    std::size_t compared_samples = 0U;
    std::optional<double> average_cross_track_error_m;
    std::optional<double> max_cross_track_error_m;
    std::optional<std::size_t> active_from_waypoint_index;
    std::optional<std::size_t> active_to_waypoint_index;
    std::optional<double> current_altitude_error_m;
    std::string clearance_profile_status = "unavailable";
};

[[nodiscard]] PlanVisualizationLoadResult
load_plan_visualization(const std::filesystem::path &path);

[[nodiscard]] PlanTrackComparison compare_plan_to_track(const PlanVisualizationData &plan,
                                                        const animus::telemetry_core::Track &track);
[[nodiscard]] PlanActualAggregate compare_plan_actual(const PlanVisualizationData &plan,
                                                      const animus::telemetry_core::Track &track,
                                                      double current_time_s);
[[nodiscard]] std::optional<PlanActualDeviation>
plan_actual_deviation_at(const PlanVisualizationData &plan,
                         const animus::telemetry_core::TelemetrySample &sample);
[[nodiscard]] GhostReplayCurrentRunSummary
ghost_replay_current_run_summary(const PlanActualAggregate &comparison);
[[nodiscard]] PlanRouteProfileSummary
plan_route_profile_summary(const PlanVisualizationData &plan,
                           const std::optional<PlanActualAggregate> &comparison);

[[nodiscard]] double plan_route_distance_m(const std::vector<PlanWaypoint> &waypoints);
[[nodiscard]] double
geo_distance_m(double a_lat_deg, double a_lon_deg, double b_lat_deg, double b_lon_deg);

} // namespace animus::app
