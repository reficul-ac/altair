#pragma once

#include "plot_config.hpp"
#include "plot_series_buffer.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace animus::app
{

struct Options;
struct TelemetryPlaybackState;
struct RuntimeSignalInputs;

struct PlotRuntimeSeries
{
    std::string key;
    PlotSeriesBuffer buffer;
};

struct PlotUiState
{
    std::string selected_plot_id;
    std::string selected_series_id;
    std::string loaded_plot_id;
    std::string loaded_series_id;
    std::array<char, 128> plot_title{};
    std::array<char, 96> series_label{};
    std::array<char, 96> signal_search{};
    double previous_offline_time_s = 0.0;
    std::optional<double> frozen_latest_time_s;
    bool have_previous_offline_time = false;
    std::vector<PlotRuntimeSeries> buffers;
};

void draw_plot_shelf(Options &options,
                     TelemetryPlaybackState &playback,
                     const RuntimeSignalInputs &runtime,
                     PlotUiState &state);

} // namespace animus::app
