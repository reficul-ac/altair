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
