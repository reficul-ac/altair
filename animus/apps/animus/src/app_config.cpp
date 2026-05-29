#include "app_config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>

namespace animus::app
{
namespace
{

std::string trim_left(std::string text)
{
    text.erase(text.begin(),
               std::find_if(text.begin(),
                            text.end(),
                            [](const unsigned char ch) { return std::isspace(ch) == 0; }));
    return text;
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
                std::vector<std::string> &diagnostics)
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
        diagnostics.push_back(std::string("invalid config value for ") + key + ": " + error.what());
    }
}

void read_positive_int(const YAML::Node &node,
                       const char *key,
                       int &target,
                       std::vector<std::string> &diagnostics)
{
    int value = target;
    read_value(node, key, value, diagnostics);
    if (value <= 0)
    {
        diagnostics.push_back(std::string("invalid config value for ") + key +
                              ": expected positive integer");
        return;
    }
    target = value;
}

void read_positive_size(const YAML::Node &node,
                        const char *key,
                        std::size_t &target,
                        std::vector<std::string> &diagnostics)
{
    std::size_t value = target;
    read_value(node, key, value, diagnostics);
    if (value == 0U)
    {
        diagnostics.push_back(std::string("invalid config value for ") + key +
                              ": expected positive integer");
        return;
    }
    target = value;
}

void read_positive_double(const YAML::Node &node,
                          const char *key,
                          double &target,
                          std::vector<std::string> &diagnostics)
{
    double value = target;
    read_value(node, key, value, diagnostics);
    if (value <= 0.0)
    {
        diagnostics.push_back(std::string("invalid config value for ") + key +
                              ": expected positive number");
        return;
    }
    target = value;
}

void read_unit_float(const YAML::Node &node,
                     const char *key,
                     float &target,
                     std::vector<std::string> &diagnostics)
{
    float value = target;
    read_value(node, key, value, diagnostics);
    if (value < 0.0F || value > 1.0F)
    {
        diagnostics.push_back(std::string("invalid config value for ") + key +
                              ": expected value in 0..1");
        return;
    }
    target = value;
}

void read_udp_port(const YAML::Node &node,
                   const char *key,
                   std::uint16_t &target,
                   std::vector<std::string> &diagnostics)
{
    int value = target;
    read_value(node, key, value, diagnostics);
    if (value <= 0 || value > 65535)
    {
        diagnostics.push_back(std::string("invalid config value for ") + key +
                              ": expected UDP port 1..65535");
        return;
    }
    target = static_cast<std::uint16_t>(value);
}

