#include "animus/render_core/render_stats.hpp"

namespace animus::render_core {

void RenderStats::frame_started()
{
    current_frame_start_ = Clock::now();
    if (frame_count_ == 0) {
        first_frame_start_ = current_frame_start_;
    }
}

void RenderStats::frame_finished()
{
    last_frame_end_ = Clock::now();
    last_frame_seconds_ =
        std::chrono::duration<double>(last_frame_end_ - current_frame_start_).count();
    ++frame_count_;
}

int RenderStats::frame_count() const
{
    return frame_count_;
}

double RenderStats::last_frame_seconds() const
{
    return last_frame_seconds_;
}

double RenderStats::total_seconds() const
{
    if (frame_count_ == 0) {
        return 0.0;
    }
    return std::chrono::duration<double>(last_frame_end_ - first_frame_start_).count();
}

} // namespace animus::render_core
