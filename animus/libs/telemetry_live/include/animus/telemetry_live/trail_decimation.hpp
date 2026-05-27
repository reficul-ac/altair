#pragma once

#include <cstddef>
#include <vector>

namespace animus::telemetry_live
{

[[nodiscard]] std::vector<std::size_t> decimated_trail_indices(std::size_t sample_count,
                                                               std::size_t max_points);

} // namespace animus::telemetry_live
