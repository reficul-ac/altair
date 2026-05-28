#pragma once

#include "telemetry_signal_catalog.hpp"

#include <cstddef>
#include <deque>
#include <span>
#include <vector>

namespace animus::app
{

struct PlotPoint
{
    double time_s = 0.0;
    double value = 0.0;
};

struct PlotSeriesBufferConfig
{
    double time_window_s = 30.0;
    std::size_t max_points = 2048U;
};

class PlotSeriesBuffer
{
  public:
    explicit PlotSeriesBuffer(PlotSeriesBufferConfig config = {});

    void set_config(PlotSeriesBufferConfig config);
    void set_paused(bool paused);
    void append(SignalSample sample);
    void clear();

    [[nodiscard]] bool paused() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::span<const PlotPoint> points() const;
    [[nodiscard]] SignalSampleStatus latest_status() const;
    [[nodiscard]] double latest_time_s() const;
    [[nodiscard]] double latest_value() const;
    [[nodiscard]] std::vector<PlotPoint> draw_points(std::size_t render_max_points) const;

  private:
    void prune(double newest_time_s);

    PlotSeriesBufferConfig config_;
    std::vector<PlotPoint> points_;
    bool paused_ = false;
    SignalSampleStatus latest_status_ = SignalSampleStatus::Unavailable;
    double latest_time_s_ = 0.0;
    double latest_value_ = 0.0;
};

} // namespace animus::app
