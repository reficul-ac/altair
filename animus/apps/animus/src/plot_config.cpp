#include "plot_config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>

namespace animus::app
{
namespace
{

PlotSeriesDefinition make_series(std::string id,
                                 std::string label,
                                 SignalSource source,
                                 std::string field,
                                 SignalTransform transform,
                                 std::string unit = {})
{
    PlotSeriesDefinition series;
    series.id = std::move(id);
    series.label = std::move(label);
    series.signal.source = source;
    series.signal.field_path = std::move(field);
    series.transform = transform;
    series.unit_override = std::move(unit);
    return series;
}

PlotDefinition
make_plot(std::string id, std::string title, std::vector<PlotSeriesDefinition> series)
{
    PlotDefinition plot;
    plot.id = std::move(id);
    plot.title = std::move(title);
    plot.series = std::move(series);
    return plot;
}

void warn_unknown_keys(const YAML::Node &node,
                       const std::unordered_set<std::string> &known,
                       const std::string &prefix,
                       std::vector<std::string> &diagnostics)
{
    if (!node || !node.IsMap())
    {
        return;
    }
    for (const auto &entry : node)
    {
        const std::string key = entry.first.as<std::string>();
        if (known.find(key) == known.end())
        {
            diagnostics.push_back("unknown config key: " + prefix + key);
        }
    }
}

template <typename T>
void read_value(const YAML::Node &node,
                const char *key,
                T &target,
                std::vector<std::string> &diagnostics,
                const std::string &prefix)
{
    if (!node || !node[key])
    {
        return;
    }
    try
    {
        target = node[key].as<T>();
    }
    catch (const YAML::Exception &error)
    {
        diagnostics.push_back("invalid config value for " + prefix + key + ": " + error.what());
    }
}

template <typename T>
void write_optional(YAML::Emitter &out, const char *key, const std::optional<T> &value)
{
    if (value)
    {
        out << YAML::Key << key << YAML::Value << *value;
    }
}

std::optional<animus::telemetry_core::EntityId> read_entity(const YAML::Node &node,
                                                            std::vector<std::string> &diagnostics,
                                                            const std::string &prefix)
{
    if (!node)
    {
        return std::nullopt;
    }
    if (!node.IsMap())
    {
        diagnostics.push_back("invalid config value for " + prefix + ": expected map");
        return std::nullopt;
    }
    int system_id = 0;
    int component_id = 0;
    read_value(node, "sysid", system_id, diagnostics, prefix + ".");
    read_value(node, "component", component_id, diagnostics, prefix + ".");
    if (system_id <= 0 || system_id > 255 || component_id < 0 || component_id > 255)
    {
        diagnostics.push_back("invalid config value for " + prefix +
                              ": expected sysid/component in MAVLink byte range");
        return std::nullopt;
    }
    return animus::telemetry_core::EntityId{static_cast<std::uint8_t>(system_id),
                                            static_cast<std::uint8_t>(component_id)};
}

void write_entity(YAML::Emitter &out,
                  const char *key,
                  const std::optional<animus::telemetry_core::EntityId> &entity)
{
    if (!entity)
    {
        return;
    }
    out << YAML::Key << key << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "sysid" << YAML::Value << static_cast<int>(entity->system_id);
    out << YAML::Key << "component" << YAML::Value << static_cast<int>(entity->component_id);
    out << YAML::EndMap;
}

void write_signal(YAML::Emitter &out, const SignalRef &signal)
{
    out << YAML::Key << "signal" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "source" << YAML::Value << SignalCatalog::source_name(signal.source);
    if (signal.source == SignalSource::Mavlink)
    {
        out << YAML::Key << "message" << YAML::Value << signal.mavlink_message;
        out << YAML::Key << "field" << YAML::Value << signal.mavlink_field;
    }
    else
    {
        out << YAML::Key << "field" << YAML::Value << signal.field_path;
    }
    write_entity(out, "entity", signal.entity_id);
    out << YAML::EndMap;
}

std::optional<SignalRef> read_signal(const YAML::Node &node,
                                     std::vector<std::string> &diagnostics,
                                     const std::string &prefix)
{
    if (!node || !node.IsMap())
    {
        diagnostics.push_back("invalid config value for " + prefix + ": expected map");
        return std::nullopt;
    }
    SignalRef signal;
    std::string source_name = "sample";
    read_value(node, "source", source_name, diagnostics, prefix + ".");
    const auto source = parse_signal_source(source_name);
    if (!source)
    {
        diagnostics.push_back("invalid config value for " + prefix + ".source: " + source_name);
        return std::nullopt;
    }
    signal.source = *source;
    if (signal.source == SignalSource::Mavlink)
    {
        read_value(node, "message", signal.mavlink_message, diagnostics, prefix + ".");
        read_value(node, "field", signal.mavlink_field, diagnostics, prefix + ".");
        if (mavlink_message_id(signal.mavlink_message) == UINT32_MAX ||
            signal.mavlink_field.empty())
        {
            diagnostics.push_back("invalid config value for " + prefix +
                                  ": unknown MAVLink message or field");
            return std::nullopt;
        }
    }
    else
    {
        read_value(node, "field", signal.field_path, diagnostics, prefix + ".");
        if (signal.field_path.empty())
        {
            diagnostics.push_back("invalid config value for " + prefix + ".field: missing field");
            return std::nullopt;
        }
    }
    signal.entity_id = read_entity(node["entity"], diagnostics, prefix + ".entity");
    return signal;
}

std::optional<PlotSeriesDefinition> read_series(const YAML::Node &node,
                                                std::vector<std::string> &diagnostics,
                                                const std::string &prefix)
{
    if (!node || !node.IsMap())
    {
        diagnostics.push_back("invalid config value for " + prefix + ": expected map");
        return std::nullopt;
    }
    warn_unknown_keys(node,
                      {"id",
                       "label",
                       "enabled",
                       "signal",
                       "transform",
                       "scale",
                       "offset",
                       "unit",
                       "entity_binding",
                       "explicit_entity"},
                      prefix + ".",
                      diagnostics);
    PlotSeriesDefinition series;
    read_value(node, "id", series.id, diagnostics, prefix + ".");
    read_value(node, "label", series.label, diagnostics, prefix + ".");
    read_value(node, "enabled", series.enabled, diagnostics, prefix + ".");
    const auto signal = read_signal(node["signal"], diagnostics, prefix + ".signal");
    if (!signal)
    {
        return std::nullopt;
    }
    series.signal = *signal;
    std::string transform_name = SignalCatalog::transform_name(series.transform);
    read_value(node, "transform", transform_name, diagnostics, prefix + ".");
    const auto transform = parse_signal_transform(transform_name);
    if (!transform)
    {
        diagnostics.push_back("invalid config value for " + prefix +
                              ".transform: " + transform_name);
        return std::nullopt;
    }
    series.transform = *transform;
    read_value(node, "scale", series.scale, diagnostics, prefix + ".");
    read_value(node, "offset", series.offset, diagnostics, prefix + ".");
    read_value(node, "unit", series.unit_override, diagnostics, prefix + ".");
    std::string binding_name = plot_entity_binding_name(series.entity_binding);
    read_value(node, "entity_binding", binding_name, diagnostics, prefix + ".");
    const auto binding = parse_plot_entity_binding(binding_name);
    if (!binding)
    {
        diagnostics.push_back("invalid config value for " + prefix +
                              ".entity_binding: " + binding_name);
        return std::nullopt;
    }
    series.entity_binding = *binding;
    series.explicit_entity =
        read_entity(node["explicit_entity"], diagnostics, prefix + ".explicit_entity");
    if (series.id.empty())
    {
        series.id = "series";
    }
    if (series.label.empty())
    {
        series.label = series.signal.source == SignalSource::Mavlink ? series.signal.mavlink_field
                                                                     : series.signal.field_path;
    }
    return series;
}

std::optional<PlotDefinition>
read_plot(const YAML::Node &node, std::vector<std::string> &diagnostics, const std::string &prefix)
{
    if (!node || !node.IsMap())
    {
        diagnostics.push_back("invalid config value for " + prefix + ": expected map");
        return std::nullopt;
    }
    warn_unknown_keys(node,
                      {"id",
                       "title",
                       "enabled",
                       "time_window_s",
                       "height_px",
                       "y_auto",
                       "y_min",
                       "y_max",
                       "series"},
                      prefix + ".",
                      diagnostics);
    PlotDefinition plot;
    read_value(node, "id", plot.id, diagnostics, prefix + ".");
    read_value(node, "title", plot.title, diagnostics, prefix + ".");
    read_value(node, "enabled", plot.enabled, diagnostics, prefix + ".");
    read_value(node, "time_window_s", plot.time_window_s, diagnostics, prefix + ".");
    read_value(node, "height_px", plot.height_px, diagnostics, prefix + ".");
    read_value(node, "y_auto", plot.y_auto, diagnostics, prefix + ".");
    if (node["y_min"])
    {
        plot.y_min = node["y_min"].as<double>();
    }
    if (node["y_max"])
    {
        plot.y_max = node["y_max"].as<double>();
    }
    if (plot.id.empty())
    {
        plot.id = "plot";
    }
    if (plot.title.empty())
    {
        plot.title = plot.id;
    }
    if (plot.time_window_s <= 0.0 || plot.height_px <= 0.0F)
    {
        diagnostics.push_back("invalid config value for " + prefix +
                              ": expected positive time_window_s and height_px");
        return std::nullopt;
    }
    const YAML::Node series_node = node["series"];
    if (!series_node || !series_node.IsSequence())
    {
        diagnostics.push_back("invalid config value for " + prefix + ".series: expected sequence");
        return std::nullopt;
    }
    for (std::size_t index = 0; index < series_node.size(); ++index)
    {
        if (auto series = read_series(
                series_node[index], diagnostics, prefix + ".series[" + std::to_string(index) + "]"))
        {
            plot.series.push_back(std::move(*series));
        }
    }
    if (plot.series.empty())
    {
        diagnostics.push_back("invalid config value for " + prefix + ": no valid series");
        return std::nullopt;
    }
    return plot;
}

} // namespace

PlotShelfConfig default_plot_shelf_config()
{
    PlotShelfConfig config;
    config.plots = {
        make_plot("altitude_clearance",
                  "Altitude / Clearance",
                  {make_series("msl_altitude",
                               "MSL altitude",
                               SignalSource::Sample,
                               "altitude_msl_m",
                               SignalTransform::None,
                               "m"),
                   make_series("relative_altitude",
                               "Relative altitude",
                               SignalSource::Sample,
                               "altitude_relative_m",
                               SignalTransform::None,
                               "m"),
                   make_series("terrain_clearance",
                               "Terrain clearance",
                               SignalSource::Derived,
                               "terrain_clearance_m",
                               SignalTransform::None,
                               "m")}),
        make_plot("speed_climb",
                  "Speed / Climb",
                  {make_series("ground_speed",
                               "Ground speed",
                               SignalSource::Sample,
                               "ground_speed_mps",
                               SignalTransform::None,
                               "m/s"),
                   make_series("climb_rate",
                               "Climb rate",
                               SignalSource::Sample,
                               "climb_rate_mps",
                               SignalTransform::None,
                               "m/s")}),
        make_plot(
            "attitude",
            "Attitude",
            {make_series("roll",
                         "Roll",
                         SignalSource::Sample,
                         "roll_rad",
                         SignalTransform::RadToDeg,
                         "deg"),
             make_series("pitch",
                         "Pitch",
                         SignalSource::Sample,
                         "pitch_rad",
                         SignalTransform::RadToDeg,
                         "deg"),
             make_series(
                 "yaw", "Yaw", SignalSource::Sample, "yaw_rad", SignalTransform::RadToDeg, "deg")}),
        make_plot("link_quality",
                  "Link Quality",
                  {make_series("link_hz",
                               "Link Hz",
                               SignalSource::Runtime,
                               "link_hz",
                               SignalTransform::None,
                               "Hz"),
                   make_series("telemetry_age",
                               "Telemetry age",
                               SignalSource::Runtime,
                               "telemetry_age_s",
                               SignalTransform::None,
                               "s"),
                   make_series("telemetry_gap",
                               "Telemetry gap",
                               SignalSource::Runtime,
                               "telemetry_gap_s",
                               SignalTransform::None,
                               "s")}),
        make_plot("render_health",
                  "Render Health",
                  {make_series("frame_time",
                               "Frame time",
                               SignalSource::Runtime,
                               "frame_time_ms",
                               SignalTransform::None,
                               "ms"),
                   make_series("resident_tiles",
                               "Resident tiles",
                               SignalSource::Runtime,
                               "resident_tile_count",
                               SignalTransform::None,
                               "")}),
    };
    return config;
}

std::vector<std::string> validate_plot_shelf_config(const PlotShelfConfig &config,
                                                    const SignalCatalog &catalog)
{
    std::vector<std::string> diagnostics;
    if (config.default_time_window_s <= 0.0)
    {
        diagnostics.push_back("plots.default_time_window_s must be positive");
    }
    if (config.max_points_per_series == 0U || config.render_max_points == 0U)
    {
        diagnostics.push_back("plots point limits must be positive");
    }
    for (const PlotDefinition &plot : config.plots)
    {
        if (plot.id.empty() || plot.time_window_s <= 0.0 || plot.height_px <= 0.0F)
        {
            diagnostics.push_back("plot has invalid id or dimensions");
        }
        for (const PlotSeriesDefinition &series : plot.series)
        {
            if (catalog.lookup(series.signal) == nullptr)
            {
                diagnostics.push_back("unsupported plot signal: " + series.label);
            }
        }
    }
    return diagnostics;
}

const char *plot_entity_binding_name(const PlotEntityBindingMode mode)
{
    switch (mode)
    {
    case PlotEntityBindingMode::Selected:
        return "selected";
    case PlotEntityBindingMode::Explicit:
        return "explicit";
    }
    return "selected";
}

std::optional<PlotEntityBindingMode> parse_plot_entity_binding(const std::string_view name)
{
    if (name == "selected")
    {
        return PlotEntityBindingMode::Selected;
    }
    if (name == "explicit")
    {
        return PlotEntityBindingMode::Explicit;
    }
    return std::nullopt;
}

std::optional<SignalSource> parse_signal_source(const std::string_view name)
{
    if (name == "sample")
    {
        return SignalSource::Sample;
    }
    if (name == "derived")
    {
        return SignalSource::Derived;
    }
    if (name == "runtime")
    {
        return SignalSource::Runtime;
    }
    if (name == "mavlink")
    {
        return SignalSource::Mavlink;
    }
    return std::nullopt;
}

std::optional<SignalTransform> parse_signal_transform(const std::string_view name)
{
    for (const SignalTransform transform : {SignalTransform::None,
                                            SignalTransform::RadToDeg,
                                            SignalTransform::DegToRad,
                                            SignalTransform::MetersToFeet,
                                            SignalTransform::MpsToKts,
                                            SignalTransform::MpsToMph,
                                            SignalTransform::Abs,
                                            SignalTransform::Negate})
    {
        if (name == SignalCatalog::transform_name(transform))
        {
            return transform;
        }
    }
    return std::nullopt;
}

void write_plot_shelf_config(YAML::Emitter &out, const PlotShelfConfig &config)
{
    out << YAML::BeginMap;
    out << YAML::Key << "visible" << YAML::Value << config.visible;
    out << YAML::Key << "collapsed" << YAML::Value << config.collapsed;
    out << YAML::Key << "paused" << YAML::Value << config.paused;
    out << YAML::Key << "follow_latest" << YAML::Value << config.follow_latest;
    out << YAML::Key << "default_time_window_s" << YAML::Value << config.default_time_window_s;
    out << YAML::Key << "max_points_per_series" << YAML::Value << config.max_points_per_series;
    out << YAML::Key << "render_max_points" << YAML::Value << config.render_max_points;
    out << YAML::Key << "height_px" << YAML::Value << config.height_px;
    out << YAML::Key << "plots" << YAML::Value << YAML::BeginSeq;
    for (const PlotDefinition &plot : config.plots)
    {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << plot.id;
        out << YAML::Key << "title" << YAML::Value << plot.title;
        out << YAML::Key << "enabled" << YAML::Value << plot.enabled;
        out << YAML::Key << "time_window_s" << YAML::Value << plot.time_window_s;
        out << YAML::Key << "height_px" << YAML::Value << plot.height_px;
        out << YAML::Key << "y_auto" << YAML::Value << plot.y_auto;
        write_optional(out, "y_min", plot.y_min);
        write_optional(out, "y_max", plot.y_max);
        out << YAML::Key << "series" << YAML::Value << YAML::BeginSeq;
        for (const PlotSeriesDefinition &series : plot.series)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << series.id;
            out << YAML::Key << "label" << YAML::Value << series.label;
            out << YAML::Key << "enabled" << YAML::Value << series.enabled;
            write_signal(out, series.signal);
            out << YAML::Key << "transform" << YAML::Value
                << SignalCatalog::transform_name(series.transform);
            out << YAML::Key << "scale" << YAML::Value << series.scale;
            out << YAML::Key << "offset" << YAML::Value << series.offset;
            out << YAML::Key << "unit" << YAML::Value << series.unit_override;
            out << YAML::Key << "entity_binding" << YAML::Value
                << plot_entity_binding_name(series.entity_binding);
            write_entity(out, "explicit_entity", series.explicit_entity);
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
}

PlotShelfConfig read_plot_shelf_config(const YAML::Node &node,
                                       std::vector<std::string> &diagnostics)
{
    PlotShelfConfig config = default_plot_shelf_config();
    if (!node || node.IsNull())
    {
        return config;
    }
    if (!node.IsMap())
    {
        diagnostics.push_back("invalid config value for plots: expected map");
        return default_plot_shelf_config();
    }
    warn_unknown_keys(node,
                      {"visible",
                       "collapsed",
                       "paused",
                       "follow_latest",
                       "default_time_window_s",
                       "max_points_per_series",
                       "render_max_points",
                       "height_px",
                       "plots"},
                      "plots.",
                      diagnostics);
    read_value(node, "visible", config.visible, diagnostics, "plots.");
    read_value(node, "collapsed", config.collapsed, diagnostics, "plots.");
    read_value(node, "paused", config.paused, diagnostics, "plots.");
    read_value(node, "follow_latest", config.follow_latest, diagnostics, "plots.");
    read_value(node, "default_time_window_s", config.default_time_window_s, diagnostics, "plots.");
    read_value(node, "max_points_per_series", config.max_points_per_series, diagnostics, "plots.");
    read_value(node, "render_max_points", config.render_max_points, diagnostics, "plots.");
    read_value(node, "height_px", config.height_px, diagnostics, "plots.");
    if (config.default_time_window_s <= 0.0 || config.max_points_per_series == 0U ||
        config.render_max_points == 0U || config.height_px <= 0.0F)
    {
        diagnostics.push_back("invalid config value for plots: expected positive limits");
        return default_plot_shelf_config();
    }
    if (const YAML::Node plots = node["plots"])
    {
        if (!plots.IsSequence())
        {
            diagnostics.push_back("invalid config value for plots.plots: expected sequence");
            return default_plot_shelf_config();
        }
        config.plots.clear();
        for (std::size_t index = 0; index < plots.size(); ++index)
        {
            if (auto plot = read_plot(
                    plots[index], diagnostics, "plots.plots[" + std::to_string(index) + "]"))
            {
                config.plots.push_back(std::move(*plot));
            }
        }
        if (config.plots.empty())
        {
            diagnostics.push_back("invalid config value for plots: no valid plot definitions");
            return default_plot_shelf_config();
        }
    }
    const SignalCatalog catalog;
    const std::vector<std::string> validation = validate_plot_shelf_config(config, catalog);
    if (!validation.empty())
    {
        diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
        return default_plot_shelf_config();
    }
    return config;
}

} // namespace animus::app
