#pragma once

#include "telemetry_signal_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace YAML
{
class Emitter;
class Node;
} // namespace YAML

namespace animus::app
{

enum class PlotEntityBindingMode
{
    Selected,
    Explicit,
};

struct PlotSeriesDefinition
{
    std::string id;
    std::string label;
    bool enabled = true;
    SignalRef signal;
    SignalTransform transform = SignalTransform::None;
    double scale = 1.0;
    double offset = 0.0;
    std::string unit_override;
    PlotEntityBindingMode entity_binding = PlotEntityBindingMode::Selected;
    std::optional<animus::telemetry_core::EntityId> explicit_entity;

    bool operator==(const PlotSeriesDefinition &) const = default;
};

struct PlotDefinition
{
    std::string id;
    std::string title;
    bool enabled = true;
    double time_window_s = 30.0;
    float height_px = 112.0F;
    bool y_auto = true;
    std::optional<double> y_min;
    std::optional<double> y_max;
    std::vector<PlotSeriesDefinition> series;

    bool operator==(const PlotDefinition &) const = default;
};

struct PlotShelfConfig
{
    bool visible = true;
    bool collapsed = false;
    bool paused = false;
    bool follow_latest = true;
    double default_time_window_s = 30.0;
    std::size_t max_points_per_series = 2048U;
    std::size_t render_max_points = 360U;
    float height_px = 260.0F;
    std::vector<PlotDefinition> plots;

    bool operator==(const PlotShelfConfig &) const = default;
};

[[nodiscard]] PlotShelfConfig default_plot_shelf_config();
[[nodiscard]] std::vector<std::string> validate_plot_shelf_config(const PlotShelfConfig &config,
                                                                  const SignalCatalog &catalog);
[[nodiscard]] const char *plot_entity_binding_name(PlotEntityBindingMode mode);
[[nodiscard]] std::optional<PlotEntityBindingMode> parse_plot_entity_binding(std::string_view name);
[[nodiscard]] std::optional<SignalSource> parse_signal_source(std::string_view name);
[[nodiscard]] std::optional<SignalTransform> parse_signal_transform(std::string_view name);

void write_plot_shelf_config(YAML::Emitter &out, const PlotShelfConfig &config);
[[nodiscard]] PlotShelfConfig read_plot_shelf_config(const YAML::Node &node,
                                                     std::vector<std::string> &diagnostics);

} // namespace animus::app
