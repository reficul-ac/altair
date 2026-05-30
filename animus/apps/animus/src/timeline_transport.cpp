#include "timeline_transport.hpp"

#include <algorithm>
#include <cmath>

namespace animus::app
{

double
time_to_timeline_fraction(const double start_time_s, const double end_time_s, const double time_s)
{
    if (!std::isfinite(start_time_s) || !std::isfinite(end_time_s) || !std::isfinite(time_s) ||
        end_time_s <= start_time_s)
    {
        return 0.0;
    }
    const double clamped_time = std::clamp(time_s, start_time_s, end_time_s);
    return std::clamp((clamped_time - start_time_s) / (end_time_s - start_time_s), 0.0, 1.0);
}

double
timeline_fraction_to_time(const double start_time_s, const double end_time_s, const double fraction)
{
    if (!std::isfinite(start_time_s) || !std::isfinite(end_time_s) || end_time_s <= start_time_s)
    {
        return std::isfinite(start_time_s) ? start_time_s : 0.0;
    }
    const double clamped_fraction = std::isfinite(fraction) ? std::clamp(fraction, 0.0, 1.0) : 0.0;
    return std::clamp(
        start_time_s + (end_time_s - start_time_s) * clamped_fraction, start_time_s, end_time_s);
}

double timeline_step_time(const double start_time_s,
                          const double end_time_s,
                          const double time_s,
                          const double delta_s)
{
    if (!std::isfinite(start_time_s) || !std::isfinite(end_time_s) || end_time_s <= start_time_s)
    {
        return std::isfinite(start_time_s) ? start_time_s : 0.0;
    }
    const double base_time = std::isfinite(time_s) ? time_s : start_time_s;
    const double step = std::isfinite(delta_s) ? delta_s : 0.0;
    return std::clamp(base_time + step, start_time_s, end_time_s);
}

} // namespace animus::app
