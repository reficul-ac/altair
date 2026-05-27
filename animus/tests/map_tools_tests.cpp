#include "../apps/animus/src/map_tools.hpp"

#include <deque>

#include <gtest/gtest.h>

namespace
{

using animus::app::MapToolPoint;
using animus::app::MapToolState;
using animus::app::push_bounded_point;
using animus::app::range_bearing_between;

TEST(MapTools, RangeBearingUsesGreatCircleDistanceAndInitialBearing)
{
    MapToolPoint start;
    start.lat_deg = 0.0;
    start.lon_deg = 0.0;
    MapToolPoint east;
    east.lat_deg = 0.0;
    east.lon_deg = 1.0;
    MapToolPoint north;
    north.lat_deg = 1.0;
    north.lon_deg = 0.0;

    const auto east_result = range_bearing_between(start, east);
    EXPECT_NEAR(east_result.distance_m, 111195.0, 20.0);
    EXPECT_NEAR(east_result.initial_bearing_deg, 90.0, 1.0e-9);

    const auto north_result = range_bearing_between(start, north);
    EXPECT_NEAR(north_result.distance_m, 111195.0, 20.0);
    EXPECT_NEAR(north_result.initial_bearing_deg, 0.0, 1.0e-9);
}

TEST(MapTools, BoundedPointsEvictOldestAndAssignMonotonicOrder)
{
    std::deque<MapToolPoint> points;
    std::size_t next_order = 1U;
    for (std::size_t index = 0; index < MapToolState::point_cap + 3U; ++index)
    {
        MapToolPoint point;
        point.lat_deg = static_cast<double>(index);
        push_bounded_point(points, point, next_order);
    }

    ASSERT_EQ(points.size(), MapToolState::point_cap);
    EXPECT_DOUBLE_EQ(points.front().lat_deg, 3.0);
    EXPECT_EQ(points.front().order, 4U);
    EXPECT_DOUBLE_EQ(points.back().lat_deg, static_cast<double>(MapToolState::point_cap + 2U));
    EXPECT_EQ(points.back().order, MapToolState::point_cap + 3U);
    EXPECT_EQ(next_order, MapToolState::point_cap + 4U);
}

} // namespace
