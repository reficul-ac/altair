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

[[nodiscard]] PlanVisualizationLoadResult
load_plan_visualization(const std::filesystem::path &path);

[[nodiscard]] PlanTrackComparison compare_plan_to_track(const PlanVisualizationData &plan,
                                                        const animus::telemetry_core::Track &track);

[[nodiscard]] double plan_route_distance_m(const std::vector<PlanWaypoint> &waypoints);
[[nodiscard]] double
geo_distance_m(double a_lat_deg, double a_lon_deg, double b_lat_deg, double b_lon_deg);

} // namespace animus::app
