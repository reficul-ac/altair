#include "plot_series_buffer.hpp"

#include <algorithm>

namespace animus::app
{

PlotSeriesBuffer::PlotSeriesBuffer(PlotSeriesBufferConfig config) : config_(config)
{
}

void PlotSeriesBuffer::set_config(PlotSeriesBufferConfig config)
{
    config_ = config;
    if (!points_.empty())
    {
        prune(points_.back().time_s);
    }
}

void PlotSeriesBuffer::set_paused(const bool paused)
{
    paused_ = paused;
}

void PlotSeriesBuffer::append(const SignalSample sample)
{
    latest_status_ = sample.status;
    latest_time_s_ = sample.time_s;
    if (sample.status != SignalSampleStatus::Valid || paused_)
    {
        return;
    }
    latest_value_ = sample.value;
    if (!points_.empty() && sample.time_s <= points_.back().time_s)
    {
        if (sample.time_s == points_.back().time_s)
        {
            points_.back() = PlotPoint{.time_s = sample.time_s, .value = sample.value};
        }
        return;
    }
    points_.push_back(PlotPoint{.time_s = sample.time_s, .value = sample.value});
    prune(sample.time_s);
}

void PlotSeriesBuffer::clear()
{
    points_.clear();
    latest_status_ = SignalSampleStatus::Unavailable;
    latest_time_s_ = 0.0;
    latest_value_ = 0.0;
}

bool PlotSeriesBuffer::paused() const
{
    return paused_;
}

std::size_t PlotSeriesBuffer::size() const
{
    return points_.size();
}

std::span<const PlotPoint> PlotSeriesBuffer::points() const
{
    return points_;
}

SignalSampleStatus PlotSeriesBuffer::latest_status() const
{
    return latest_status_;
}

double PlotSeriesBuffer::latest_time_s() const
{
    return latest_time_s_;
}

double PlotSeriesBuffer::latest_value() const
{
    return latest_value_;
}

std::vector<PlotPoint> PlotSeriesBuffer::draw_points(const std::size_t render_max_points) const
{
    if (render_max_points == 0U || points_.empty())
    {
        return {};
    }
    if (points_.size() <= render_max_points)
    {
        return points_;
    }
    if (render_max_points == 1U)
    {
        return {points_.front()};
    }

    std::vector<PlotPoint> result;
    result.reserve(render_max_points);
    const double stride =
        static_cast<double>(points_.size() - 1U) / static_cast<double>(render_max_points - 1U);
    std::size_t previous = points_.size();
    for (std::size_t index = 0; index < render_max_points; ++index)
    {
        std::size_t source =
            static_cast<std::size_t>(std::min<double>(points_.size() - 1U, index * stride));
        if (index == render_max_points - 1U)
        {
            source = points_.size() - 1U;
        }
        if (source == previous && source + 1U < points_.size())
        {
            ++source;
        }
        result.push_back(points_[source]);
        previous = source;
    }
    return result;
}

void PlotSeriesBuffer::prune(const double newest_time_s)
{
    if (config_.time_window_s > 0.0)
    {
        const double oldest = newest_time_s - config_.time_window_s;
        const auto first = std::lower_bound(points_.begin(),
                                            points_.end(),
                                            oldest,
                                            [](const PlotPoint &point, const double cutoff)
                                            { return point.time_s < cutoff; });
        points_.erase(points_.begin(), first);
    }
    if (config_.max_points > 0U && points_.size() > config_.max_points)
    {
        points_.erase(points_.begin(), points_.begin() + (points_.size() - config_.max_points));
    }
}

} // namespace animus::app
