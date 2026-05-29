#include "mavlink_inspector.hpp"

#include <algorithm>

namespace animus::app
{
namespace
{

constexpr const char *inspector_plot_id = "mavlink_inspector";
constexpr const char *inspector_plot_title = "MAVLink Inspector";

std::string next_series_id(const PlotDefinition &plot)
{
    return "mavlink_" + std::to_string(plot.series.size() + 1U);
}

PlotDefinition &dedicated_plot(PlotShelfConfig &config, MavlinkInspectorPlotResult &result)
{
    auto found = std::find_if(config.plots.begin(),
                              config.plots.end(),
                              [](const PlotDefinition &plot)
                              { return plot.id == inspector_plot_id; });
    if (found != config.plots.end())
    {
        return *found;
    }

    PlotDefinition plot;
    plot.id = inspector_plot_id;
    plot.title = inspector_plot_title;
    plot.time_window_s = config.default_time_window_s;
    plot.height_px = 112.0F;
    config.plots.push_back(std::move(plot));
    result.created_plot = true;
    return config.plots.back();
}

PlotDefinition *existing_or_first_plot(PlotShelfConfig &config, PlotUiState &state)
{
    if (config.plots.empty())
    {
        return nullptr;
    }
    auto found = std::find_if(config.plots.begin(),
                              config.plots.end(),
                              [&state](const PlotDefinition &plot)
                              { return plot.id == state.selected_plot_id; });
    return found == config.plots.end() ? &config.plots.front() : &*found;
}

} // namespace

std::string mavlink_inspector_field_path(const std::string_view message_name,
                                         const std::string_view field_name)
{
    return "mavlink." + std::string(message_name) + "." + std::string(field_name);
}

std::optional<PlotSeriesDefinition>
make_mavlink_inspector_series(const SignalCatalog &catalog,
                              const std::string_view message_name,
                              const std::string_view field_name)
{
    SignalRef ref;
    ref.source = SignalSource::Mavlink;
    ref.field_path = mavlink_inspector_field_path(message_name, field_name);
    ref.mavlink_message = std::string(message_name);
    ref.mavlink_field = std::string(field_name);

    const SignalInfo *info = catalog.lookup(ref);
    if (info == nullptr || !info->numeric)
    {
        return std::nullopt;
    }

    PlotSeriesDefinition series;
    series.id = "mavlink_1";
    series.label = info->display_name;
    series.enabled = true;
    series.signal = info->ref;
    series.transform = info->default_transform;
    series.scale = 1.0;
    series.offset = 0.0;
    series.unit_override = info->unit;
    series.entity_binding = PlotEntityBindingMode::Selected;
    series.explicit_entity.reset();
    return series;
}

bool plot_series_matches_mavlink_field(const PlotSeriesDefinition &series,
                                       const std::string_view message_name,
                                       const std::string_view field_name,
                                       const PlotEntityBindingMode binding)
{
    return series.signal.source == SignalSource::Mavlink &&
           series.signal.mavlink_message == message_name && series.signal.mavlink_field == field_name &&
           series.entity_binding == binding;
}

MavlinkInspectorPlotResult plot_mavlink_inspector_field(PlotShelfConfig &config,
                                                        PlotUiState &state,
                                                        const SignalCatalog &catalog,
                                                        const std::string_view message_name,
                                                        const std::string_view field_name,
                                                        const MavlinkInspectorPlotTarget target)
{
    MavlinkInspectorPlotResult result;
    PlotDefinition *plot = nullptr;
    if (target == MavlinkInspectorPlotTarget::Dedicated)
    {
        plot = &dedicated_plot(config, result);
    }
    else
    {
        plot = existing_or_first_plot(config, state);
        if (plot == nullptr)
        {
            plot = &dedicated_plot(config, result);
        }
    }
    result.plot = plot;
    state.selected_plot_id = plot->id;

    auto existing = std::find_if(plot->series.begin(),
                                 plot->series.end(),
                                 [message_name, field_name](const PlotSeriesDefinition &series)
                                 {
                                     return plot_series_matches_mavlink_field(
                                         series,
                                         message_name,
                                         field_name,
                                         PlotEntityBindingMode::Selected);
                                 });
    if (existing != plot->series.end())
    {
        result.series = &*existing;
        state.selected_series_id = existing->id;
        return result;
    }

    std::optional<PlotSeriesDefinition> series =
        make_mavlink_inspector_series(catalog, message_name, field_name);
    if (!series)
    {
        return result;
    }
    series->id = next_series_id(*plot);
    plot->series.push_back(*series);
    result.created_series = true;
    result.series = &plot->series.back();
    state.selected_series_id = result.series->id;
    return result;
}

} // namespace animus::app
