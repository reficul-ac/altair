#include "plot_series_buffer.hpp"

#include <gtest/gtest.h>

namespace
{

animus::app::SignalSample valid(const double time_s, const double value)
{
    return animus::app::SignalSample{
        .time_s = time_s,
        .value = value,
        .status = animus::app::SignalSampleStatus::Valid,
    };
}

} // namespace

TEST(AnimusPlotSeriesBuffer, PrunesByMaxPointCount)
{
    animus::app::PlotSeriesBuffer buffer({.time_window_s = 100.0, .max_points = 3U});

    buffer.append(valid(1.0, 10.0));
    buffer.append(valid(2.0, 20.0));
    buffer.append(valid(3.0, 30.0));
    buffer.append(valid(4.0, 40.0));

    ASSERT_EQ(buffer.size(), 3U);
    EXPECT_DOUBLE_EQ(buffer.points().front().time_s, 2.0);
    EXPECT_DOUBLE_EQ(buffer.points().back().time_s, 4.0);
}

TEST(AnimusPlotSeriesBuffer, PrunesByTimeWindow)
{
    animus::app::PlotSeriesBuffer buffer({.time_window_s = 2.0, .max_points = 10U});

    buffer.append(valid(1.0, 10.0));
    buffer.append(valid(2.0, 20.0));
    buffer.append(valid(4.0, 40.0));

    ASSERT_EQ(buffer.size(), 2U);
    EXPECT_DOUBLE_EQ(buffer.points().front().time_s, 2.0);
}

TEST(AnimusPlotSeriesBuffer, ClearRemovesHistoryAndStatus)
{
    animus::app::PlotSeriesBuffer buffer;
    buffer.append(valid(1.0, 10.0));

    buffer.clear();

    EXPECT_EQ(buffer.size(), 0U);
    EXPECT_EQ(buffer.latest_status(), animus::app::SignalSampleStatus::Unavailable);
}

TEST(AnimusPlotSeriesBuffer, PauseSkipsAppends)
{
    animus::app::PlotSeriesBuffer buffer;
    buffer.append(valid(1.0, 10.0));
    buffer.set_paused(true);
    buffer.append(valid(2.0, 20.0));

    ASSERT_EQ(buffer.size(), 1U);
    EXPECT_DOUBLE_EQ(buffer.points().back().time_s, 1.0);
    EXPECT_EQ(buffer.latest_status(), animus::app::SignalSampleStatus::Valid);
}

TEST(AnimusPlotSeriesBuffer, DownsampleIncludesEndpointsAndPreservesOrder)
{
    animus::app::PlotSeriesBuffer buffer({.time_window_s = 100.0, .max_points = 20U});
    for (int index = 0; index < 10; ++index)
    {
        buffer.append(valid(static_cast<double>(index), static_cast<double>(index * 10)));
    }

    const std::vector<animus::app::PlotPoint> draw = buffer.draw_points(4U);

    ASSERT_EQ(draw.size(), 4U);
    EXPECT_DOUBLE_EQ(draw.front().time_s, 0.0);
    EXPECT_DOUBLE_EQ(draw.back().time_s, 9.0);
    EXPECT_LT(draw[0].time_s, draw[1].time_s);
    EXPECT_LT(draw[1].time_s, draw[2].time_s);
    EXPECT_LT(draw[2].time_s, draw[3].time_s);
}

TEST(AnimusPlotSeriesBuffer, UnavailableDoesNotInjectZero)
{
    animus::app::PlotSeriesBuffer buffer;
    buffer.append(valid(1.0, 10.0));
    buffer.append(animus::app::SignalSample{
        .time_s = 2.0,
        .value = 0.0,
        .status = animus::app::SignalSampleStatus::Unavailable,
    });

    ASSERT_EQ(buffer.size(), 1U);
    EXPECT_DOUBLE_EQ(buffer.points().back().value, 10.0);
    EXPECT_EQ(buffer.latest_status(), animus::app::SignalSampleStatus::Unavailable);
}
