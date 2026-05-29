#include "plan_visualization.hpp"

#include <filesystem>
#include <fstream>
#include <optional>

#include <gtest/gtest.h>

namespace
{

std::filesystem::path write_temp_plan(const std::string &name, const std::string &text)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path);
    output << text;
    return path;
}

animus::telemetry_core::TelemetrySample
telemetry_sample(const double time_s,
                 const double lat_deg,
                 const double lon_deg,
                 std::optional<double> altitude_m = std::nullopt)
{
    animus::telemetry_core::TelemetrySample sample;
    sample.time_s = time_s;
    sample.lat_deg = lat_deg;
    sample.lon_deg = lon_deg;
    sample.altitude_relative_m = altitude_m;
    sample.fields.position = true;
    sample.fields.altitude_relative = altitude_m.has_value();
    return sample;
}

animus::app::PlanVisualizationData straight_plan_with_altitudes()
{
    animus::app::PlanVisualizationData plan;
    plan.mission_waypoints.push_back({{39.0, -120.0, 100.0}, "1"});
    plan.mission_waypoints.push_back({{39.001, -120.0, 110.0}, "2"});
    plan.mission_waypoints.push_back({{39.002, -120.0, 120.0}, "3"});
    plan.route_distance_m = animus::app::plan_route_distance_m(plan.mission_waypoints);
    return plan;
}

} // namespace

TEST(AnimusPlanVisualization, LoadsMinimalValidPlan)
{
    const std::filesystem::path path = write_temp_plan("animus_minimal.plan",
                                                       R"json({
          "fileType": "Plan",
          "groundStation": "QGroundControl",
          "mission": {
            "version": 2,
            "plannedHomePosition": [39.0, -120.0, 1900.0],
            "items": [
              {"type": "SimpleItem", "params": [0, 0, 0, null, 39.0000, -120.0000, 1910.0]},
              {"type": "SimpleItem", "params": [0, 0, 0, null, 39.0010, -120.0000, 1920.0]}
            ]
          }
        })json");

    const auto result = animus::app::load_plan_visualization(path);

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.data);
    EXPECT_EQ(result.data->mission_waypoints.size(), 2U);
    EXPECT_EQ(result.data->mission_waypoints[0].label, "1");
    EXPECT_NEAR(result.data->route_distance_m, 111.2, 0.5);

    std::filesystem::remove(path);
}

TEST(AnimusPlanVisualization, RejectsMalformedJson)
{
    const std::filesystem::path path = write_temp_plan("animus_bad_json.plan", "{");

    const auto result = animus::app::load_plan_visualization(path);

    EXPECT_FALSE(result.error.empty());
    EXPECT_FALSE(result.data);

    std::filesystem::remove(path);
}

TEST(AnimusPlanVisualization, RejectsMissingFileType)
{
    const std::filesystem::path path =
        write_temp_plan("animus_missing_file_type.plan", R"json({"mission": {"items": []}})json");

    const auto result = animus::app::load_plan_visualization(path);

    EXPECT_EQ(result.error, "plan fileType must be Plan");
    EXPECT_FALSE(result.data);

    std::filesystem::remove(path);
}

TEST(AnimusPlanVisualization, UnsupportedComplexItemWarns)
{
    const std::filesystem::path path = write_temp_plan("animus_complex_warning.plan",
                                                       R"json({
          "fileType": "Plan",
          "mission": {
            "version": 2,
            "items": [
              {"type": "ComplexItem", "complexItemType": "survey"}
            ]
          }
        })json");

    const auto result = animus::app::load_plan_visualization(path);

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.data);
    EXPECT_EQ(result.data->unsupported_item_count, 1U);
    EXPECT_EQ(result.diagnostics.size(), 1U);

    std::filesystem::remove(path);
}

TEST(AnimusPlanVisualization, ParsesFenceCirclePolygonAndRally)
{
    const std::filesystem::path path = write_temp_plan("animus_fence_rally.plan",
                                                       R"json({
          "fileType": "Plan",
          "mission": {"version": 2, "items": []},
          "geoFence": {
            "polygons": [
              {"polygon": [[39.0, -120.0], [39.0, -120.1], [39.1, -120.1]]}
            ],
            "circles": [
              {"circle": {"center": [39.0, -120.0], "radius": 75.0}}
            ]
          },
          "rallyPoints": {
            "points": [[39.05, -120.05, 1950.0]]
          }
        })json");

    const auto result = animus::app::load_plan_visualization(path);

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.data);
    EXPECT_EQ(result.data->geofence_polygons.size(), 1U);
    EXPECT_EQ(result.data->geofence_circles.size(), 1U);
    EXPECT_EQ(result.data->rally_points.size(), 1U);
    EXPECT_DOUBLE_EQ(result.data->geofence_circles[0].radius_m, 75.0);

    std::filesystem::remove(path);
}

TEST(AnimusPlanVisualization, RouteDistanceIsDeterministic)
{
    std::vector<animus::app::PlanWaypoint> waypoints;
    waypoints.push_back({{39.0, -120.0, std::nullopt}, "1"});
    waypoints.push_back({{39.0, -120.001, std::nullopt}, "2"});
    waypoints.push_back({{39.001, -120.001, std::nullopt}, "3"});

    const double distance = animus::app::plan_route_distance_m(waypoints);

    EXPECT_NEAR(distance, 197.6, 0.5);
    EXPECT_DOUBLE_EQ(distance, animus::app::plan_route_distance_m(waypoints));
}