std::optional<std::string> json_string_field(const std::string &text, const std::string &field)
{
    const std::string key = "\"" + field + "\"";
    const std::size_t key_pos = text.find(key);
    if (key_pos == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
    const std::size_t first_quote = text.find('"', colon == std::string::npos ? key_pos : colon);
    const std::size_t second_quote =
        first_quote == std::string::npos ? std::string::npos : text.find('"', first_quote + 1U);
    if (first_quote == std::string::npos || second_quote == std::string::npos)
    {
        return std::nullopt;
    }
    return text.substr(first_quote + 1U, second_quote - first_quote - 1U);
}

std::optional<float> json_float_field(const std::string &text, const std::string &field)
{
    const std::string key = "\"" + field + "\"";
    const std::size_t key_pos = text.find(key);
    const std::size_t colon =
        key_pos == std::string::npos ? std::string::npos : text.find(':', key_pos + key.size());
    if (colon == std::string::npos)
    {
        return std::nullopt;
    }
    try
    {
        return std::stof(text.substr(colon + 1U));
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

std::optional<int> json_int_field(const std::string &text, const std::string &field)
{
    const std::string key = "\"" + field + "\"";
    const std::size_t key_pos = text.find(key);
    const std::size_t colon =
        key_pos == std::string::npos ? std::string::npos : text.find(':', key_pos + key.size());
    if (colon == std::string::npos)
    {
        return std::nullopt;
    }
    try
    {
        return std::stoi(text.substr(colon + 1U));
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

std::optional<bool> json_bool_field(const std::string &text, const std::string &field)
{
    const std::string key = "\"" + field + "\"";
    const std::size_t key_pos = text.find(key);
    const std::size_t colon =
        key_pos == std::string::npos ? std::string::npos : text.find(':', key_pos + key.size());
    if (colon == std::string::npos)
    {
        return std::nullopt;
    }
    const std::string value = trim_left(text.substr(colon + 1U));
    if (value.rfind("true", 0U) == 0U)
    {
        return true;
    }
    if (value.rfind("false", 0U) == 0U)
    {
        return false;
    }
    return std::nullopt;
}

std::vector<AppConfigOverlay> json_overlay_layers(const std::string &text)
{
    const std::string key = "\"overlays\"";
    const std::size_t key_pos = text.find(key);
    const std::size_t array_begin =
        key_pos == std::string::npos ? std::string::npos : text.find('[', key_pos + key.size());
    const std::size_t array_end =
        array_begin == std::string::npos ? std::string::npos : text.find(']', array_begin + 1U);
    if (array_begin == std::string::npos || array_end == std::string::npos)
    {
        return {};
    }

    std::vector<AppConfigOverlay> layers;
    std::size_t cursor = array_begin + 1U;
    while (cursor < array_end)
    {
        const std::size_t object_begin = text.find('{', cursor);
        if (object_begin == std::string::npos || object_begin >= array_end)
        {
            break;
        }
        const std::size_t object_end = text.find('}', object_begin + 1U);
        if (object_end == std::string::npos || object_end > array_end)
        {
            break;
        }
        const std::string object = text.substr(object_begin, object_end - object_begin + 1U);
        AppConfigOverlay layer;
        if (const auto value = json_string_field(object, "path"))
        {
            layer.path = *value;
        }
        if (const auto value = json_bool_field(object, "enabled"))
        {
            layer.enabled = *value;
        }
        if (const auto value = json_float_field(object, "opacity"))
        {
            layer.opacity = std::clamp(*value, 0.0F, 1.0F);
        }
        if (const auto value = json_int_field(object, "draw_order"))
        {
            layer.draw_order = *value;
        }
        if (const auto value = json_string_field(object, "cache_identity"))
        {
            layer.cache_identity = *value;
        }
        if (!layer.path.empty())
        {
            layers.push_back(std::move(layer));
        }
        cursor = object_end + 1U;
    }
    return layers;
}

AppConfigLoadResult load_legacy_json_like(const std::string &text)
{
    AppConfigLoadResult result;
    result.status = AppConfigLoadStatus::LoadedLegacy;
    if (const auto value = json_string_field(text, "workspace_mode"))
    {
        result.config.workspace_mode = *value;
    }
    if (const auto value = json_string_field(text, "view_mode"))
    {
        result.config.view_mode = *value;
    }
    if (const auto value = json_bool_field(text, "overlay_enabled"))
    {
        result.config.overlay_enabled = *value;
    }
    if (const auto value = json_float_field(text, "overlay_opacity"))
    {
        result.config.overlay_opacity = std::clamp(*value, 0.0F, 1.0F);
    }
    result.config.overlays = json_overlay_layers(text);
    result.diagnostics.push_back("loaded legacy JSON-like config; future saves use YAML");
    return result;
}

void read_overlays(const YAML::Node &node,
                   std::vector<AppConfigOverlay> &overlays,
                   std::vector<std::string> &diagnostics)
{
    if (!node)
    {
        return;
    }
    if (!node.IsSequence())
    {
        diagnostics.push_back("invalid config value for layers.overlays: expected sequence");
        return;
    }
    overlays.clear();
    for (std::size_t index = 0; index < node.size(); ++index)
    {
        const YAML::Node item = node[index];
        if (!item.IsMap())
        {
            diagnostics.push_back("invalid config value for layers.overlays: expected map entry");
            continue;
        }
        warn_unknown_keys(item,
                          {"path", "enabled", "opacity", "draw_order", "cache_identity"},
                          "layers.overlays.",
                          diagnostics);
        AppConfigOverlay overlay;
        std::string path;
        read_value(item, "path", path, diagnostics);
        overlay.path = path;
        read_value(item, "enabled", overlay.enabled, diagnostics);
        read_unit_float(item, "opacity", overlay.opacity, diagnostics);
        read_value(item, "draw_order", overlay.draw_order, diagnostics);
        read_value(item, "cache_identity", overlay.cache_identity, diagnostics);
        if (overlay.path.empty())
        {
            diagnostics.push_back("invalid config value for layers.overlays: missing path");
            continue;
        }
        overlays.push_back(std::move(overlay));
    }
}

void write_string(YAML::Emitter &out, const char *key, const std::string &value)
{
    out << YAML::Key << key << YAML::Value << value;
}

} // namespace

AppConfig default_app_config()
{
    return AppConfig{};
}

AppConfigLoadResult load_app_config_file(const std::filesystem::path &path)
{
    AppConfigLoadResult result;
    if (path.empty() || !std::filesystem::exists(path))
    {
        result.status = AppConfigLoadStatus::Missing;
        result.diagnostics.push_back("config file not found: " + path.string());
        return result;
    }

    std::ifstream input(path);
    if (!input)
    {
        result.status = AppConfigLoadStatus::Error;
        result.diagnostics.push_back("failed to open config file: " + path.string());
        return result;
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (trim_left(text).rfind("{", 0U) == 0U)
    {
        return load_legacy_json_like(text);
    }

    YAML::Node root;
    try
    {
        root = YAML::Load(text);
    }
    catch (const YAML::Exception &error)
    {
        result.status = AppConfigLoadStatus::Error;
        result.diagnostics.push_back(std::string("bad YAML: ") + error.what());
        return result;
    }
    if (!root || !root.IsMap())
    {
        result.status = AppConfigLoadStatus::Error;
        result.diagnostics.push_back("bad YAML: root must be a map");
        return result;
    }

    warn_unknown_keys(root,
                      {"version",
                       "app",
                       "window",
                       "view",
                       "panels",
                       "layers",
                       "telemetry",
                       "status_thresholds",
                       "vehicle_visuals",
                       "plots"},
                      "",
                      result.diagnostics);

    read_value(root, "version", result.config.version, result.diagnostics);
    if (result.config.version != 1)
    {
        result.diagnostics.push_back("unsupported config version: " +
                                     std::to_string(result.config.version));
        result.config.version = 1;
    }

    const YAML::Node app = root["app"];
    warn_unknown_keys(app, {"workspace"}, "app.", result.diagnostics);
    read_value(app, "workspace", result.config.workspace_mode, result.diagnostics);

    const YAML::Node window = root["window"];
    warn_unknown_keys(window, {"width", "height"}, "window.", result.diagnostics);
    read_positive_int(window, "width", result.config.window_width, result.diagnostics);
    read_positive_int(window, "height", result.config.window_height, result.diagnostics);

    const YAML::Node view = root["view"];
    warn_unknown_keys(
        view, {"mode", "follow_selected", "map_orientation"}, "view.", result.diagnostics);
    read_value(view, "mode", result.config.view_mode, result.diagnostics);
    read_value(view, "follow_selected", result.config.follow_selected, result.diagnostics);
    read_value(view, "map_orientation", result.config.map_orientation, result.diagnostics);

    const YAML::Node panels = root["panels"];
    warn_unknown_keys(panels,
                      {"active",
                       "telemetry_tracks_visible",
                       "telemetry_labels_visible",
                       "bathymetry_enabled",
                       "developer_diagnostics_visible",
                       "telemetry_diagnostics_visible",
                       "mavlink_inspector_visible"},
                      "panels.",
                      result.diagnostics);
    read_value(panels, "active", result.config.active_panel, result.diagnostics);
    read_value(panels,
               "telemetry_tracks_visible",
               result.config.telemetry_tracks_visible,
               result.diagnostics);
    read_value(panels,
               "telemetry_labels_visible",
               result.config.telemetry_labels_visible,
               result.diagnostics);
    read_value(panels, "bathymetry_enabled", result.config.bathymetry_enabled, result.diagnostics);
    read_value(panels,
               "developer_diagnostics_visible",
               result.config.developer_diagnostics_visible,
               result.diagnostics);
    read_value(panels,
               "telemetry_diagnostics_visible",
               result.config.telemetry_diagnostics_visible,
               result.diagnostics);
    read_value(panels,
               "mavlink_inspector_visible",
               result.config.mavlink_inspector_visible,
               result.diagnostics);

    const YAML::Node layers = root["layers"];
    warn_unknown_keys(
        layers, {"overlay_enabled", "overlay_opacity", "overlays"}, "layers.", result.diagnostics);
    read_value(layers, "overlay_enabled", result.config.overlay_enabled, result.diagnostics);
    read_unit_float(layers, "overlay_opacity", result.config.overlay_opacity, result.diagnostics);
    read_overlays(layers["overlays"], result.config.overlays, result.diagnostics);

    const YAML::Node telemetry = root["telemetry"];
    warn_unknown_keys(telemetry,
                      {"live_udp_host",
                       "live_udp_port",
                       "live_buffer_seconds",
                       "max_samples",
                       "render_max_points"},
                      "telemetry.",
                      result.diagnostics);
    read_value(
        telemetry, "live_udp_host", result.config.telemetry_live_udp_host, result.diagnostics);
    read_udp_port(
        telemetry, "live_udp_port", result.config.telemetry_live_udp_port, result.diagnostics);
    read_positive_double(telemetry,
                         "live_buffer_seconds",
                         result.config.telemetry_live_buffer_s,
                         result.diagnostics);
    read_positive_size(
        telemetry, "max_samples", result.config.telemetry_live_max_samples, result.diagnostics);
    read_positive_size(telemetry,
                       "render_max_points",
                       result.config.telemetry_live_render_max_points,
                       result.diagnostics);

    const YAML::Node thresholds = root["status_thresholds"];
    warn_unknown_keys(thresholds,
                      {"terrain_clearance_warning_m",
                       "terrain_clearance_critical_m",
                       "roll_warning_deg",
                       "pitch_warning_deg",
                       "frame_time_warning_ms",
                       "link_hz_warning",
                       "telemetry_gap_warning_s",
                       "telemetry_gap_critical_s"},
                      "status_thresholds.",
                      result.diagnostics);
    read_positive_double(thresholds,
                         "terrain_clearance_warning_m",
                         result.config.status_thresholds.terrain_clearance_warning_m,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "terrain_clearance_critical_m",
                         result.config.status_thresholds.terrain_clearance_critical_m,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "roll_warning_deg",
                         result.config.status_thresholds.roll_warning_deg,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "pitch_warning_deg",
                         result.config.status_thresholds.pitch_warning_deg,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "frame_time_warning_ms",
                         result.config.status_thresholds.frame_time_warning_ms,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "link_hz_warning",
                         result.config.status_thresholds.link_hz_warning,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "telemetry_gap_warning_s",
                         result.config.status_thresholds.telemetry_gap_warning_s,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "telemetry_gap_critical_s",
                         result.config.status_thresholds.telemetry_gap_critical_s,
                         result.diagnostics);

    result.config.plots = read_plot_shelf_config(root["plots"], result.diagnostics);

    result.status = AppConfigLoadStatus::Loaded;
    return result;
}

AppConfigSaveResult save_app_config_file(const std::filesystem::path &path, const AppConfig &config)
{
    AppConfigSaveResult result;
    result.path = path;
    if (path.empty())
    {
        result.diagnostics.push_back("config path is empty");
        return result;
    }

    std::error_code error;
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            result.diagnostics.push_back("failed to create config directory: " + error.message());
            return result;
        }
    }

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << 1;
    out << YAML::Key << "app" << YAML::Value << YAML::BeginMap;
    write_string(out, "workspace", config.workspace_mode);
    out << YAML::EndMap;
    out << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "width" << YAML::Value << config.window_width;
    out << YAML::Key << "height" << YAML::Value << config.window_height;
    out << YAML::EndMap;
    out << YAML::Key << "view" << YAML::Value << YAML::BeginMap;
    write_string(out, "mode", config.view_mode);
    out << YAML::Key << "follow_selected" << YAML::Value << config.follow_selected;
    write_string(out, "map_orientation", config.map_orientation);
    out << YAML::EndMap;
    out << YAML::Key << "panels" << YAML::Value << YAML::BeginMap;
    write_string(out, "active", config.active_panel);
    out << YAML::Key << "telemetry_tracks_visible" << YAML::Value
        << config.telemetry_tracks_visible;
    out << YAML::Key << "telemetry_labels_visible" << YAML::Value
        << config.telemetry_labels_visible;
    out << YAML::Key << "bathymetry_enabled" << YAML::Value << config.bathymetry_enabled;
    out << YAML::Key << "developer_diagnostics_visible" << YAML::Value
        << config.developer_diagnostics_visible;
    out << YAML::Key << "telemetry_diagnostics_visible" << YAML::Value
        << config.telemetry_diagnostics_visible;
    out << YAML::Key << "mavlink_inspector_visible" << YAML::Value
        << config.mavlink_inspector_visible;
    out << YAML::EndMap;
    out << YAML::Key << "layers" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "overlay_enabled" << YAML::Value << config.overlay_enabled;
    out << YAML::Key << "overlay_opacity" << YAML::Value << config.overlay_opacity;
    out << YAML::Key << "overlays" << YAML::Value << YAML::BeginSeq;
    for (const AppConfigOverlay &overlay : config.overlays)
    {
        out << YAML::BeginMap;
        out << YAML::Key << "path" << YAML::Value << overlay.path.generic_string();
        out << YAML::Key << "enabled" << YAML::Value << overlay.enabled;
        out << YAML::Key << "opacity" << YAML::Value << overlay.opacity;
        out << YAML::Key << "draw_order" << YAML::Value << overlay.draw_order;
        write_string(out, "cache_identity", overlay.cache_identity);
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::Key << "telemetry" << YAML::Value << YAML::BeginMap;
    write_string(out, "live_udp_host", config.telemetry_live_udp_host);
    out << YAML::Key << "live_udp_port" << YAML::Value << config.telemetry_live_udp_port;
    out << YAML::Key << "live_buffer_seconds" << YAML::Value << config.telemetry_live_buffer_s;
    out << YAML::Key << "max_samples" << YAML::Value << config.telemetry_live_max_samples;
    out << YAML::Key << "render_max_points" << YAML::Value
        << config.telemetry_live_render_max_points;
    out << YAML::EndMap;
    out << YAML::Key << "status_thresholds" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "terrain_clearance_warning_m" << YAML::Value
        << config.status_thresholds.terrain_clearance_warning_m;
    out << YAML::Key << "terrain_clearance_critical_m" << YAML::Value
        << config.status_thresholds.terrain_clearance_critical_m;
    out << YAML::Key << "roll_warning_deg" << YAML::Value
        << config.status_thresholds.roll_warning_deg;
    out << YAML::Key << "pitch_warning_deg" << YAML::Value
        << config.status_thresholds.pitch_warning_deg;
    out << YAML::Key << "frame_time_warning_ms" << YAML::Value
        << config.status_thresholds.frame_time_warning_ms;
    out << YAML::Key << "link_hz_warning" << YAML::Value
        << config.status_thresholds.link_hz_warning;
    out << YAML::Key << "telemetry_gap_warning_s" << YAML::Value
        << config.status_thresholds.telemetry_gap_warning_s;
    out << YAML::Key << "telemetry_gap_critical_s" << YAML::Value
        << config.status_thresholds.telemetry_gap_critical_s;
    out << YAML::EndMap;
    out << YAML::Key << "vehicle_visuals" << YAML::Value << YAML::BeginMap << YAML::EndMap;
    out << YAML::Key << "plots" << YAML::Value;
    write_plot_shelf_config(out, config.plots);
    out << YAML::EndMap;

    const std::filesystem::path temp_path =
        path.parent_path() / (path.filename().string() + ".tmp");
    {
        std::ofstream output(temp_path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            result.diagnostics.push_back("failed to open temp config file: " + temp_path.string());
            return result;
        }
        output << out.c_str() << '\n';
        output.flush();
        if (!output)
        {
            result.diagnostics.push_back("failed to flush temp config file: " + temp_path.string());
            return result;
        }
    }

    std::filesystem::rename(temp_path, path, error);
    if (error)
    {
        std::filesystem::remove(temp_path);
        result.diagnostics.push_back("failed to rename temp config file: " + error.message());
        return result;
    }
    result.saved = true;
    result.diagnostics.push_back("saved config: " + path.string());
    return result;
}

const char *app_config_load_status_label(const AppConfigLoadStatus status)
{
    switch (status)
    {
    case AppConfigLoadStatus::Missing:
        return "missing";
    case AppConfigLoadStatus::Loaded:
        return "loaded";
    case AppConfigLoadStatus::LoadedLegacy:
        return "loaded legacy";
    case AppConfigLoadStatus::Error:
        return "error";
    }
    return "unknown";
}

} // namespace animus::app
