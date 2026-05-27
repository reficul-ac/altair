#pragma once

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/terrain_stream.hpp"
#include "options.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace animus::app
{

struct PrewarmPreview
{
    animus::geo_core::GeoBounds bounds;
    std::size_t tile_count = 0;
    std::string layers;
    std::string command;
};

[[nodiscard]] animus::geo_core::GeoBounds
view_bounds_from_tiles(const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                       int fallback_z,
                       int fallback_x,
                       int fallback_y);

[[nodiscard]] std::size_t estimate_prewarm_tile_count(animus::geo_core::GeoBounds bounds,
                                                      int min_z,
                                                      int max_z);

[[nodiscard]] PrewarmPreview build_prewarm_preview(
    const Options &options,
    const std::filesystem::path &pack_root,
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles);

} // namespace animus::app
