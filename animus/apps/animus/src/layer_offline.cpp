#include "layer_offline.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace animus::app
{
namespace
{

std::string shell_quote(const std::string &value)
{
    if (value.empty())
    {
        return "''";
    }
    const bool plain = std::all_of(value.begin(),
                                   value.end(),
                                   [](const unsigned char ch)
                                   {
                                       return std::isalnum(ch) != 0 || ch == '/' || ch == '.' ||
                                              ch == '_' || ch == '-' || ch == ':' || ch == ',' ||
                                              ch == '=' || ch == '{' || ch == '}';
                                   });
    if (plain)
    {
        return value;
    }
    std::string quoted = "'";
    for (const char ch : value)
    {
        if (ch == '\'')
        {
            quoted += "'\\''";
        }
        else
        {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::string format_bbox(const animus::geo_core::GeoBounds bounds)
{
    char buffer[128]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%.7f,%.7f,%.7f,%.7f",
                  bounds.west_deg,
                  bounds.south_deg,
                  bounds.east_deg,
                  bounds.north_deg);
    return buffer;
}

std::string prewarm_layers(const Options &options)
{
    std::string layers = "imagery,elevation";
    if (options.use_bathymetry && !options.bathymetry_geotiff.empty())
    {
        layers += ",bathymetry";
    }
    return layers;
}

} // namespace

animus::geo_core::GeoBounds
view_bounds_from_tiles(const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                       const int fallback_z,
                       const int fallback_x,
                       const int fallback_y)
{
    std::vector<animus::geo_core::TileCoord> coords;
    coords.reserve(std::max<std::size_t>(visible_tiles.size(), 1U));
    for (const auto &tile : visible_tiles)
    {
        coords.push_back(tile.coord);
    }
    if (coords.empty())
    {
        coords.push_back(animus::geo_core::TileCoord{fallback_z, fallback_x, fallback_y});
    }

    animus::geo_core::GeoBounds bounds = animus::geo_core::tile_to_bounds(coords.front());
    for (const auto coord : coords)
    {
        const auto tile_bounds = animus::geo_core::tile_to_bounds(coord);
        bounds.south_deg = std::min(bounds.south_deg, tile_bounds.south_deg);
        bounds.west_deg = std::min(bounds.west_deg, tile_bounds.west_deg);
        bounds.north_deg = std::max(bounds.north_deg, tile_bounds.north_deg);
        bounds.east_deg = std::max(bounds.east_deg, tile_bounds.east_deg);
    }
    return bounds;
}

std::size_t estimate_prewarm_tile_count(const animus::geo_core::GeoBounds bounds,
                                        const int min_z,
                                        const int max_z)
{
    std::size_t count = 0U;
    for (int z = min_z; z <= max_z; ++z)
    {
        const auto west_south =
            animus::geo_core::lat_lon_to_tile(bounds.south_deg, bounds.west_deg, z);
        const auto east_north =
            animus::geo_core::lat_lon_to_tile(bounds.north_deg, bounds.east_deg, z);
        const int x0 = std::min(west_south.x, east_north.x);
        const int x1 = std::max(west_south.x, east_north.x);
        const int y0 = std::min(west_south.y, east_north.y);
        const int y1 = std::max(west_south.y, east_north.y);
        count += static_cast<std::size_t>(x1 - x0 + 1) * static_cast<std::size_t>(y1 - y0 + 1);
    }
    return count;
}

PrewarmPreview
build_prewarm_preview(const Options &options,
                      const std::filesystem::path &pack_root,
                      const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles)
{
    PrewarmPreview preview;
    preview.bounds =
        view_bounds_from_tiles(visible_tiles, options.z, options.center_x, options.center_y);
    preview.tile_count = estimate_prewarm_tile_count(preview.bounds, options.min_z, options.max_z);
    preview.layers = prewarm_layers(options);

    std::ostringstream command;
    command << "python3 animus/tools/prewarm_cache.py" << " --bbox "
            << shell_quote(format_bbox(preview.bounds)) << " --min-z " << options.min_z
            << " --max-z " << options.max_z << " --pack-root " << shell_quote(pack_root.string())
            << " --cache-root " << shell_quote(options.cache_root.string());
    if (preview.layers != "imagery,elevation")
    {
        command << " --layers " << shell_quote(preview.layers);
    }
    if (!options.imagery_mbtiles.empty())
    {
        command << " --mbtiles " << shell_quote(options.imagery_mbtiles.string());
    }
    if (!options.remote_imagery_url_template.empty())
    {
        command << " --remote-url " << shell_quote(options.remote_imagery_url_template)
                << " --remote-user-agent " << shell_quote(options.remote_imagery_user_agent);
    }
    preview.command = command.str();
    return preview;
}

} // namespace animus::app
