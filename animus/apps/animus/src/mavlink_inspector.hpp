#pragma once

#include "plot_config.hpp"
#include "plot_ui.hpp"

#include <optional>
#include <string>

namespace animus::app
{

enum class MavlinkInspectorPlotTarget
{
    Dedicated,
    Existing,
};

struct MavlinkInspectorPlotResult
{
    PlotDefinition *plot = nullptr;
    PlotSeriesDefinition *series = nullptr;
    bool created_plot = false;
    bool created_series = false;
};

[[nodiscard]] std::string mavlink_inspector_field_path(std::string_view message_name,
                                                       std::string_view field_name);
[[nodiscard]] std::optional<PlotSeriesDefinition>
make_mavlink_inspector_series(const SignalCatalog &catalog,
                              std::string_view message_name,
                              std::string_view field_name);
[[nodiscard]] bool plot_series_matches_mavlink_field(const PlotSeriesDefinition &series,
                                                     std::string_view message_name,
                                                     std::string_view field_name,
                                                     PlotEntityBindingMode binding);
MavlinkInspectorPlotResult plot_mavlink_inspector_field(PlotShelfConfig &config,
                                                        PlotUiState &state,
                                                        const SignalCatalog &catalog,
                                                        std::string_view message_name,
                                                        std::string_view field_name,
                                                        MavlinkInspectorPlotTarget target);

} // namespace animus::app
