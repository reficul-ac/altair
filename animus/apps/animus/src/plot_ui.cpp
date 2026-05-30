#include "plot_ui.hpp"

#include "options.hpp"
#include "ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>

namespace animus::app
{
namespace
{

const ImU32 series_colors[] = {
    IM_COL32(88, 166, 255, 255),
    IM_COL32(126, 231, 135, 255),
    IM_COL32(245, 189, 87, 255),
    IM_COL32(241, 112, 112, 255),
    IM_COL32(192, 132, 252, 255),
};

std::string series_key(const PlotDefinition &plot, const PlotSeriesDefinition &series)
{
    return plot.id + ":" + series.id;
}

PlotSeriesBuffer &buffer_for(PlotUiState &state,
                             const PlotDefinition &plot,
                             const PlotSeriesDefinition &series,
                             const PlotShelfConfig &config)
{
    const std::string key = series_key(plot, series);
    const auto found =
        std::find_if(state.buffers.begin(),
                     state.buffers.end(),
                     [&key](const PlotRuntimeSeries &runtime) { return runtime.key == key; });
    if (found != state.buffers.end())
    {
        found->buffer.set_config(
            {.time_window_s = plot.time_window_s, .max_points = config.max_points_per_series});
        return found->buffer;
    }
    PlotRuntimeSeries runtime;
    runtime.key = key;
    runtime.buffer.set_config(
        {.time_window_s = plot.time_window_s, .max_points = config.max_points_per_series});
    state.buffers.push_back(std::move(runtime));
    return state.buffers.back().buffer;
}

std::optional<PlotDefinition *> selected_plot(PlotShelfConfig &config, PlotUiState &state)
{
    if (config.plots.empty())
    {
        return std::nullopt;
    }
    auto found = std::find_if(config.plots.begin(),
                              config.plots.end(),
                              [&state](const PlotDefinition &plot)
                              { return plot.id == state.selected_plot_id; });
    if (found == config.plots.end())
    {
        state.selected_plot_id = config.plots.front().id;
        found = config.plots.begin();
    }
    return &*found;
}

std::optional<PlotSeriesDefinition *> selected_series(PlotDefinition &plot, PlotUiState &state)
{
    if (plot.series.empty())
    {
        return std::nullopt;
    }
    auto found = std::find_if(plot.series.begin(),
                              plot.series.end(),
                              [&state](const PlotSeriesDefinition &series)
                              { return series.id == state.selected_series_id; });
    if (found == plot.series.end())
    {
        state.selected_series_id = plot.series.front().id;
        found = plot.series.begin();
    }
    return &*found;
}

void copy_to_buffer(std::array<char, 128> &buffer, const std::string &value)
{
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

void copy_to_buffer(std::array<char, 96> &buffer, const std::string &value)
{
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

SignalSample evaluate_series(const SignalCatalog &catalog,
                             const PlotSeriesDefinition &series,
                             const TelemetryPlaybackState &playback,
                             const RuntimeSignalInputs &runtime)
{
    const double now_s = playback.live ? playback.timeline.end_time_s : playback.clock.time_s();
    SignalRef ref = series.signal;
    if (ref.source == SignalSource::Mavlink)
    {
        if (series.entity_binding == PlotEntityBindingMode::Explicit && series.explicit_entity)
        {
            ref.entity_id = series.explicit_entity;
        }
        else
        {
            ref.entity_id = playback.selected_entity;
        }
        SignalSample sample =
            catalog.extract_mavlink(ref, playback.mavlink_values, now_s, series.transform);
        if (sample.status == SignalSampleStatus::Valid)
        {
            sample.value = sample.value * series.scale + series.offset;
        }
        return sample;
    }
    if (ref.source == SignalSource::Runtime || ref.source == SignalSource::Derived)
    {
        SignalSample sample = catalog.extract_runtime(ref, runtime, now_s, series.transform);
        if (sample.status == SignalSampleStatus::Valid)
        {
            sample.value = sample.value * series.scale + series.offset;
        }
        return sample;
    }
    if (!playback.loaded)
    {
        return SignalSample{.time_s = now_s, .status = SignalSampleStatus::Unavailable};
    }
    const auto sample_at = playback.timeline.sample_at(playback.selected_entity, now_s);
    if (!sample_at)
    {
        return SignalSample{.time_s = now_s, .status = SignalSampleStatus::Unavailable};
    }
    SignalSample sample = catalog.extract_sample(ref, *sample_at, series.transform);
    if (sample.status == SignalSampleStatus::Valid)
    {
        sample.value = sample.value * series.scale + series.offset;
    }
    return sample;
}

void update_buffers(PlotShelfConfig &config,
                    TelemetryPlaybackState &playback,
                    const RuntimeSignalInputs &runtime,
                    PlotUiState &state)
{
    const double now_s = playback.live ? playback.timeline.end_time_s : playback.clock.time_s();
    if (!playback.live && state.have_previous_offline_time &&
        now_s + 1.0e-6 < state.previous_offline_time_s)
    {
        for (PlotRuntimeSeries &series : state.buffers)
        {
            series.buffer.clear();
        }
    }
    state.previous_offline_time_s = now_s;
    state.have_previous_offline_time = true;
    const SignalCatalog catalog;
    for (PlotDefinition &plot : config.plots)
    {
        if (!plot.enabled)
        {
            continue;
        }
        for (PlotSeriesDefinition &series : plot.series)
        {
            PlotSeriesBuffer &buffer = buffer_for(state, plot, series, config);
            buffer.set_paused(config.paused || !series.enabled);
            if (!series.enabled || config.paused)
            {
                continue;
            }
            buffer.append(evaluate_series(catalog, series, playback, runtime));
        }
    }
}

void draw_plot_canvas(const PlotDefinition &plot, PlotUiState &state, const PlotShelfConfig &config)
{
    ImGui::TextUnformatted(plot.title.c_str());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(80.0F, ImGui::GetContentRegionAvail().x);
    const float height = plot.height_px;
    ImGui::InvisibleButton(("plot_canvas_" + plot.id).c_str(), ImVec2(width, height));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + width, origin.y + height);
    draw->AddRectFilled(origin, max, IM_COL32(13, 17, 20, 235), 4.0F);
    draw->AddRect(origin, max, IM_COL32(66, 76, 84, 220), 4.0F);

    double x_max = -std::numeric_limits<double>::infinity();
    double y_min = std::numeric_limits<double>::infinity();
    double y_max = -std::numeric_limits<double>::infinity();
    std::vector<std::vector<PlotPoint>> draws;
    for (const PlotSeriesDefinition &series : plot.series)
    {
        const auto found = std::find_if(state.buffers.begin(),
                                        state.buffers.end(),
                                        [&plot, &series](const PlotRuntimeSeries &runtime)
                                        { return runtime.key == series_key(plot, series); });
        draws.push_back(found == state.buffers.end()
                            ? std::vector<PlotPoint>{}
                            : found->buffer.draw_points(config.render_max_points));
        for (const PlotPoint &point : draws.back())
        {
            x_max = std::max(x_max, point.time_s);
            y_min = std::min(y_min, point.value);
            y_max = std::max(y_max, point.value);
        }
    }
    if (!std::isfinite(x_max))
    {
        draw->AddText(ImVec2(origin.x + 10.0F, origin.y + 10.0F),
                      IM_COL32(170, 178, 186, 255),
                      "unavailable");
        return;
    }
    if (config.follow_latest)
    {
        state.frozen_latest_time_s.reset();
    }
    else if (!state.frozen_latest_time_s)
    {
        state.frozen_latest_time_s = x_max;
    }
    x_max = state.frozen_latest_time_s.value_or(x_max);
    if (!plot.y_auto && plot.y_min && plot.y_max && *plot.y_max > *plot.y_min)
    {
        y_min = *plot.y_min;
        y_max = *plot.y_max;
    }
    if (std::abs(y_max - y_min) < 1.0e-9)
    {
        y_min -= 1.0;
        y_max += 1.0;
    }
    const double x_min = x_max - plot.time_window_s;
    for (std::size_t series_index = 0; series_index < draws.size(); ++series_index)
    {
        const std::vector<PlotPoint> &points = draws[series_index];
        if (points.size() < 2U)
        {
            continue;
        }
        for (std::size_t index = 1; index < points.size(); ++index)
        {
            const auto map_point = [&](const PlotPoint &point)
            {
                const float x =
                    origin.x +
                    static_cast<float>((point.time_s - x_min) / plot.time_window_s) * width;
                const float y =
                    origin.y + height -
                    static_cast<float>((point.value - y_min) / (y_max - y_min)) * height;
                return ImVec2(std::clamp(x, origin.x, max.x), std::clamp(y, origin.y, max.y));
            };
            draw->AddLine(map_point(points[index - 1U]),
                          map_point(points[index]),
                          series_colors[series_index % std::size(series_colors)],
                          1.5F);
        }
    }
    char bounds[128]{};
    std::snprintf(bounds, sizeof(bounds), "%.2f .. %.2f", y_min, y_max);
    draw->AddText(ImVec2(origin.x + 8.0F, origin.y + 6.0F), IM_COL32(170, 178, 186, 255), bounds);
}

void draw_series_status(const PlotDefinition &plot, PlotUiState &state)
{
    for (std::size_t index = 0; index < plot.series.size(); ++index)
    {
        const PlotSeriesDefinition &series = plot.series[index];
        const auto found = std::find_if(state.buffers.begin(),
                                        state.buffers.end(),
                                        [&plot, &series](const PlotRuntimeSeries &runtime)
                                        { return runtime.key == series_key(plot, series); });
        const SignalSampleStatus status = found == state.buffers.end()
                                              ? SignalSampleStatus::Unavailable
                                              : found->buffer.latest_status();
        ImGui::TextColored(
            ImGui::ColorConvertU32ToFloat4(series_colors[index % std::size(series_colors)]),
            "%s",
            series.label.c_str());
        ImGui::SameLine();
        if (status == SignalSampleStatus::Valid && found != state.buffers.end())
        {
            ImGui::Text("%.3f", found->buffer.latest_value());
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.93F, 0.60F, 0.28F, 1.0F), "%s", SignalCatalog::status_name(status));
        }
        if (index + 1U < plot.series.size())
        {
            ImGui::SameLine();
        }
    }
}

std::string next_id(const std::string &prefix, const std::size_t count)
{
    return prefix + "_" + std::to_string(count + 1U);
}

void draw_plot_editor(Options &options, PlotUiState &state)
{
    PlotShelfConfig &config = options.plots;
    if (ImGui::Button("+ Add Plot"))
    {
        PlotDefinition plot;
        plot.id = next_id("plot", config.plots.size());
        plot.title = "Custom Plot";
        plot.time_window_s = config.default_time_window_s;
        plot.series.push_back(default_plot_shelf_config().plots.front().series.front());
        plot.series.front().id = "series_1";
        config.plots.push_back(plot);
        state.selected_plot_id = plot.id;
        state.selected_series_id = plot.series.front().id;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults"))
    {
        config = default_plot_shelf_config();
        state.buffers.clear();
        state.selected_plot_id.clear();
        state.selected_series_id.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear History"))
    {
        for (PlotRuntimeSeries &runtime : state.buffers)
        {
            runtime.buffer.clear();
        }
    }

    auto selected = selected_plot(config, state);
    if (!selected)
    {
        return;
    }
    PlotDefinition &plot = **selected;
    if (state.loaded_plot_id != plot.id)
    {
        copy_to_buffer(state.plot_title, plot.title);
        state.loaded_plot_id = plot.id;
    }

    ImGui::Separator();
    if (ImGui::BeginCombo("Plot", plot.title.c_str()))
    {
        for (PlotDefinition &candidate : config.plots)
        {
            if (ImGui::Selectable(candidate.title.c_str(), candidate.id == plot.id))
            {
                state.selected_plot_id = candidate.id;
                state.loaded_plot_id.clear();
                state.loaded_series_id.clear();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::InputText("Title", state.plot_title.data(), state.plot_title.size()))
    {
        plot.title = state.plot_title.data();
    }
    ImGui::Checkbox("Enabled", &plot.enabled);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate Plot"))
    {
        PlotDefinition copy = plot;
        copy.id = next_id(plot.id, config.plots.size());
        copy.title += " Copy";
        config.plots.push_back(copy);
        state.selected_plot_id = copy.id;
    }
    ImGui::SameLine();
    if (config.plots.size() <= 1U)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Plot"))
    {
        config.plots.erase(std::remove_if(config.plots.begin(),
                                          config.plots.end(),
                                          [&plot](const PlotDefinition &candidate)
                                          { return candidate.id == plot.id; }),
                           config.plots.end());
        state.selected_plot_id.clear();
        state.selected_series_id.clear();
        if (config.plots.size() < 1U)
        {
            config = default_plot_shelf_config();
        }
        if (config.plots.size() <= 1U)
        {
            ImGui::EndDisabled();
        }
        return;
    }
    if (config.plots.size() <= 1U)
    {
        ImGui::EndDisabled();
    }
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputDouble("Window s", &plot.time_window_s, 1.0, 10.0, "%.1f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputFloat("Height", &plot.height_px, 8.0F, 24.0F, "%.0f");
    plot.time_window_s = std::max(1.0, plot.time_window_s);
    plot.height_px = std::clamp(plot.height_px, 64.0F, 260.0F);
    ImGui::Checkbox("Auto Y", &plot.y_auto);
    if (!plot.y_auto)
    {
        double y_min = plot.y_min.value_or(-1.0);
        double y_max = plot.y_max.value_or(1.0);
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::InputDouble("Y min", &y_min))
        {
            plot.y_min = y_min;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::InputDouble("Y max", &y_max))
        {
            plot.y_max = y_max;
        }
    }

    if (ImGui::Button("+ Add Series"))
    {
        PlotSeriesDefinition series = default_plot_shelf_config().plots.front().series.front();
        series.id = next_id("series", plot.series.size());
        plot.series.push_back(series);
        state.selected_series_id = series.id;
        state.loaded_series_id.clear();
    }
    auto selected_series_ref = selected_series(plot, state);
    if (!selected_series_ref)
    {
        return;
    }
    PlotSeriesDefinition &series = **selected_series_ref;
    if (state.loaded_series_id != series.id)
    {
        copy_to_buffer(state.series_label, series.label);
        state.loaded_series_id = series.id;
    }
    if (ImGui::BeginCombo("Series", series.label.c_str()))
    {
        for (PlotSeriesDefinition &candidate : plot.series)
        {
            if (ImGui::Selectable(candidate.label.c_str(), candidate.id == series.id))
            {
                state.selected_series_id = candidate.id;
                state.loaded_series_id.clear();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::InputText("Label", state.series_label.data(), state.series_label.size()))
    {
        series.label = state.series_label.data();
    }
    ImGui::Checkbox("Series enabled", &series.enabled);
    ImGui::SameLine();
    if (plot.series.size() <= 1U)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Series"))
    {
        plot.series.erase(std::remove_if(plot.series.begin(),
                                         plot.series.end(),
                                         [&series](const PlotSeriesDefinition &candidate)
                                         { return candidate.id == series.id; }),
                          plot.series.end());
        state.selected_series_id.clear();
        if (plot.series.size() <= 1U)
        {
            ImGui::EndDisabled();
        }
        return;
    }
    if (plot.series.size() <= 1U)
    {
        ImGui::EndDisabled();
    }

    int source_index = static_cast<int>(series.signal.source);
    const char *sources[] = {"sample", "derived", "runtime", "mavlink"};
    if (ImGui::Combo("Source", &source_index, sources, std::size(sources)))
    {
        series.signal = {};
        series.signal.source = static_cast<SignalSource>(source_index);
    }
    ImGui::InputText("Search", state.signal_search.data(), state.signal_search.size());
    const SignalCatalog catalog;
    const std::string needle = state.signal_search.data();
    const std::string current =
        series.signal.source == SignalSource::Mavlink
            ? series.signal.mavlink_message + "." + series.signal.mavlink_field
            : series.signal.field_path;
    if (ImGui::BeginCombo("Field", current.empty() ? "choose field" : current.c_str()))
    {
        for (const SignalInfo &signal : catalog.signals())
        {
            if (signal.ref.source != series.signal.source)
            {
                continue;
            }
            const std::string label =
                signal.ref.source == SignalSource::Mavlink
                    ? signal.ref.mavlink_message + "." + signal.ref.mavlink_field
                    : signal.ref.field_path;
            if (!needle.empty() && label.find(needle) == std::string::npos &&
                signal.display_name.find(needle) == std::string::npos)
            {
                continue;
            }
            if (ImGui::Selectable((label + " - " + signal.display_name).c_str()))
            {
                series.signal = signal.ref;
                series.transform = signal.default_transform;
                series.label = signal.display_name;
                series.unit_override = signal.unit;
                copy_to_buffer(state.series_label, series.label);
            }
        }
        ImGui::EndCombo();
    }
    int transform_index = static_cast<int>(series.transform);
    const char *transforms[] = {"none",
                                "rad_to_deg",
                                "deg_to_rad",
                                "meters_to_feet",
                                "mps_to_kts",
                                "mps_to_mph",
                                "abs",
                                "negate"};
    if (ImGui::Combo("Transform", &transform_index, transforms, std::size(transforms)))
    {
        series.transform = static_cast<SignalTransform>(transform_index);
    }
    ImGui::InputDouble("Scale", &series.scale, 0.1, 1.0, "%.4f");
    ImGui::InputDouble("Offset", &series.offset, 0.1, 1.0, "%.4f");
    char unit[32]{};
    std::snprintf(unit, sizeof(unit), "%s", series.unit_override.c_str());
    if (ImGui::InputText("Unit", unit, sizeof(unit)))
    {
        series.unit_override = unit;
    }
    if (series.signal.source == SignalSource::Mavlink)
    {
        int binding = series.entity_binding == PlotEntityBindingMode::Explicit ? 1 : 0;
        const char *bindings[] = {"selected", "explicit"};
        if (ImGui::Combo("Entity", &binding, bindings, std::size(bindings)))
        {
            series.entity_binding =
                binding == 1 ? PlotEntityBindingMode::Explicit : PlotEntityBindingMode::Selected;
        }
        if (series.entity_binding == PlotEntityBindingMode::Explicit)
        {
            int sysid = series.explicit_entity ? series.explicit_entity->system_id : 1;
            int component = series.explicit_entity ? series.explicit_entity->component_id : 1;
            ImGui::InputInt("Sysid", &sysid);
            ImGui::InputInt("Component", &component);
            sysid = std::clamp(sysid, 1, 255);
            component = std::clamp(component, 0, 255);
            series.explicit_entity = animus::telemetry_core::EntityId{
                static_cast<std::uint8_t>(sysid), static_cast<std::uint8_t>(component)};
        }
    }
}

} // namespace

void draw_plot_shelf(Options &options,
                     TelemetryPlaybackState &playback,
                     const RuntimeSignalInputs &runtime,
                     PlotUiState &state,
                     AppWindowRect &rect,
                     const bool restore_rect)
{
    PlotShelfConfig &config = options.plots;
    update_buffers(config, playback, runtime, state);
    if (!config.visible)
    {
        return;
    }

    const float left = 12.0F + 128.0F + 10.0F;
    const float right_reserve = ImGui::GetIO().DisplaySize.x >= 980.0F ? 316.0F + 36.0F : 24.0F;
    const float width = ImGui::GetIO().DisplaySize.x - left - right_reserve;
    if (width < 320.0F)
    {
        return;
    }
    const float timeline_reserve = playback.loaded ? 118.0F : 12.0F;
    config.height_px = std::clamp(config.height_px, 104.0F, ImGui::GetIO().DisplaySize.y * 0.55F);
    const AppWindowRect fallback{left,
                                 ImGui::GetIO().DisplaySize.y - timeline_reserve - config.height_px,
                                 width,
                                 config.height_px};
    AppWindowRect requested = rect.width > 0.0F && rect.height > 0.0F ? rect : fallback;
    requested.width =
        std::clamp(requested.width, 320.0F, std::max(320.0F, ImGui::GetIO().DisplaySize.x - 24.0F));
    requested.height = std::clamp(requested.height, 104.0F, ImGui::GetIO().DisplaySize.y * 0.55F);
    requested.x = std::clamp(
        requested.x, 0.0F, std::max(0.0F, ImGui::GetIO().DisplaySize.x - requested.width));
    requested.y = std::clamp(
        requested.y, 38.0F, std::max(38.0F, ImGui::GetIO().DisplaySize.y - requested.height));
    const ImGuiCond rect_condition = restore_rect ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImVec2(requested.x, requested.y), rect_condition);
    ImGui::SetNextWindowSize(ImVec2(requested.width, requested.height), rect_condition);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Plot Shelf", &config.visible, flags);
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    rect = {pos.x, pos.y, size.x, size.y};
    config.height_px = size.y;
    ImGui::Checkbox("Paused", &config.paused);
    ImGui::SameLine();
    ImGui::Checkbox("Follow latest", &config.follow_latest);
    ImGui::SameLine();
    if (ImGui::Button(config.collapsed ? "Expand" : "Collapse"))
    {
        config.collapsed = !config.collapsed;
    }
    if (config.collapsed)
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("plot_shelf_tabs"))
    {
        if (ImGui::BeginTabItem("Plots"))
        {
            for (const PlotDefinition &plot : config.plots)
            {
                if (!plot.enabled)
                {
                    continue;
                }
                draw_plot_canvas(plot, state, config);
                draw_series_status(plot, state);
                ImGui::Spacing();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Editor"))
        {
            draw_plot_editor(options, state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace animus::app
