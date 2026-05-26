#include "animus/geo_core/tile_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace animus::geo_core
{
namespace
{

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double min_longitude_deg = -180.0;
constexpr double max_longitude_deg = 180.0;

void validate_zoom(const int z)
{
    if (z < 0 || z > max_tile_zoom)
    {
        throw std::invalid_argument("tile zoom is outside the supported range");
    }
}

void validate_tile(const TileCoord coord)
{
    if (!is_valid(coord))
    {
        throw std::invalid_argument("tile coordinate is outside the supported range");
    }
}

double deg_to_rad(const double degrees)
{
    return degrees * pi / 180.0;
}

double rad_to_deg(const double radians)
{
    return radians * 180.0 / pi;
}

double clamp_latitude(const double lat_deg)
{
    return std::clamp(lat_deg, -web_mercator_max_latitude_deg, web_mercator_max_latitude_deg);
}

double clamp_longitude(const double lon_deg)
{
    return std::clamp(lon_deg, min_longitude_deg, max_longitude_deg);
}

double mercator_y_normalized(const double lat_deg)
{
    const double lat_rad = deg_to_rad(clamp_latitude(lat_deg));
    return std::clamp((1.0 - (std::asinh(std::tan(lat_rad)) / pi)) * 0.5, 0.0, 1.0);
}

double longitude_x_normalized(const double lon_deg)
{
    return (clamp_longitude(lon_deg) + 180.0) / 360.0;
}

int tile_index_from_global(const double global, const int axis_count)
{
    const auto index = static_cast<int>(std::floor(global));
    return std::clamp(index, 0, axis_count - 1);
}

double latitude_from_tile_y(const double y, const int axis_count)
{
    const double mercator = pi * (1.0 - (2.0 * y / static_cast<double>(axis_count)));
    return rad_to_deg(std::atan(std::sinh(mercator)));
}

std::uint64_t fnv1a_mix_int(std::uint64_t hash, const int value)
{
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;
    const auto bits = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8)
    {
        hash ^= (bits >> shift) & 0xffU;
        hash *= fnv_prime;
    }
    return hash;
}

} // namespace

bool is_valid(const TileCoord coord)
{
    if (coord.z < 0 || coord.z > max_tile_zoom)
    {
        return false;
    }

    const int axis_count = tiles_per_axis(coord.z);
    return coord.x >= 0 && coord.y >= 0 && coord.x < axis_count && coord.y < axis_count;
}

int tiles_per_axis(const int z)
{
    validate_zoom(z);
    return 1 << z;
}

TileCoord lat_lon_to_tile(const double lat_deg, const double lon_deg, const int z)
{
    const int axis_count = tiles_per_axis(z);
    const double x_global = longitude_x_normalized(lon_deg) * axis_count;
    const double y_global = mercator_y_normalized(lat_deg) * axis_count;

    return TileCoord{
        z,
        tile_index_from_global(x_global, axis_count),
        tile_index_from_global(y_global, axis_count),
    };
}

GeoBounds tile_to_bounds(const TileCoord coord)
{
    validate_tile(coord);

    const int axis_count = tiles_per_axis(coord.z);
    const double west =
        (static_cast<double>(coord.x) / static_cast<double>(axis_count)) * 360.0 - 180.0;
    const double east =
        (static_cast<double>(coord.x + 1) / static_cast<double>(axis_count)) * 360.0 - 180.0;

    return GeoBounds{
        latitude_from_tile_y(static_cast<double>(coord.y + 1), axis_count),
        west,
        latitude_from_tile_y(static_cast<double>(coord.y), axis_count),
        east,
    };
}

Vec2 lat_lon_to_tile_uv(const double lat_deg, const double lon_deg, const TileCoord coord)
{
    validate_tile(coord);

    const int axis_count = tiles_per_axis(coord.z);
    const double x_global = longitude_x_normalized(lon_deg) * axis_count;
    const double y_global = mercator_y_normalized(lat_deg) * axis_count;

    return Vec2{
        x_global - static_cast<double>(coord.x),
        y_global - static_cast<double>(coord.y),
    };
}

TileCoord parent(const TileCoord coord)
{
    validate_tile(coord);
    if (coord.z == 0)
    {
        return coord;
    }

    return TileCoord{coord.z - 1, coord.x / 2, coord.y / 2};
}

std::array<TileCoord, 4> children(const TileCoord coord)
{
    validate_tile(coord);
    if (coord.z == max_tile_zoom)
    {
        throw std::invalid_argument("max-zoom tile has no supported children");
    }

    const int child_z = coord.z + 1;
    const int child_x = coord.x * 2;
    const int child_y = coord.y * 2;
    return {
        TileCoord{child_z, child_x, child_y},
        TileCoord{child_z, child_x + 1, child_y},
        TileCoord{child_z, child_x, child_y + 1},
        TileCoord{child_z, child_x + 1, child_y + 1},
    };
}

std::string tile_key(const TileCoord coord)
{
    validate_tile(coord);

    std::ostringstream stream;
    stream << coord.z << '/' << coord.x << '/' << coord.y;
    return stream.str();
}

std::size_t hash_value(const TileCoord coord)
{
    validate_tile(coord);

    constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
    std::uint64_t hash = fnv_offset;
    hash = fnv1a_mix_int(hash, coord.z);
    hash = fnv1a_mix_int(hash, coord.x);
    hash = fnv1a_mix_int(hash, coord.y);
    return static_cast<std::size_t>(hash);
}

} // namespace animus::geo_core
