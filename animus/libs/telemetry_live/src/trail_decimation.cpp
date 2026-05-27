#include "animus/telemetry_live/trail_decimation.hpp"

#include <cmath>

namespace animus::telemetry_live
{

std::vector<std::size_t> decimated_trail_indices(const std::size_t sample_count,
                                                 const std::size_t max_points)
{
    std::vector<std::size_t> indices;
    if (sample_count == 0U || max_points == 0U)
    {
        return indices;
    }
    if (sample_count <= max_points)
    {
        indices.reserve(sample_count);
        for (std::size_t index = 0U; index < sample_count; ++index)
        {
            indices.push_back(index);
        }
        return indices;
    }
    if (max_points == 1U)
    {
        return {sample_count - 1U};
    }

    indices.reserve(max_points);
    const double step =
        static_cast<double>(sample_count - 1U) / static_cast<double>(max_points - 1U);
    std::size_t previous = 0U;
    for (std::size_t point = 0U; point < max_points; ++point)
    {
        std::size_t index = static_cast<std::size_t>(std::llround(step * point));
        if (point + 1U == max_points)
        {
            index = sample_count - 1U;
        }
        if (indices.empty() || index > previous)
        {
            indices.push_back(index);
            previous = index;
        }
    }
    return indices;
}

} // namespace animus::telemetry_live
