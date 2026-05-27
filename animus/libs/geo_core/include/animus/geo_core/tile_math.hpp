#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <functional>
#include <string>

namespace animus::geo_core
{

struct TileCoord
{
    int z = 0;
    int x = 0;
    int y = 0;

    auto operator<=>(const TileCoord &) const = default;
};

struct GeoBounds
{
    double south_deg = 0.0;
    double west_deg = 0.0;
    double north_deg = 0.0;
    double east_deg = 0.0;
};

struct Vec2
{
    double u = 0.0;
    double v = 0.0;
};

constexpr double web_mercator_max_latitude_deg = 85.0511287798066;
constexpr int max_tile_zoom = 30;

bool is_valid(TileCoord coord);
int tiles_per_axis(int z);

TileCoord lat_lon_to_tile(double lat_deg, double lon_deg, int z);
GeoBounds tile_to_bounds(TileCoord coord);
Vec2 lat_lon_to_tile_uv(double lat_deg, double lon_deg, TileCoord coord);
Vec2 tile_space_to_lat_lon(double x, double y, int z);

TileCoord parent(TileCoord coord);
std::array<TileCoord, 4> children(TileCoord coord);

std::string tile_key(TileCoord coord);
std::size_t hash_value(TileCoord coord);

} // namespace animus::geo_core

template <> struct std::hash<animus::geo_core::TileCoord>
{
    std::size_t operator()(animus::geo_core::TileCoord coord) const
    {
        return animus::geo_core::hash_value(coord);
    }
};
