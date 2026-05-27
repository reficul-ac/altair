#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace animus::app
{

enum class ToolMode
{
    None,
    RangeBearing,
    TerrainProbe,
    ElevationProfile,
    ClearanceProfile,
};

struct MapToolPoint
{
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    std::optional<double> terrain_elevation_m;
    int tile_z = 0;
    int tile_x = 0;
    int tile_y = 0;
    float world_x = 0.0F;
    float world_y = 0.0F;
    float world_z = 0.0F;
    std::string label;
    std::size_t order = 0;
};

struct RangeBearingResult
{
    double distance_m = 0.0;
    double initial_bearing_deg = 0.0;
};

struct MapToolState
{
    static constexpr std::size_t point_cap = 64U;

    ToolMode mode = ToolMode::None;
    std::deque<MapToolPoint> markers;
    std::deque<MapToolPoint> bookmarks;
    std::optional<MapToolPoint> terrain_probe;
    std::optional<MapToolPoint> range_anchor;
    std::optional<MapToolPoint> range_endpoint;
    std::size_t next_order = 1U;
};

inline void push_bounded_point(std::deque<MapToolPoint> &points,
                               MapToolPoint point,
                               std::size_t &next_order,
                               const std::size_t cap = MapToolState::point_cap)
{
    point.order = next_order++;
    points.push_back(std::move(point));
    while (points.size() > cap)
    {
        points.pop_front();
    }
}

inline RangeBearingResult range_bearing_between(const MapToolPoint &from, const MapToolPoint &to)
{
    constexpr double deg_to_rad = 3.14159265358979323846 / 180.0;
    constexpr double rad_to_deg = 180.0 / 3.14159265358979323846;
    constexpr double earth_radius_m = 6371008.8;
    const double lat1 = from.lat_deg * deg_to_rad;
    const double lat2 = to.lat_deg * deg_to_rad;
    const double d_lat = (to.lat_deg - from.lat_deg) * deg_to_rad;
    const double d_lon = (to.lon_deg - from.lon_deg) * deg_to_rad;

    const double sin_half_lat = std::sin(d_lat * 0.5);
    const double sin_half_lon = std::sin(d_lon * 0.5);
    const double a = sin_half_lat * sin_half_lat +
                     std::cos(lat1) * std::cos(lat2) * sin_half_lon * sin_half_lon;
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    const double y = std::sin(d_lon) * std::cos(lat2);
    const double x =
        std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(d_lon);
    double bearing = std::atan2(y, x) * rad_to_deg;
    if (bearing < 0.0)
    {
        bearing += 360.0;
    }
    return {earth_radius_m * c, bearing};
}

} // namespace animus::app
