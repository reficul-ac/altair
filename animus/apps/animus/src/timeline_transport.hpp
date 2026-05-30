#pragma once

namespace animus::app
{

[[nodiscard]] double
time_to_timeline_fraction(double start_time_s, double end_time_s, double time_s);
[[nodiscard]] double
timeline_fraction_to_time(double start_time_s, double end_time_s, double fraction);
[[nodiscard]] double
timeline_step_time(double start_time_s, double end_time_s, double time_s, double delta_s);

} // namespace animus::app
