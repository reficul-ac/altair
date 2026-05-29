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
#include <string_view>
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

void read_positive_float(const YAML::Node &node,
                         const char *key,
                         float &target,
                         std::vector<std::string> &diagnostics)
{
    float value = target;
    read_value(node, key, value, diagnostics);
    if (value <= 0.0F)
    {
        diagnostics.push_back(std::string("invalid config value for ") + key +
                              ": expected positive number");
        return;
    }
    target = value;
}

float clamped_vehicle_scale(const float value)
{
    if (value < 0.1F)
    {
        return 0.1F;
    }
    if (value > 10.0F)
    {
        return 10.0F;
    }
    return value;
}

bool valid_vehicle_assignment_key(const std::string_view value)
{
    const std::size_t colon = value.find(':');
    if (colon == std::string_view::npos || colon == 0U || colon + 1U >= value.size())
    {
        return false;
    }
    const auto digits = [](const std::string_view text)
    {
        return std::all_of(text.begin(),
                           text.end(),
                           [](const char ch)
                           { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
    };
    return digits(value.substr(0U, colon)) && digits(value.substr(colon + 1U));
}

bool valid_vehicle_heading_source(const std::string_view value)
{
    return value == "auto" || value == "none";
}

bool valid_vehicle_altitude_placement(const std::string_view value)
{
    return value == "terrain_resolved";
}

void read_vehicle_visuals(const YAML::Node &node,
                          VehicleVisualAssignments &visuals,
                          std::vector<std::string> &diagnostics)
{
    if (!node)
    {
        return;
    }
    if (!node.IsMap())
    {
        diagnostics.push_back("invalid config value for vehicle_visuals: expected map");
        return;
    }

    warn_unknown_keys(node, {"defaults_by_type", "entities"}, "vehicle_visuals.", diagnostics);

    const YAML::Node defaults = node["defaults_by_type"];
    if (defaults)
    {
        if (!defaults.IsMap())
        {
            diagnostics.push_back(
                "invalid config value for vehicle_visuals.defaults_by_type: expected map");
        }
        else
        {
            for (const auto &entry : defaults)
            {
                try
                {
                    const std::string type = entry.first.as<std::string>();
                    const std::string vehicle_id = entry.second.as<std::string>();
                    if (type.empty() || vehicle_id.empty())
                    {
                        diagnostics.push_back(
                            "invalid config value for vehicle_visuals.defaults_by_type: "
                            "expected non-empty strings");
                        continue;
                    }
                    visuals.defaults_by_type[type] = vehicle_id;
                }
                catch (const YAML::Exception &error)
                {
                    diagnostics.push_back(
                        std::string("invalid config value for vehicle_visuals.defaults_by_type: ") +
                        error.what());
                }
            }
        }
    }

    const YAML::Node entities = node["entities"];
    if (!entities)
    {
        return;
    }
    if (!entities.IsMap())
    {
        diagnostics.push_back("invalid config value for vehicle_visuals.entities: expected map");
        return;
    }
    for (const auto &entry : entities)
    {
        std::string key;
        try
        {
            key = entry.first.as<std::string>();
        }
        catch (const YAML::Exception &error)
        {
            diagnostics.push_back(
                std::string("invalid config key under vehicle_visuals.entities: ") + error.what());
            continue;
        }
        if (!valid_vehicle_assignment_key(key))
        {
            diagnostics.push_back("invalid config key under vehicle_visuals.entities: " + key);
            continue;
        }
        const YAML::Node assignment_node = entry.second;
        if (!assignment_node || !assignment_node.IsMap())
        {
            diagnostics.push_back("invalid config value for vehicle_visuals.entities." + key +
                                  ": expected map");
            continue;
        }
        warn_unknown_keys(
            assignment_node,
            {"vehicle_id", "force_icon_only", "scale", "heading_source", "altitude_placement"},
            "vehicle_visuals.entities." + key + ".",
            diagnostics);

        VehicleVisualAssignment assignment;
        read_value(assignment_node, "vehicle_id", assignment.vehicle_id, diagnostics);
        read_value(assignment_node, "force_icon_only", assignment.force_icon_only, diagnostics);
        read_positive_float(assignment_node, "scale", assignment.scale, diagnostics);
        assignment.scale = clamped_vehicle_scale(assignment.scale);
        read_value(assignment_node, "heading_source", assignment.heading_source, diagnostics);
        if (!valid_vehicle_heading_source(assignment.heading_source))
        {
            diagnostics.push_back("invalid config value for vehicle_visuals.entities." + key +
                                  ".heading_source: " + assignment.heading_source);
            assignment.heading_source = "auto";
        }
        read_value(
            assignment_node, "altitude_placement", assignment.altitude_placement, diagnostics);
        if (!valid_vehicle_altitude_placement(assignment.altitude_placement))
        {
            diagnostics.push_back("invalid config value for vehicle_visuals.entities." + key +
                                  ".altitude_placement: " + assignment.altitude_placement);
            assignment.altitude_placement = "terrain_resolved";
        }
        if (assignment.vehicle_id.empty())
        {
            diagnostics.push_back("invalid config value for vehicle_visuals.entities." + key +
                                  ".vehicle_id: expected non-empty string");
            assignment.vehicle_id = "animus.rc_plane.generic";
        }
        visuals.entities[key] = assignment;
    }
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
        const std::string canonical = canonical_workspace_id(*value);
        result.config.workspace_mode = canonical.empty() ? *value : canonical;
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

void read_layer_settings(const YAML::Node &node,
                         AppLayerSettings &settings,
                         std::vector<std::string> &diagnostics)
{
    if (!node)
    {
        return;
    }
    if (!node.IsMap())
    {
        diagnostics.push_back("invalid config value for layers: expected map");
        return;
    }
    read_value(node, "vehicle_icons_visible", settings.vehicle_icons_visible, diagnostics);
    read_value(node, "vehicle_labels_visible", settings.vehicle_labels_visible, diagnostics);
    read_value(node, "track_tail_visible", settings.track_tail_visible, diagnostics);
    read_value(node, "heading_vectors_visible", settings.heading_vectors_visible, diagnostics);
    read_value(node, "planned_route_visible", settings.planned_route_visible, diagnostics);
    read_value(node, "geofence_rally_visible", settings.geofence_rally_visible, diagnostics);
    read_value(
        node, "terrain_confidence_visible", settings.terrain_confidence_visible, diagnostics);
    read_value(node,
               "terrain_clearance_heatmap_visible",
               settings.terrain_clearance_heatmap_visible,
               diagnostics);
    read_value(node, "geotiff_overlay_visible", settings.geotiff_overlay_visible, diagnostics);
    read_unit_float(node, "geotiff_overlay_opacity", settings.geotiff_overlay_opacity, diagnostics);
    read_value(
        node, "geotiff_overlay_draw_order", settings.geotiff_overlay_draw_order, diagnostics);
    read_value(node, "bathymetry_visible", settings.bathymetry_visible, diagnostics);
    read_unit_float(node, "bathymetry_opacity", settings.bathymetry_opacity, diagnostics);
    read_value(node, "hillshade_visible", settings.hillshade_visible, diagnostics);
    read_value(node, "tile_state_debug_visible", settings.tile_state_debug_visible, diagnostics);
    read_value(
        node, "fallback_highlight_visible", settings.fallback_highlight_visible, diagnostics);
    read_positive_size(
        node, "selected_entity_tail_points", settings.selected_entity_tail_points, diagnostics);
}

void read_window_rect(const YAML::Node &node,
                      const std::string &key,
                      AppWindowRect &target,
                      std::vector<std::string> &diagnostics)
{
    if (!node)
    {
        return;
    }
    if (!node.IsMap())
    {
        diagnostics.push_back("invalid config value for workspaces." + key + ": expected map");
        return;
    }
    warn_unknown_keys(node, {"x", "y", "width", "height"}, "workspaces." + key + ".", diagnostics);
    read_value(node, "x", target.x, diagnostics);
    read_value(node, "y", target.y, diagnostics);
    read_positive_float(node, "width", target.width, diagnostics);
    read_positive_float(node, "height", target.height, diagnostics);
}

void read_workspace_layouts(const YAML::Node &node,
                            std::map<std::string, AppWorkspaceLayout> &layouts,
                            std::vector<std::string> &diagnostics)
{
    if (!node)
    {
        return;
    }
    if (!node.IsMap())
    {
        diagnostics.push_back("invalid config value for workspaces: expected map");
        return;
    }
    layouts.clear();
    for (const auto &entry : node)
    {
        const std::string raw_id = entry.first.as<std::string>();
        const std::string id = canonical_workspace_id(raw_id);
        if (id.empty())
        {
            diagnostics.push_back("unknown workspace layout key: " + raw_id);
            continue;
        }
        const YAML::Node layout_node = entry.second;
        if (!layout_node.IsMap())
        {
            diagnostics.push_back("invalid config value for workspaces." + raw_id +
                                  ": expected map");
            continue;
        }
        warn_unknown_keys(layout_node,
                          {"active_panel",
                           "view_mode",
                           "map_orientation",
                           "plot_shelf_visible",
                           "plot_shelf_height_px",
                           "timeline_visible",
                           "timeline_height_px",
                           "inspector_visible",
                           "bottom_drawer_state",
                           "main_panel",
                           "inspector",
                           "timeline",
                           "plot_shelf"},
                          "workspaces." + raw_id + ".",
                          diagnostics);
        AppWorkspaceLayout layout = default_workspace_layout(id);
        read_value(layout_node, "active_panel", layout.active_panel, diagnostics);
        read_value(layout_node, "view_mode", layout.view_mode, diagnostics);
        read_value(layout_node, "map_orientation", layout.map_orientation, diagnostics);
        read_value(layout_node, "plot_shelf_visible", layout.plot_shelf_visible, diagnostics);
        read_positive_float(
            layout_node, "plot_shelf_height_px", layout.plot_shelf_height_px, diagnostics);
        read_value(layout_node, "timeline_visible", layout.timeline_visible, diagnostics);
        read_positive_float(
            layout_node, "timeline_height_px", layout.timeline_height_px, diagnostics);
        read_value(layout_node, "inspector_visible", layout.inspector_visible, diagnostics);
        std::string bottom_drawer_state =
            bottom_drawer_state_config_value(layout.bottom_drawer_state);
        read_value(layout_node, "bottom_drawer_state", bottom_drawer_state, diagnostics);
        layout.bottom_drawer_state = bottom_drawer_state_from_config_value(bottom_drawer_state);
        read_window_rect(
            layout_node["main_panel"], raw_id + ".main_panel", layout.main_panel, diagnostics);
        read_window_rect(
            layout_node["inspector"], raw_id + ".inspector", layout.inspector, diagnostics);
        read_window_rect(
            layout_node["timeline"], raw_id + ".timeline", layout.timeline, diagnostics);
        read_window_rect(
            layout_node["plot_shelf"], raw_id + ".plot_shelf", layout.plot_shelf, diagnostics);
        layouts[id] = std::move(layout);
    }
}

void write_string(YAML::Emitter &out, const char *key, const std::string &value)
{
    out << YAML::Key << key << YAML::Value << value;
}

void write_window_rect(YAML::Emitter &out, const char *key, const AppWindowRect &rect)
{
    out << YAML::Key << key << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "x" << YAML::Value << rect.x;
    out << YAML::Key << "y" << YAML::Value << rect.y;
    out << YAML::Key << "width" << YAML::Value << rect.width;
    out << YAML::Key << "height" << YAML::Value << rect.height;
    out << YAML::EndMap;
}

} // namespace

AppConfig default_app_config()
{
    return AppConfig{};
}

std::string canonical_workspace_id(const std::string_view value)
{
    if (value == "fly_test" || value == "operator")
    {
        return "fly_test";
    }
    if (value == "plan")
    {
        return "plan";
    }
    if (value == "analyze" || value == "advanced")
    {
        return "analyze";
    }
    if (value == "terrain")
    {
        return "terrain";
    }
    if (value == "export" || value == "capture" || value == "report")
    {
        return "export";
    }
    if (value == "developer")
    {
        return "developer";
    }
    return {};
}

const char *bottom_drawer_state_config_value(const BottomDrawerState state)
{
    switch (state)
    {
    case BottomDrawerState::Hidden:
        return "hidden";
    case BottomDrawerState::Collapsed:
        return "collapsed";
    case BottomDrawerState::Compact:
        return "compact";
    case BottomDrawerState::Expanded:
        return "expanded";
    }
    return "compact";
}

BottomDrawerState bottom_drawer_state_from_config_value(const std::string_view value)
{
    if (value == "hidden")
    {
        return BottomDrawerState::Hidden;
    }
    if (value == "collapsed")
    {
        return BottomDrawerState::Collapsed;
    }
    if (value == "expanded")
    {
        return BottomDrawerState::Expanded;
    }
    return BottomDrawerState::Compact;
}

AppWorkspaceLayout default_workspace_layout(const std::string_view workspace_id)
{
    const std::string id = canonical_workspace_id(workspace_id);
    AppWorkspaceLayout layout;
    layout.main_panel = {150.0F, 50.0F, 440.0F, 430.0F};
    layout.inspector = {952.0F, 50.0F, 316.0F, 520.0F};
    layout.timeline = {150.0F, 570.0F, 766.0F, 144.0F};
    layout.plot_shelf = {150.0F, 350.0F, 766.0F, 210.0F};
    if (id == "plan")
    {
        layout.active_panel = "view";
        layout.view_mode = "map2d";
        layout.map_orientation = "north_up";
        layout.plot_shelf_visible = false;
        layout.timeline_visible = false;
        layout.inspector_visible = false;
        layout.bottom_drawer_state = BottomDrawerState::Hidden;
    }
    else if (id == "analyze")
    {
        layout.active_panel = "telemetry";
        layout.view_mode = "terrain3d";
        layout.plot_shelf_visible = true;
        layout.plot_shelf_height_px = 300.0F;
        layout.timeline_visible = true;
        layout.timeline_height_px = 190.0F;
        layout.inspector_visible = true;
        layout.bottom_drawer_state = BottomDrawerState::Expanded;
    }
    else if (id == "terrain")
    {
        layout.active_panel = "layers";
        layout.view_mode = "terrain3d";
        layout.plot_shelf_visible = false;
        layout.timeline_visible = false;
        layout.inspector_visible = true;
        layout.bottom_drawer_state = BottomDrawerState::Hidden;
    }
    else if (id == "export")
    {
        layout.active_panel = "capture";
        layout.view_mode = "terrain3d";
        layout.plot_shelf_visible = false;
        layout.timeline_visible = false;
        layout.inspector_visible = false;
        layout.bottom_drawer_state = BottomDrawerState::Hidden;
    }
    else if (id == "developer")
    {
        layout.active_panel = "developer";
        layout.view_mode = "terrain3d";
        layout.plot_shelf_visible = false;
        layout.timeline_visible = false;
        layout.inspector_visible = true;
        layout.bottom_drawer_state = BottomDrawerState::Hidden;
    }
    else
    {
        layout.active_panel = "telemetry";
        layout.view_mode = "terrain3d";
        layout.plot_shelf_visible = true;
        layout.plot_shelf_height_px = 160.0F;
        layout.timeline_visible = true;
        layout.timeline_height_px = 28.0F;
        layout.inspector_visible = true;
        layout.bottom_drawer_state = BottomDrawerState::Compact;
    }
    layout.plot_shelf.height = layout.plot_shelf_height_px;
    layout.timeline.height = layout.timeline_height_px;
    return layout;
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
                       "workspaces",
                       "layers",
                       "telemetry",
                       "status_thresholds",
                       "vehicle_visuals",
                       "plots",
                       "selected_vehicle",
                       "ghost_replay",
                       "report_export"},
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
    const std::string canonical_workspace = canonical_workspace_id(result.config.workspace_mode);
    if (!canonical_workspace.empty())
    {
        result.config.workspace_mode = canonical_workspace;
    }

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
    warn_unknown_keys(layers,
                      {"vehicle_icons_visible",
                       "vehicle_labels_visible",
                       "track_tail_visible",
                       "heading_vectors_visible",
                       "planned_route_visible",
                       "geofence_rally_visible",
                       "terrain_confidence_visible",
                       "terrain_clearance_heatmap_visible",
                       "geotiff_overlay_visible",
                       "geotiff_overlay_opacity",
                       "geotiff_overlay_draw_order",
                       "bathymetry_visible",
                       "bathymetry_opacity",
                       "hillshade_visible",
                       "tile_state_debug_visible",
                       "fallback_highlight_visible",
                       "selected_entity_tail_points",
                       "overlay_enabled",
                       "overlay_opacity",
                       "overlays"},
                      "layers.",
                      result.diagnostics);
    read_layer_settings(layers, result.config.layers, result.diagnostics);
    read_value(layers, "overlay_enabled", result.config.overlay_enabled, result.diagnostics);
    read_unit_float(layers, "overlay_opacity", result.config.overlay_opacity, result.diagnostics);
    if (layers && layers["overlay_enabled"] && !layers["geotiff_overlay_visible"])
    {
        result.config.layers.geotiff_overlay_visible = result.config.overlay_enabled;
    }
    if (layers && layers["overlay_opacity"] && !layers["geotiff_overlay_opacity"])
    {
        result.config.layers.geotiff_overlay_opacity = result.config.overlay_opacity;
    }
    result.config.overlay_enabled = result.config.layers.geotiff_overlay_visible;
    result.config.overlay_opacity = result.config.layers.geotiff_overlay_opacity;
    if (layers && layers["bathymetry_visible"])
    {
        result.config.bathymetry_enabled = result.config.layers.bathymetry_visible;
    }
    if (layers && layers["track_tail_visible"])
    {
        result.config.telemetry_tracks_visible = result.config.layers.track_tail_visible;
    }
    if (layers && layers["vehicle_labels_visible"])
    {
        result.config.telemetry_labels_visible = result.config.layers.vehicle_labels_visible;
    }
    read_overlays(layers["overlays"], result.config.overlays, result.diagnostics);
    result.config.selected_entity_tail_points = result.config.layers.selected_entity_tail_points;

    const YAML::Node telemetry = root["telemetry"];
    warn_unknown_keys(telemetry,
                      {"live_udp_host",
                       "live_udp_port",
                       "live_buffer_seconds",
                       "max_samples",
                       "render_max_points",
                       "selected_entity_tail_points"},
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
    read_positive_size(telemetry,
                       "selected_entity_tail_points",
                       result.config.selected_entity_tail_points,
                       result.diagnostics);
    if (telemetry && telemetry["selected_entity_tail_points"])
    {
        result.config.layers.selected_entity_tail_points =
            result.config.selected_entity_tail_points;
    }

    const YAML::Node selected_vehicle = root["selected_vehicle"];
    warn_unknown_keys(selected_vehicle,
                      {"test_name", "phase", "target_speed", "target_altitude", "target_heading"},
                      "selected_vehicle.",
                      result.diagnostics);
    read_value(selected_vehicle,
               "test_name",
               result.config.selected_vehicle_test.test_name,
               result.diagnostics);
    read_value(
        selected_vehicle, "phase", result.config.selected_vehicle_test.phase, result.diagnostics);
    read_value(selected_vehicle,
               "target_speed",
               result.config.selected_vehicle_test.target_speed,
               result.diagnostics);
    read_value(selected_vehicle,
               "target_altitude",
               result.config.selected_vehicle_test.target_altitude,
               result.diagnostics);
    read_value(selected_vehicle,
               "target_heading",
               result.config.selected_vehicle_test.target_heading,
               result.diagnostics);

    const YAML::Node ghost = root["ghost_replay"];
    warn_unknown_keys(
        ghost, {"recent_baseline_path", "layer_visible"}, "ghost_replay.", result.diagnostics);
    std::string baseline_path;
    read_value(ghost, "recent_baseline_path", baseline_path, result.diagnostics);
    result.config.ghost_recent_baseline_path = baseline_path;
    read_value(ghost, "layer_visible", result.config.ghost_layer_visible, result.diagnostics);

    const YAML::Node report = root["report_export"];
    warn_unknown_keys(report, {"default_dir"}, "report_export.", result.diagnostics);
    std::string report_dir = result.config.report_export_default_dir.string();
    read_value(report, "default_dir", report_dir, result.diagnostics);
    result.config.report_export_default_dir = report_dir;

    const YAML::Node thresholds = root["status_thresholds"];
    warn_unknown_keys(thresholds,
                      {"terrain_clearance_warning_m",
                       "terrain_clearance_critical_m",
                       "roll_warning_deg",
                       "pitch_warning_deg",
                       "frame_time_warning_ms",
                       "link_hz_warning",
                       "telemetry_gap_warning_s",
                       "telemetry_gap_critical_s",
                       "plan_deviation_warning_m",
                       "plan_altitude_error_warning_m"},
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
    read_positive_double(thresholds,
                         "plan_deviation_warning_m",
                         result.config.status_thresholds.plan_deviation_warning_m,
                         result.diagnostics);
    read_positive_double(thresholds,
                         "plan_altitude_error_warning_m",
                         result.config.status_thresholds.plan_altitude_error_warning_m,
                         result.diagnostics);

    result.config.plots = read_plot_shelf_config(root["plots"], result.diagnostics);
    read_vehicle_visuals(
        root["vehicle_visuals"], result.config.vehicle_visuals, result.diagnostics);
    read_workspace_layouts(root["workspaces"], result.config.workspace_layouts, result.diagnostics);

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
    const std::string workspace = canonical_workspace_id(config.workspace_mode);
    write_string(out, "workspace", workspace.empty() ? std::string("fly_test") : workspace);
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
    out << YAML::Key << "vehicle_icons_visible" << YAML::Value
        << config.layers.vehicle_icons_visible;
    out << YAML::Key << "vehicle_labels_visible" << YAML::Value
        << config.layers.vehicle_labels_visible;
    out << YAML::Key << "track_tail_visible" << YAML::Value << config.layers.track_tail_visible;
    out << YAML::Key << "heading_vectors_visible" << YAML::Value
        << config.layers.heading_vectors_visible;
    out << YAML::Key << "planned_route_visible" << YAML::Value
        << config.layers.planned_route_visible;
    out << YAML::Key << "geofence_rally_visible" << YAML::Value
        << config.layers.geofence_rally_visible;
    out << YAML::Key << "terrain_confidence_visible" << YAML::Value
        << config.layers.terrain_confidence_visible;
    out << YAML::Key << "terrain_clearance_heatmap_visible" << YAML::Value
        << config.layers.terrain_clearance_heatmap_visible;
    out << YAML::Key << "geotiff_overlay_visible" << YAML::Value
        << config.layers.geotiff_overlay_visible;
    out << YAML::Key << "geotiff_overlay_opacity" << YAML::Value
        << config.layers.geotiff_overlay_opacity;
    out << YAML::Key << "geotiff_overlay_draw_order" << YAML::Value
        << config.layers.geotiff_overlay_draw_order;
    out << YAML::Key << "bathymetry_visible" << YAML::Value << config.layers.bathymetry_visible;
    out << YAML::Key << "bathymetry_opacity" << YAML::Value << config.layers.bathymetry_opacity;
    out << YAML::Key << "hillshade_visible" << YAML::Value << config.layers.hillshade_visible;
    out << YAML::Key << "tile_state_debug_visible" << YAML::Value
        << config.layers.tile_state_debug_visible;
    out << YAML::Key << "fallback_highlight_visible" << YAML::Value
        << config.layers.fallback_highlight_visible;
    out << YAML::Key << "selected_entity_tail_points" << YAML::Value
        << config.layers.selected_entity_tail_points;
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
    out << YAML::Key << "selected_entity_tail_points" << YAML::Value
        << config.selected_entity_tail_points;
    out << YAML::EndMap;
    out << YAML::Key << "selected_vehicle" << YAML::Value << YAML::BeginMap;
    write_string(out, "test_name", config.selected_vehicle_test.test_name);
    write_string(out, "phase", config.selected_vehicle_test.phase);
    write_string(out, "target_speed", config.selected_vehicle_test.target_speed);
    write_string(out, "target_altitude", config.selected_vehicle_test.target_altitude);
    write_string(out, "target_heading", config.selected_vehicle_test.target_heading);
    out << YAML::EndMap;
    out << YAML::Key << "ghost_replay" << YAML::Value << YAML::BeginMap;
    write_string(out, "recent_baseline_path", config.ghost_recent_baseline_path.generic_string());
    out << YAML::Key << "layer_visible" << YAML::Value << config.ghost_layer_visible;
    out << YAML::EndMap;
    out << YAML::Key << "report_export" << YAML::Value << YAML::BeginMap;
    write_string(out, "default_dir", config.report_export_default_dir.generic_string());
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
    out << YAML::Key << "plan_deviation_warning_m" << YAML::Value
        << config.status_thresholds.plan_deviation_warning_m;
    out << YAML::Key << "plan_altitude_error_warning_m" << YAML::Value
        << config.status_thresholds.plan_altitude_error_warning_m;
    out << YAML::EndMap;
    out << YAML::Key << "vehicle_visuals" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "defaults_by_type" << YAML::Value << YAML::BeginMap;
    for (const auto &[type, vehicle_id] : config.vehicle_visuals.defaults_by_type)
    {
        write_string(out, type.c_str(), vehicle_id);
    }
    out << YAML::EndMap;
    out << YAML::Key << "entities" << YAML::Value << YAML::BeginMap;
    for (const auto &[key, assignment] : config.vehicle_visuals.entities)
    {
        out << YAML::Key << key << YAML::Value << YAML::BeginMap;
        write_string(out, "vehicle_id", assignment.vehicle_id);
        out << YAML::Key << "force_icon_only" << YAML::Value << assignment.force_icon_only;
        out << YAML::Key << "scale" << YAML::Value << clamped_vehicle_scale(assignment.scale);
        write_string(out,
                     "heading_source",
                     valid_vehicle_heading_source(assignment.heading_source)
                         ? assignment.heading_source
                         : std::string("auto"));
        write_string(out,
                     "altitude_placement",
                     valid_vehicle_altitude_placement(assignment.altitude_placement)
                         ? assignment.altitude_placement
                         : std::string("terrain_resolved"));
        out << YAML::EndMap;
    }
    out << YAML::EndMap;
    out << YAML::EndMap;
    out << YAML::Key << "plots" << YAML::Value;
    write_plot_shelf_config(out, config.plots);
    out << YAML::Key << "workspaces" << YAML::Value << YAML::BeginMap;
    for (const auto &[id, layout] : config.workspace_layouts)
    {
        const std::string canonical_id = canonical_workspace_id(id);
        if (canonical_id.empty())
        {
            continue;
        }
        out << YAML::Key << canonical_id << YAML::Value << YAML::BeginMap;
        write_string(out, "active_panel", layout.active_panel);
        write_string(out, "view_mode", layout.view_mode);
        write_string(out, "map_orientation", layout.map_orientation);
        out << YAML::Key << "plot_shelf_visible" << YAML::Value << layout.plot_shelf_visible;
        out << YAML::Key << "plot_shelf_height_px" << YAML::Value << layout.plot_shelf_height_px;
        out << YAML::Key << "timeline_visible" << YAML::Value << layout.timeline_visible;
        out << YAML::Key << "timeline_height_px" << YAML::Value << layout.timeline_height_px;
        out << YAML::Key << "inspector_visible" << YAML::Value << layout.inspector_visible;
        write_string(out,
                     "bottom_drawer_state",
                     bottom_drawer_state_config_value(layout.bottom_drawer_state));
        write_window_rect(out, "main_panel", layout.main_panel);
        write_window_rect(out, "inspector", layout.inspector);
        write_window_rect(out, "timeline", layout.timeline);
        write_window_rect(out, "plot_shelf", layout.plot_shelf);
        out << YAML::EndMap;
    }
    out << YAML::EndMap;
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
