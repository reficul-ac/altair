#pragma once

#include <chrono>

namespace animus::render_core
{

class RenderStats
{
  public:
    void frame_started();
    void frame_finished();

    [[nodiscard]] int frame_count() const;
    [[nodiscard]] double last_frame_seconds() const;
    [[nodiscard]] double total_seconds() const;

  private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point first_frame_start_{};
    Clock::time_point current_frame_start_{};
    Clock::time_point last_frame_end_{};
    int frame_count_ = 0;
    double last_frame_seconds_ = 0.0;
};

} // namespace animus::render_core