TEST(AnimusPlanVisualization, PlanActualSelectsActiveSegmentBeforeBetweenAndAfter)
{
    const auto plan = straight_plan_with_altitudes();
    animus::telemetry_core::Track track;
    track.samples = {telemetry_sample(0.0, 38.9995, -120.0),
                     telemetry_sample(1.0, 39.0015, -120.0),
                     telemetry_sample(2.0, 39.0025, -120.0)};

    const auto before = animus::app::compare_plan_actual(plan, track, 0.0);
    ASSERT_TRUE(before.current);
    EXPECT_EQ(before.current->progress.segment_index, 0U);
    EXPECT_DOUBLE_EQ(before.current->progress.segment_fraction, 0.0);

    const auto between = animus::app::compare_plan_actual(plan, track, 1.0);
    ASSERT_TRUE(between.current);
    EXPECT_EQ(between.current->progress.segment_index, 1U);
    EXPECT_NEAR(between.current->progress.segment_fraction, 0.5, 0.02);

    const auto after = animus::app::compare_plan_actual(plan, track, 2.0);
    ASSERT_TRUE(after.current);
    EXPECT_EQ(after.current->progress.segment_index, 1U);
    EXPECT_DOUBLE_EQ(after.current->progress.segment_fraction, 1.0);
}

TEST(AnimusPlanVisualization, PlanActualComputesStraightAndMultiSegmentCrossTrack)
{
    auto plan = straight_plan_with_altitudes();
    animus::telemetry_core::Track track;
    track.samples = {telemetry_sample(0.0, 39.001, -119.999)};

    const auto straight = animus::app::compare_plan_actual(plan, track, 0.0);

    ASSERT_TRUE(straight.current);
    EXPECT_NEAR(straight.current->cross_track_error_m, 86.4, 1.0);

    plan.mission_waypoints.clear();
    plan.mission_waypoints.push_back({{39.0, -120.0, std::nullopt}, "1"});
    plan.mission_waypoints.push_back({{39.0, -119.999, std::nullopt}, "2"});
    plan.mission_waypoints.push_back({{39.001, -119.999, std::nullopt}, "3"});
    plan.route_distance_m = animus::app::plan_route_distance_m(plan.mission_waypoints);
    track.samples = {telemetry_sample(1.0, 39.0005, -119.999)};

    const auto multi = animus::app::compare_plan_actual(plan, track, 1.0);

    ASSERT_TRUE(multi.current);
    EXPECT_NEAR(multi.current->cross_track_error_m, 0.0, 0.5);
    EXPECT_EQ(multi.current->progress.segment_index, 1U);
}

TEST(AnimusPlanVisualization, PlanActualAltitudeErrorRequiresPlannedAltitude)
{
    auto plan = straight_plan_with_altitudes();
    animus::telemetry_core::Track track;
    track.samples = {telemetry_sample(0.0, 39.001, -120.0, 130.0)};

    const auto with_altitude = animus::app::compare_plan_actual(plan, track, 0.0);

    ASSERT_TRUE(with_altitude.current);
    ASSERT_TRUE(with_altitude.current->altitude_error_m);
    EXPECT_NEAR(*with_altitude.current->altitude_error_m, 20.0, 0.5);

    plan.mission_waypoints[0].point.alt_m.reset();
    plan.mission_waypoints[1].point.alt_m.reset();
    plan.mission_waypoints[2].point.alt_m.reset();
    const auto without_altitude = animus::app::compare_plan_actual(plan, track, 0.0);

    ASSERT_TRUE(without_altitude.current);
    EXPECT_FALSE(without_altitude.current->altitude_error_m);
}

TEST(AnimusPlanVisualization, PlanActualReportsWaypointAndRouteCompletion)
{
    const auto plan = straight_plan_with_altitudes();
    animus::telemetry_core::Track partial;
    partial.samples = {telemetry_sample(0.0, 39.0, -120.0), telemetry_sample(1.0, 39.001, -120.0)};

    const auto partial_summary = animus::app::compare_plan_actual(plan, partial, 1.0);

    EXPECT_NEAR(partial_summary.route_completion_ratio, 0.5, 0.02);
    ASSERT_TRUE(partial_summary.current);
    EXPECT_EQ(partial_summary.current->progress.to_waypoint_index, 1U);

    animus::telemetry_core::Track complete;
    complete.samples = {telemetry_sample(0.0, 39.0, -120.0), telemetry_sample(1.0, 39.002, -120.0)};

    const auto complete_summary = animus::app::compare_plan_actual(plan, complete, 1.0);

    EXPECT_NEAR(complete_summary.route_completion_ratio, 1.0, 0.001);
}

TEST(AnimusPlanVisualization, PlanActualHandlesUnavailableGeometry)
{
    animus::app::PlanVisualizationData empty_plan;
    animus::telemetry_core::Track track;
    track.samples = {telemetry_sample(0.0, 39.0, -120.0)};

    const auto empty = animus::app::compare_plan_actual(empty_plan, track, 0.0);

    EXPECT_FALSE(empty.current);
    EXPECT_EQ(empty.compared_samples, 0U);

    animus::app::PlanVisualizationData single_waypoint;
    single_waypoint.mission_waypoints.push_back({{39.0, -120.0, std::nullopt}, "1"});
    auto missing_position = telemetry_sample(0.0, 39.0, -120.0);
    missing_position.fields.position = false;
    track.samples = {missing_position};

    const auto single = animus::app::compare_plan_actual(single_waypoint, track, 0.0);

    EXPECT_FALSE(single.current);
    EXPECT_EQ(single.compared_samples, 0U);
}
