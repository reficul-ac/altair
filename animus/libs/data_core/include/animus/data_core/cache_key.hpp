#pragma once

#include <string>
#include <string_view>

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"

namespace animus::data_core
{

std::string cache_component(std::string_view value);
std::string layer_cache_prefix(const terrain_core::LayerSpec &spec);
std::string tile_cache_key(const terrain_core::LayerSpec &spec, geo_core::TileCoord coord);

} // namespace animus::data_core
