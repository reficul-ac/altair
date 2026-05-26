#include "animus/data_core/cache_key.hpp"

#include <cctype>
#include <sstream>

namespace animus::data_core
{
namespace
{

std::string normalize_component(std::string_view value)
{
    if (value.empty())
    {
        return "_";
    }

    std::string result;
    result.reserve(value.size());

    for (const unsigned char raw : value)
    {
        const char ch = static_cast<char>(raw);
        if (std::isalnum(raw) != 0 || ch == '-' || ch == '_')
        {
            result.push_back(static_cast<char>(std::tolower(raw)));
        }
        else
        {
            result.push_back('_');
        }
    }

    return result;
}

} // namespace

std::string cache_component(const std::string_view value)
{
    return normalize_component(value);
}

std::string layer_cache_prefix(const terrain_core::LayerSpec &spec)
{
    std::ostringstream stream;
    stream << terrain_core::to_string(spec.type) << '/' << cache_component(spec.source) << '/'
           << cache_component(spec.style) << '/' << cache_component(spec.extra) << '/'
           << spec.resolution << '/' << spec.min_zoom << '-' << spec.max_zoom;
    return stream.str();
}

std::string tile_cache_key(const terrain_core::LayerSpec &spec, const geo_core::TileCoord coord)
{
    return layer_cache_prefix(spec) + '/' + geo_core::tile_key(coord);
}

} // namespace animus::data_core
