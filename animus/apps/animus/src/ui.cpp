#include "ui.hpp"

#include "capture.hpp"
#include "ghost_replay.hpp"
#include "layer_offline.hpp"
#include "layer_stack_model.hpp"
#include "mavlink_inspector.hpp"
#include "plot_ui.hpp"
#include "selected_vehicle_card.hpp"
#include "status_ribbon.hpp"
#include "timeline_transport.hpp"
#include "vehicle_visual_style.hpp"
#include "workspace_layout_model.hpp"

#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

namespace animus::app
{
namespace
{

constexpr float status_bar_height = 38.0F;
constexpr float chrome_margin = 12.0F;
constexpr float nav_width = 128.0F;
constexpr float panel_gap = 10.0F;
constexpr float inspector_width = 316.0F;
const ImVec4 text_muted(0.64F, 0.68F, 0.72F, 1.0F);
const ImVec4 panel_bg(0.075F, 0.088F, 0.098F, 0.92F);
const ImVec4 panel_border(0.18F, 0.21F, 0.23F, 0.90F);
const ImVec4 accent_blue(0.30F, 0.64F, 0.90F, 1.0F);
const ImVec4 live_green(0.34F, 0.76F, 0.52F, 1.0F);
const ImVec4 stale_amber(0.93F, 0.60F, 0.28F, 1.0F);
const ImVec4 quiet_gray(0.46F, 0.49F, 0.52F, 1.0F);

enum class PillState
{
    Good,
    Warning,
    Error,
    Inactive,
};

const char *altitude_datum_label(animus::telemetry_core::AltitudeDatum datum);
void draw_review_filters(UiState &ui_state);
bool mode_visible_in_workspace(UiNavigationMode mode, WorkspaceMode workspace_mode);

const char *workspace_label(const WorkspaceMode mode)
{
    switch (mode)
    {
    case WorkspaceMode::FlyTest:
        return "Fly/Test";
    case WorkspaceMode::Plan:
        return "Plan";
    case WorkspaceMode::Analyze:
        return "Analyze";
    case WorkspaceMode::Terrain:
        return "Terrain";
    case WorkspaceMode::Export:
        return "Export";
    case WorkspaceMode::Developer:
        return "Developer";
    }
    return "Fly/Test";
}

std::string workspace_config_value(const WorkspaceMode mode)
{
    switch (mode)
    {
    case WorkspaceMode::FlyTest:
        return "fly_test";
    case WorkspaceMode::Plan:
        return "plan";
    case WorkspaceMode::Analyze:
        return "analyze";
    case WorkspaceMode::Terrain:
        return "terrain";
    case WorkspaceMode::Export:
        return "export";
    case WorkspaceMode::Developer:
        return "developer";
    }
    return "fly_test";
}

const char *view_mode_label(const ViewMode mode)
{
    switch (mode)
    {
    case ViewMode::Terrain3D:
        return "3D";
    case ViewMode::Map2D:
        return "2D";
    case ViewMode::Oblique25D:
        return "2.5D";
    }
    return "3D";
}

const char *orientation_label(const MapOrientationMode mode)
{
    switch (mode)
    {
    case MapOrientationMode::NorthUp:
        return "North";
    case MapOrientationMode::TrackUp:
        return "Track";
    case MapOrientationMode::FreeRotate:
        return "Free";
    }
    return "North";
}

const char *mode_label(const UiNavigationMode mode)
{
    switch (mode)
    {
    case UiNavigationMode::View:
        return "View";
    case UiNavigationMode::Layers:
        return "Layers";
    case UiNavigationMode::Telemetry:
        return "Telemetry";
    case UiNavigationMode::Signals:
        return "Signals";
    case UiNavigationMode::Capture:
        return "Capture";
    case UiNavigationMode::Settings:
        return "Settings";
    case UiNavigationMode::Developer:
        return "Developer";
    }
    return "View";
}

const char *workspace_tab_label(const UiNavigationMode mode, const WorkspaceMode workspace)
{
    switch (workspace)
    {
    case WorkspaceMode::FlyTest:
        switch (mode)
        {
        case UiNavigationMode::View:
            return "Vehicle";
        case UiNavigationMode::Telemetry:
            return "Link";
        case UiNavigationMode::Signals:
            return "Test";
        case UiNavigationMode::Capture:
            return "Plots";
        default:
            break;
        }
        break;
    case WorkspaceMode::Plan:
        switch (mode)
        {
        case UiNavigationMode::View:
            return "Mission";
        case UiNavigationMode::Layers:
            return "Terrain";
        default:
            break;
        }
        break;
    case WorkspaceMode::Analyze:
        switch (mode)
        {
        case UiNavigationMode::Telemetry:
            return "Timeline";
        case UiNavigationMode::Signals:
            return "Plots";
        case UiNavigationMode::Capture:
            return "Report";
        default:
            break;
        }
        break;
    case WorkspaceMode::Terrain:
        switch (mode)
        {
        case UiNavigationMode::Layers:
            return "Layers";
        case UiNavigationMode::View:
            return "Probe";
        default:
            break;
        }
        break;
    case WorkspaceMode::Export:
        switch (mode)
        {
        case UiNavigationMode::Capture:
            return "Bundle";
        case UiNavigationMode::Settings:
            return "Settings";
        default:
            break;
        }
        break;
    case WorkspaceMode::Developer:
        break;
    }
    return mode_label(mode);
}

std::string format_value(const char *format, const double value)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

std::string format_altitude(const std::optional<double> altitude,
                            const animus::telemetry_core::AltitudeDatum datum)
{
    if (!altitude)
    {
        return "alt n/a";
    }
    return format_value("%.0f m", *altitude) + " " + altitude_datum_label(datum);
}

std::string format_speed(const std::optional<double> speed_mps)
{
    return speed_mps ? format_value("%.1f m/s", *speed_mps) : std::string("speed n/a");
}

std::string format_distance_m(const std::optional<double> distance_m)
{
    return distance_m ? format_value("%.0f m", *distance_m) : std::string("n/a");
}

std::string format_route_distance(const double distance_m)
{
    if (distance_m >= 1000.0)
    {
        return format_value("%.2f km", distance_m / 1000.0);
    }
    return format_value("%.0f m", distance_m);
}

std::string format_percent(const double ratio)
{
    return format_value("%.0f%%", std::clamp(ratio, 0.0, 1.0) * 100.0);
}

std::string format_age(const double age_s)
{
    if (age_s < 1.0)
    {
        return format_value("%.0f ms", age_s * 1000.0);
    }
    return format_value("%.1f s", age_s);
}

void muted_text(const char *text)
{
    ImGui::TextColored(text_muted, "%s", text);
}

void draw_status_dot(const ImVec4 color)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float radius = 4.0F;
    ImGui::Dummy(ImVec2(radius * 2.0F + 2.0F, radius * 2.0F + 2.0F));
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(cursor.x + radius + 1.0F, cursor.y + radius + 1.0F),
        radius,
        ImGui::ColorConvertFloat4ToU32(color),
        16);
}

ImVec4 pill_state_color(const PillState state)
{
    switch (state)
    {
    case PillState::Good:
        return live_green;
    case PillState::Warning:
        return stale_amber;
    case PillState::Error:
        return ImVec4(0.94F, 0.28F, 0.28F, 1.0F);
    case PillState::Inactive:
        return quiet_gray;
    }
    return quiet_gray;
}

PillState pill_state_from_status_level(const StatusRibbonLevel level)
{
    switch (level)
    {
    case StatusRibbonLevel::Ok:
        return PillState::Good;
    case StatusRibbonLevel::Caution:
        return PillState::Warning;
    case StatusRibbonLevel::Warning:
        return PillState::Error;
    case StatusRibbonLevel::Unknown:
        return PillState::Inactive;
    }
    return PillState::Inactive;
}

bool draw_status_pill(const char *popup_id, const char *summary, const PillState state)
{
    const ImVec2 text_size = ImGui::CalcTextSize(summary);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 size(text_size.x + 24.0F, 23.0F);
    ImGui::InvisibleButton((std::string("##") + popup_id).c_str(), size);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
    {
        ImGui::OpenPopup(popup_id);
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(cursor,
                        ImVec2(cursor.x + size.x, cursor.y + size.y),
                        hovered ? IM_COL32(38, 47, 53, 232) : IM_COL32(26, 31, 35, 214),
                        6.0F);
    draw->AddCircleFilled(ImVec2(cursor.x + 10.0F, cursor.y + 11.5F),
                          3.5F,
                          ImGui::ColorConvertFloat4ToU32(pill_state_color(state)),
                          14);
    draw->AddText(ImVec2(cursor.x + 18.0F, cursor.y + 4.0F), IM_COL32(219, 226, 232, 242), summary);
    return ImGui::BeginPopup(popup_id);
}

void nav_button(UiState &ui_state, UiNavigationMode mode)
{
    if (!mode_visible_in_workspace(mode, ui_state.workspace_mode))
    {
        return;
    }
    const bool selected = ui_state.active_mode == mode;
    ImGui::PushID(mode_label(mode));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0F, 8.0F));
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.29F, 0.37F, 0.96F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18F, 0.36F, 0.45F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86F, 0.94F, 0.98F, 1.0F));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09F, 0.105F, 0.115F, 0.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15F, 0.17F, 0.18F, 0.88F));
        ImGui::PushStyleColor(ImGuiCol_Text, text_muted);
    }
    if (ImGui::Button(workspace_tab_label(mode, ui_state.workspace_mode), ImVec2(-1.0F, 0.0F)))
    {
        if (ui_state.active_mode != mode)
        {
            ui_state.active_mode = mode;
            ui_state.main_panel_restore_pending = true;
        }
        if (mode == UiNavigationMode::Developer)
        {
            ui_state.developer_diagnostics_visible = true;
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

bool mode_visible_in_workspace(const UiNavigationMode mode, const WorkspaceMode workspace_mode)
{
    if (mode == UiNavigationMode::Developer)
    {
        return workspace_mode == WorkspaceMode::Developer;
    }
    if (workspace_mode == WorkspaceMode::FlyTest)
    {
        return mode == UiNavigationMode::Telemetry || mode == UiNavigationMode::Signals ||
               mode == UiNavigationMode::Capture || mode == UiNavigationMode::View;
    }
    if (workspace_mode == WorkspaceMode::Plan)
    {
        return mode == UiNavigationMode::View || mode == UiNavigationMode::Layers;
    }
    if (workspace_mode == WorkspaceMode::Analyze)
    {
        return mode == UiNavigationMode::Telemetry || mode == UiNavigationMode::Signals ||
               mode == UiNavigationMode::Capture;
    }
    if (workspace_mode == WorkspaceMode::Terrain)
    {
        return mode == UiNavigationMode::Layers || mode == UiNavigationMode::View;
    }
    if (workspace_mode == WorkspaceMode::Export)
    {
        return mode == UiNavigationMode::Capture || mode == UiNavigationMode::Settings;
    }
    return true;
}

void sanitize_active_mode(UiState &ui_state)
{
    if (!mode_visible_in_workspace(ui_state.active_mode, ui_state.workspace_mode))
    {
        switch (ui_state.workspace_mode)
        {
        case WorkspaceMode::Analyze:
            ui_state.active_mode = UiNavigationMode::Telemetry;
            break;
        case WorkspaceMode::Terrain:
            ui_state.active_mode = UiNavigationMode::Layers;
            break;
        case WorkspaceMode::Export:
            ui_state.active_mode = UiNavigationMode::Capture;
            break;
        case WorkspaceMode::Developer:
            ui_state.active_mode = UiNavigationMode::Developer;
            break;
        case WorkspaceMode::FlyTest:
        case WorkspaceMode::Plan:
            ui_state.active_mode = UiNavigationMode::View;
            break;
        }
    }
}

void request_workspace_layout_restore(UiState &ui_state)
{
    ui_state.workspace_layout_restore_pending = true;
    ui_state.main_panel_restore_pending = true;
    ui_state.inspector_restore_pending = true;
    ui_state.timeline_restore_pending = true;
    ui_state.plot_shelf_restore_pending = true;
}

AppWorkspaceLayout &workspace_layout(UiState &ui_state, const WorkspaceMode mode)
{
    const std::string id = workspace_config_value(mode);
    auto found = ui_state.workspace_layouts.find(id);
    if (found == ui_state.workspace_layouts.end())
    {
        found = ui_state.workspace_layouts.emplace(id, default_workspace_layout(id)).first;
    }
    return found->second;
}

ViewMode view_mode_from_config_value(const std::string &value)
{
    if (value == "map2d")
    {
        return ViewMode::Map2D;
    }
    if (value == "oblique25d")
    {
        return ViewMode::Oblique25D;
    }
    return ViewMode::Terrain3D;
}

std::string view_mode_config_value(const ViewMode mode)
{
    switch (mode)
    {
    case ViewMode::Terrain3D:
        return "terrain3d";
    case ViewMode::Map2D:
        return "map2d";
    case ViewMode::Oblique25D:
        return "oblique25d";
    }
    return "terrain3d";
}

MapOrientationMode map_orientation_from_layout_value(const std::string &value)
{
    if (value == "track_up")
    {
        return MapOrientationMode::TrackUp;
    }
    if (value == "free_rotate")
    {
        return MapOrientationMode::FreeRotate;
    }
    return MapOrientationMode::NorthUp;
}

std::string map_orientation_layout_value(const MapOrientationMode mode)
{
    switch (mode)
    {
    case MapOrientationMode::NorthUp:
        return "north_up";
    case MapOrientationMode::TrackUp:
        return "track_up";
    case MapOrientationMode::FreeRotate:
        return "free_rotate";
    }
    return "north_up";
}

void apply_workspace_layout(UiState &ui_state,
                            Map2DCamera &map_camera,
                            Options &options,
                            PlanVisualizationState &plan_state,
                            bool &state_colors,
                            bool &highlight_fallback,
                            const WorkspaceMode mode)
{
    AppWorkspaceLayout &layout = workspace_layout(ui_state, mode);
    ui_state.active_mode = [&layout]()
    {
        if (layout.active_panel == "layers")
        {
            return UiNavigationMode::Layers;
        }
        if (layout.active_panel == "telemetry")
        {
            return UiNavigationMode::Telemetry;
        }
        if (layout.active_panel == "signals")
        {
            return UiNavigationMode::Signals;
        }
        if (layout.active_panel == "capture")
        {
            return UiNavigationMode::Capture;
        }
        if (layout.active_panel == "settings")
        {
            return UiNavigationMode::Settings;
        }
        if (layout.active_panel == "developer")
        {
            return UiNavigationMode::Developer;
        }
        return UiNavigationMode::View;
    }();
    ui_state.view_mode = view_mode_from_config_value(layout.view_mode);
    map_camera.orientation = map_orientation_from_layout_value(layout.map_orientation);
    options.plots.visible = layout.plot_shelf_visible;
    options.plots.height_px = layout.plot_shelf_height_px;
    ui_state.timeline_visible = layout.timeline_visible;
    ui_state.timeline_height_px = layout.timeline_height_px;
    ui_state.inspector_visible = layout.inspector_visible;
    switch (layout.bottom_drawer_state)
    {
    case BottomDrawerState::Hidden:
        options.plots.visible = false;
        ui_state.timeline_visible = false;
        break;
    case BottomDrawerState::Collapsed:
        options.plots.visible = false;
        ui_state.timeline_visible = true;
        ui_state.timeline_height_px = 28.0F;
        break;
    case BottomDrawerState::Compact:
        options.plots.visible = true;
        options.plots.height_px = 160.0F;
        ui_state.timeline_visible = true;
        ui_state.timeline_height_px = 28.0F;
        break;
    case BottomDrawerState::Expanded:
        options.plots.visible = true;
        options.plots.height_px = std::max(layout.plot_shelf_height_px, 320.0F);
        ui_state.timeline_visible = true;
        ui_state.timeline_height_px = std::max(layout.timeline_height_px, 160.0F);
        break;
    }
    ui_state.developer_diagnostics_visible = mode == WorkspaceMode::Developer;
    ui_state.telemetry_diagnostics_visible =
        mode == WorkspaceMode::Analyze || mode == WorkspaceMode::Developer;
    if (mode == WorkspaceMode::Plan)
    {
        plan_state.overlay_visible = true;
        ui_state.layers.planned_route_visible = true;
        ui_state.layers.geofence_rally_visible = true;
    }
    if (mode == WorkspaceMode::Terrain)
    {
        state_colors = true;
        highlight_fallback = true;
        ui_state.layers.tile_state_debug_visible = true;
        ui_state.layers.fallback_highlight_visible = true;
    }
    if (mode == WorkspaceMode::Export)
    {
        ui_state.inspector_visible = false;
    }
    sanitize_active_mode(ui_state);
    request_workspace_layout_restore(ui_state);
}

void capture_workspace_layout(UiState &ui_state,
                              const Map2DCamera &map_camera,
                              const Options &options)
{
    AppWorkspaceLayout &layout = workspace_layout(ui_state, ui_state.workspace_mode);
    switch (ui_state.active_mode)
    {
    case UiNavigationMode::View:
        layout.active_panel = "view";
        break;
    case UiNavigationMode::Layers:
        layout.active_panel = "layers";
        break;
    case UiNavigationMode::Telemetry:
        layout.active_panel = "telemetry";
        break;
    case UiNavigationMode::Signals:
        layout.active_panel = "signals";
        break;
    case UiNavigationMode::Capture:
        layout.active_panel = "capture";
        break;
    case UiNavigationMode::Settings:
        layout.active_panel = "settings";
        break;
    case UiNavigationMode::Developer:
        layout.active_panel = "developer";
        break;
    }
    layout.view_mode = view_mode_config_value(ui_state.view_mode);
    layout.map_orientation = map_orientation_layout_value(map_camera.orientation);
    layout.plot_shelf_visible = options.plots.visible;
    layout.plot_shelf_height_px = options.plots.height_px;
    layout.timeline_visible = ui_state.timeline_visible;
    layout.timeline_height_px = ui_state.timeline_height_px;
    layout.inspector_visible = ui_state.inspector_visible;
    if (!options.plots.visible && !ui_state.timeline_visible)
    {
        layout.bottom_drawer_state = BottomDrawerState::Hidden;
    }
    else if (!options.plots.visible && ui_state.timeline_height_px <= 40.0F)
    {
        layout.bottom_drawer_state = BottomDrawerState::Collapsed;
    }
    else if (options.plots.visible && options.plots.height_px >= 300.0F)
    {
        layout.bottom_drawer_state = BottomDrawerState::Expanded;
    }
    else
    {
        layout.bottom_drawer_state = BottomDrawerState::Compact;
    }
}

AppWindowRect clamp_window_rect(const AppWindowRect &requested,
                                const AppWindowRect &fallback,
                                const ImVec2 display,
                                const ImVec2 min_size)
{
    AppWindowRect rect = requested.width > 0.0F && requested.height > 0.0F ? requested : fallback;
    rect.width = std::clamp(rect.width, min_size.x, std::max(min_size.x, display.x - 24.0F));
    rect.height = std::clamp(rect.height, min_size.y, std::max(min_size.y, display.y - 52.0F));
    rect.x = std::clamp(rect.x, 0.0F, std::max(0.0F, display.x - rect.width));
    rect.y =
        std::clamp(rect.y, status_bar_height, std::max(status_bar_height, display.y - rect.height));
    return rect;
}

void capture_current_window_rect(AppWindowRect &rect)
{
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    rect = {pos.x, pos.y, size.x, size.y};
}

void workspace_button(UiState &ui_state,
                      Options &options,
                      Map2DCamera &map_camera,
                      PlanVisualizationState &plan_state,
                      bool &state_colors,
                      bool &highlight_fallback,
                      const WorkspaceMode mode)
{
    const bool selected = ui_state.workspace_mode == mode;
    ImGui::PushID(workspace_label(mode));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0F, 5.0F));
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.28F, 0.34F, 0.96F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.19F, 0.34F, 0.41F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86F, 0.94F, 0.98F, 1.0F));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09F, 0.105F, 0.115F, 0.55F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15F, 0.17F, 0.18F, 0.88F));
        ImGui::PushStyleColor(ImGuiCol_Text, text_muted);
    }
    if (ImGui::Button(workspace_label(mode), ImVec2(0.0F, 0.0F)))
    {
        capture_workspace_layout(ui_state, map_camera, options);
        ui_state.workspace_mode = mode;
        apply_workspace_layout(
            ui_state, map_camera, options, plan_state, state_colors, highlight_fallback, mode);
        ui_state.workspace_layout_applied = true;
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

ImVec4 telemetry_state_color(const TelemetryPlaybackState &playback)
{
    if (!playback.loaded)
    {
        return ImVec4(0.55F, 0.58F, 0.61F, 1.0F);
    }
    if (!playback.live)
    {
        return playback.clock.paused() ? ImVec4(0.42F, 0.62F, 0.82F, 1.0F)
                                       : ImVec4(0.38F, 0.78F, 0.58F, 1.0F);
    }
    if (!playback.receiver_stats.connected)
    {
        return ImVec4(0.55F, 0.58F, 0.61F, 1.0F);
    }
    return playback.receiver_stats.stale ? ImVec4(0.95F, 0.58F, 0.22F, 1.0F)
                                         : ImVec4(0.38F, 0.78F, 0.58F, 1.0F);
}

std::string entity_label(const animus::telemetry_core::EntityId id)
{
    return std::to_string(id.system_id) + ":" + std::to_string(id.component_id);
}

std::string lower_ascii(std::string text)
{
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](const unsigned char value)
                   { return static_cast<char>(std::tolower(value)); });
    return text;
}

bool entity_matches_filter(const animus::telemetry_core::Entity &entity, const char *filter)
{
    const std::string needle = lower_ascii(filter == nullptr ? std::string() : std::string(filter));
    if (needle.empty())
    {
        return true;
    }
    const std::string haystack = lower_ascii(entity_label(entity.id) + " generic rc plane vehicle");
    return haystack.find(needle) != std::string::npos;
}

std::string bool_label(const bool value)
{
    return value ? "on" : "off";
}

void detail_popup(const char *id, const std::string &text)
{
    ImGui::PushID(id);
    if (ImGui::SmallButton("details"))
    {
        ImGui::OpenPopup("details");
    }
    if (ImGui::BeginPopup("details"))
    {
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void table_text(const char *text)
{
    ImGui::TextUnformatted(text);
}

void load_plan_into_state(PlanVisualizationState &plan_state)
{
    const std::filesystem::path path(plan_state.path.data());
    if (path.empty())
    {
        plan_state.data.reset();
        plan_state.loaded_path.clear();
        plan_state.error = "plan path is empty";
        plan_state.diagnostics.clear();
        return;
    }
    const PlanVisualizationLoadResult result = load_plan_visualization(path);
    plan_state.data = result.data;
    plan_state.diagnostics = result.diagnostics;
    plan_state.error = result.error;
    plan_state.loaded_path = result.data ? path : std::filesystem::path{};
}

void draw_plan_controls(PlanVisualizationState &plan_state,
                        const TelemetryPlaybackState &playback,
                        const UiState &ui_state)
{
    ImGui::SeparatorText("Mission Plan");
    ImGui::Checkbox("Plan overlay", &plan_state.overlay_visible);
    ImGui::InputText("Plan path", plan_state.path.data(), plan_state.path.size());
    if (ImGui::Button("Load"))
    {
        load_plan_into_state(plan_state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        if (plan_state.path[0] == '\0' && !plan_state.loaded_path.empty())
        {
            std::snprintf(plan_state.path.data(),
                          plan_state.path.size(),
                          "%s",
                          plan_state.loaded_path.string().c_str());
        }
        load_plan_into_state(plan_state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        plan_state.data.reset();
        plan_state.loaded_path.clear();
        plan_state.error.clear();
        plan_state.diagnostics.clear();
        plan_state.path[0] = '\0';
    }

    if (!plan_state.error.empty())
    {
        ImGui::TextWrapped("error: %s", plan_state.error.c_str());
        return;
    }
    if (!plan_state.data)
    {
        muted_text("No plan loaded.");
        return;
    }

    const PlanVisualizationData &plan = *plan_state.data;
    ImGui::Text("waypoints %zu  distance %s",
                plan.mission_waypoints.size(),
                format_route_distance(plan.route_distance_m).c_str());
    ImGui::Text("geofence %zu/%zu  rally %zu  unsupported %zu",
                plan.geofence_polygons.size(),
                plan.geofence_circles.size(),
                plan.rally_points.size(),
                plan.unsupported_item_count);
    if (!plan_state.loaded_path.empty())
    {
        ImGui::TextWrapped("%s", plan_state.loaded_path.string().c_str());
    }
    std::optional<PlanActualAggregate> aggregate;
    if (playback.loaded && ui_state.telemetry_entity_selected)
    {
        const auto *track = playback.timeline.track_for(playback.selected_entity);
        if (track != nullptr)
        {
            aggregate = compare_plan_actual(plan, *track, playback.clock.time_s());
        }
    }
    const PlanRouteProfileSummary profile = plan_route_profile_summary(plan, aggregate);
    ImGui::SeparatorText("Route profile");
    ImGui::Text("route distance %s", format_route_distance(profile.route_distance_m).c_str());
    if (!profile.telemetry_compared)
    {
        muted_text("No selected telemetry comparison.");
    }
    else
    {
        ImGui::Text("completion %s  compared %zu",
                    format_percent(profile.route_completion_ratio).c_str(),
                    profile.compared_samples);
        ImGui::Text("cross-track avg/max %s / %s",
                    format_distance_m(profile.average_cross_track_error_m).c_str(),
                    format_distance_m(profile.max_cross_track_error_m).c_str());
        if (profile.active_from_waypoint_index && profile.active_to_waypoint_index)
        {
            ImGui::Text("active leg %zu->%zu",
                        *profile.active_from_waypoint_index + 1U,
                        *profile.active_to_waypoint_index + 1U);
        }
        if (profile.current_altitude_error_m)
        {
            ImGui::Text("altitude error %s",
                        format_distance_m(std::abs(*profile.current_altitude_error_m)).c_str());
        }
    }
    ImGui::TextColored(
        text_muted, "clearance/elevation profile %s", profile.clearance_profile_status.c_str());
    for (const std::string &diagnostic : plan_state.diagnostics)
    {
        ImGui::TextWrapped("warning: %s", diagnostic.c_str());
    }
}

void apply_layer_preset(const char *preset,
                        const Options &options,
                        UiState &ui_state,
                        bool plan_loaded,
                        bool &state_colors,
                        bool &highlight_fallback,
                        bool &overlay_enabled,
                        float &overlay_opacity)
{
    const std::string name(preset);
    if (name == "Operator Clean")
    {
        ui_state.layers = layer_preset_operator_clean(ui_state.layers, plan_loaded);
    }
    else if (name == "Terrain Analysis")
    {
        ui_state.layers =
            layer_preset_terrain_analysis(ui_state.layers, !options.bathymetry_geotiff.empty());
    }
    else if (name == "Mission Review")
    {
        ui_state.layers = layer_preset_mission_review(ui_state.layers, plan_loaded);
    }
    else if (name == "Debug Tiles")
    {
        ui_state.layers = layer_preset_debug_tiles(ui_state.layers);
    }
    else if (name == "Capture Mode")
    {
        ui_state.layers = layer_preset_capture_mode(ui_state.layers, plan_loaded);
    }
    ui_state.telemetry_tracks_visible = ui_state.layers.track_tail_visible;
    ui_state.telemetry_labels_visible = ui_state.layers.vehicle_labels_visible;
    ui_state.bathymetry_enabled = ui_state.layers.bathymetry_visible;
    state_colors = ui_state.layers.tile_state_debug_visible;
    highlight_fallback = ui_state.layers.fallback_highlight_visible;
    overlay_enabled = ui_state.layers.geotiff_overlay_visible;
    overlay_opacity = ui_state.layers.geotiff_overlay_opacity;
}

const char *altitude_datum_label(const animus::telemetry_core::AltitudeDatum datum)
{
    switch (datum)
    {
    case animus::telemetry_core::AltitudeDatum::Unknown:
        return "unknown";
    case animus::telemetry_core::AltitudeDatum::MslOrthometric:
        return "MSL";
    case animus::telemetry_core::AltitudeDatum::Ellipsoid:
        return "ellipsoid";
    case animus::telemetry_core::AltitudeDatum::TerrainRelative:
        return "terrain";
    }
    return "unknown";
}

std::optional<double> sample_altitude_m(const animus::telemetry_core::TelemetrySample &sample)
{
    if (sample.altitude_relative_m)
    {
        return sample.altitude_relative_m;
    }
    return sample.altitude_msl_m;
}

std::optional<animus::telemetry_core::TelemetrySample>
current_entity_sample(const TelemetryPlaybackState &playback,
                      const animus::telemetry_core::EntityId id)
{
    return playback.timeline.sample_at(id, playback.clock.time_s());
}

double entity_age_s(const TelemetryPlaybackState &playback,
                    const animus::telemetry_core::TelemetrySample &sample)
{
    if (playback.timeline.end_time_s <= 0.0)
    {
        return 0.0;
    }
    return std::max(0.0, playback.timeline.end_time_s - sample.time_s);
}

bool entity_stale(const TelemetryPlaybackState &playback,
                  const animus::telemetry_core::TelemetrySample &sample)
{
    if (!playback.live)
    {
        return false;
    }
    return playback.receiver_stats.stale || entity_age_s(playback, sample) > 2.0;
}

bool entity_degraded(const animus::telemetry_core::TelemetrySample &sample)
{
    return !sample.fields.position;
}

ImU32 marker_color(const TimelineReviewMarkerCategory category)
{
    switch (category)
    {
    case TimelineReviewMarkerCategory::Gap:
        return IM_COL32(238, 158, 74, 255);
    case TimelineReviewMarkerCategory::Degraded:
        return IM_COL32(224, 190, 84, 255);
    case TimelineReviewMarkerCategory::ImportWarning:
        return IM_COL32(230, 196, 80, 255);
    case TimelineReviewMarkerCategory::ImportError:
        return IM_COL32(235, 86, 86, 255);
    case TimelineReviewMarkerCategory::Bookmark:
        return IM_COL32(135, 196, 255, 255);
    case TimelineReviewMarkerCategory::MinClearance:
        return IM_COL32(93, 214, 145, 255);
    case TimelineReviewMarkerCategory::MaxSpeed:
        return IM_COL32(187, 142, 255, 255);
    case TimelineReviewMarkerCategory::LowClearance:
        return IM_COL32(238, 186, 74, 255);
    case TimelineReviewMarkerCategory::Attitude:
        return IM_COL32(235, 122, 92, 255);
    case TimelineReviewMarkerCategory::FrameTime:
        return IM_COL32(170, 188, 204, 255);
    case TimelineReviewMarkerCategory::PlanDeviation:
    case TimelineReviewMarkerCategory::PlanAltitude:
    case TimelineReviewMarkerCategory::Geofence:
        return IM_COL32(236, 186, 82, 255);
    case TimelineReviewMarkerCategory::LowLinkHz:
        return IM_COL32(238, 158, 74, 255);
    case TimelineReviewMarkerCategory::TerrainFallback:
        return IM_COL32(210, 176, 92, 255);
    case TimelineReviewMarkerCategory::SpeedExcursion:
    case TimelineReviewMarkerCategory::ClimbExcursion:
        return IM_COL32(235, 122, 92, 255);
    case TimelineReviewMarkerCategory::ModelFallback:
        return IM_COL32(187, 142, 255, 255);
    case TimelineReviewMarkerCategory::Capture:
        return IM_COL32(135, 196, 255, 255);
    }
    return IM_COL32(180, 186, 192, 255);
}

void request_review_marker_jump(UiState &ui,
                                const std::vector<TimelineReviewMarker> &markers,
                                const std::size_t index)
{
    if (index >= markers.size())
    {
        return;
    }
    ui.selected_review_marker_index = index;
    ui.request_timeline_seek_time_s = markers[index].time_s;
}

std::size_t visible_review_marker_count(const std::vector<TimelineReviewMarker> &markers,
                                        const TimelineReviewFilterState &filters)
{
    return static_cast<std::size_t>(
        std::count_if(markers.begin(),
                      markers.end(),
                      [&filters](const TimelineReviewMarker &marker)
                      { return timeline_review_marker_visible(marker, filters); }));
}

void draw_series_chart(const TimelineReviewSeries &series, const ImVec2 size)
{
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImGui::Dummy(size);
    draw->AddRectFilled(min, max, IM_COL32(14, 18, 21, 190), 5.0F);
    draw->AddRect(min, max, IM_COL32(58, 66, 72, 220), 5.0F);
    draw->AddText(
        ImVec2(min.x + 8.0F, min.y + 6.0F), IM_COL32(210, 218, 224, 240), series.label.c_str());
    if (series.points.size() < 2U)
    {
        draw->AddText(
            ImVec2(min.x + 8.0F, min.y + 28.0F), IM_COL32(140, 148, 155, 230), "unavailable");
        return;
    }

    double min_time = series.points.front().time_s;
    double max_time = series.points.back().time_s;
    double min_value = series.points.front().value;
    double max_value = series.points.front().value;
    for (const auto &point : series.points)
    {
        min_time = std::min(min_time, point.time_s);
        max_time = std::max(max_time, point.time_s);
        min_value = std::min(min_value, point.value);
        max_value = std::max(max_value, point.value);
    }
    if (max_time <= min_time || max_value <= min_value)
    {
        max_value = min_value + 1.0;
    }

    const ImVec2 plot_min(min.x + 8.0F, min.y + 26.0F);
    const ImVec2 plot_max(max.x - 8.0F, max.y - 18.0F);
    ImVec2 previous{};
    bool have_previous = false;
    for (const auto &point : series.points)
    {
        const float x =
            plot_min.x + static_cast<float>((point.time_s - min_time) / (max_time - min_time)) *
                             (plot_max.x - plot_min.x);
        const float y =
            plot_max.y - static_cast<float>((point.value - min_value) / (max_value - min_value)) *
                             (plot_max.y - plot_min.y);
        const ImVec2 current(x, y);
        if (have_previous)
        {
            draw->AddLine(previous, current, IM_COL32(99, 175, 220, 255), 1.6F);
        }
        previous = current;
        have_previous = true;
    }
    const std::string range = format_value("%.1f", min_value) + ".." +
                              format_value("%.1f", max_value) + " " + series.unit;
    draw->AddText(ImVec2(min.x + 8.0F, max.y - 15.0F), IM_COL32(143, 151, 158, 230), range.c_str());
}

void draw_review_charts(const TimelineReviewData &review)
{
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "Review charts");
    const float width = ImGui::GetContentRegionAvail().x;
    draw_series_chart(review.altitude, ImVec2(width, 76.0F));
    draw_series_chart(review.ground_speed, ImVec2(width, 76.0F));
    draw_series_chart(review.terrain_clearance, ImVec2(width, 76.0F));
}

void draw_ghost_replay_foundation(const PlanVisualizationState &plan_state,
                                  TelemetryPlaybackState &playback,
                                  const Options &options,
                                  UiState &ui)
{
    ImGui::SeparatorText("Ghost replay");
    ImGui::Checkbox("Show ghost track", &ui.ghost_layer_visible);
    if (ImGui::Button("Load recent baseline"))
    {
        playback.ghost_diagnostic.clear();
        if (ui.ghost_recent_baseline_path.empty() ||
            !std::filesystem::exists(ui.ghost_recent_baseline_path))
        {
            playback.ghost_baseline.reset();
            playback.ghost_diagnostic = "baseline path does not exist";
        }
        else
        {
            try
            {
                playback.ghost_baseline = animus::telemetry_core::load_telemetry(
                    ui.ghost_recent_baseline_path, options.telemetry_format);
            }
            catch (const std::exception &error)
            {
                playback.ghost_baseline.reset();
                playback.ghost_diagnostic = error.what();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear baseline"))
    {
        playback.ghost_baseline.reset();
    }
    if (!playback.ghost_diagnostic.empty())
    {
        muted_text(playback.ghost_diagnostic.c_str());
    }
    if (!plan_state.data || !playback.loaded || !ui.telemetry_entity_selected)
    {
        muted_text("Plan-vs-actual and ghost replay unavailable.");
        return;
    }
    const auto *track = playback.timeline.track_for(playback.selected_entity);
    if (track == nullptr)
    {
        muted_text("Plan-vs-actual and ghost replay unavailable.");
        return;
    }

    GhostReplayComparison comparison;
    if (playback.ghost_baseline)
    {
        comparison = compare_ghost_replay(playback.timeline,
                                          *playback.ghost_baseline,
                                          playback.selected_entity,
                                          &*plan_state.data);
    }
    else
    {
        comparison.current =
            summarize_telemetry_run(playback.timeline, playback.selected_entity, &*plan_state.data);
    }
    if (!playback.ghost_baseline)
    {
        muted_text("No baseline loaded.");
    }
    ImGui::Text("current duration %.1f s  distance %s",
                comparison.current.duration_s,
                format_route_distance(comparison.current.distance_m).c_str());
    if (comparison.current.route_completion_ratio)
    {
        ImGui::Text("route completion %s",
                    format_percent(*comparison.current.route_completion_ratio).c_str());
    }
    if (playback.ghost_baseline)
    {
        ImGui::Text("baseline duration %.1f s  distance %s",
                    comparison.baseline.duration_s,
                    format_route_distance(comparison.baseline.distance_m).c_str());
        ImGui::Text("delta duration %.1f s  distance %s",
                    comparison.duration_delta_s,
                    format_route_distance(comparison.distance_delta_m).c_str());
    }
}

void draw_review_jump_buttons(const TimelineReviewData &review, UiState &ui)
{
    const auto min_clearance =
        std::find_if(review.markers.begin(),
                     review.markers.end(),
                     [](const TimelineReviewMarker &marker)
                     { return marker.category == TimelineReviewMarkerCategory::MinClearance; });
    const auto gap = std::find_if(review.markers.begin(),
                                  review.markers.end(),
                                  [](const TimelineReviewMarker &marker)
                                  { return marker.category == TimelineReviewMarkerCategory::Gap; });
    if (review.min_clearance_marker)
    {
        if (ImGui::Button("Min clearance"))
        {
            request_review_marker_jump(
                ui,
                review.markers,
                static_cast<std::size_t>(min_clearance - review.markers.begin()));
        }
        ImGui::SameLine();
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Min clearance");
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (gap != review.markers.end())
    {
        if (ImGui::Button("Telemetry gap"))
        {
            request_review_marker_jump(
                ui, review.markers, static_cast<std::size_t>(gap - review.markers.begin()));
        }
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Telemetry gap");
        ImGui::EndDisabled();
    }
}

void same_line_if_room()
{
    if (ImGui::GetContentRegionAvail().x > 150.0F)
    {
        ImGui::SameLine();
    }
}

void draw_top_status_bar(const Options &options,
                         const animus::render_core::RenderStats &stats,
                         const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                         const RuntimeSignalInputs &runtime,
                         const TelemetryPlaybackState &playback,
                         const PlanVisualizationState &plan_state,
                         const VehicleRuntimeStatus &vehicle_status,
                         const ScreenshotToolState &screenshot_tool,
                         const Mp4RecorderState &recorder,
                         const UiState &ui_state,
                         std::size_t resident_gpu_bytes)
{
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, status_bar_height),
                             ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055F, 0.065F, 0.073F, 0.94F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 7.0F));
    ImGui::Begin("Animus Status", nullptr, flags);
    const auto pills = build_status_ribbon_model(options,
                                                 options.status_thresholds,
                                                 snapshot,
                                                 runtime,
                                                 playback,
                                                 ui_state,
                                                 plan_state,
                                                 vehicle_status,
                                                 screenshot_tool,
                                                 recorder,
                                                 resident_gpu_bytes);
    for (const auto &pill : pills)
    {
        const std::string text = pill.label + " " + pill.summary;
        if (draw_status_pill(
                pill.id.c_str(), text.c_str(), pill_state_from_status_level(pill.level)))
        {
            ImGui::Text("%s: %s", pill.label.c_str(), status_ribbon_level_label(pill.level));
            for (const std::string &detail : pill.details)
            {
                ImGui::TextWrapped("%s", detail.c_str());
            }
            if (!pill.action.empty())
            {
                ImGui::Separator();
                ImGui::TextWrapped("%s", pill.action.c_str());
            }
            ImGui::EndPopup();
        }
        same_line_if_room();
    }
    ImGui::TextColored(
        text_muted, "Frame %d %.1f ms", stats.frame_count(), stats.last_frame_seconds() * 1000.0);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void draw_nav(UiState &ui_state,
              Options &options,
              Map2DCamera &map_camera,
              PlanVisualizationState &plan_state,
              bool &state_colors,
              bool &highlight_fallback)
{
    sanitize_active_mode(ui_state);
    ImGui::SetNextWindowPos(ImVec2(chrome_margin, status_bar_height + chrome_margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(nav_width, 540.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panel_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, panel_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 12.0F));
    ImGui::Begin("Navigate", nullptr, flags);
    workspace_button(ui_state,
                     options,
                     map_camera,
                     plan_state,
                     state_colors,
                     highlight_fallback,
                     WorkspaceMode::FlyTest);
    workspace_button(ui_state,
                     options,
                     map_camera,
                     plan_state,
                     state_colors,
                     highlight_fallback,
                     WorkspaceMode::Plan);
    workspace_button(ui_state,
                     options,
                     map_camera,
                     plan_state,
                     state_colors,
                     highlight_fallback,
                     WorkspaceMode::Analyze);
    workspace_button(ui_state,
                     options,
                     map_camera,
                     plan_state,
                     state_colors,
                     highlight_fallback,
                     WorkspaceMode::Terrain);
    workspace_button(ui_state,
                     options,
                     map_camera,
                     plan_state,
                     state_colors,
                     highlight_fallback,
                     WorkspaceMode::Export);
    workspace_button(ui_state,
                     options,
                     map_camera,
                     plan_state,
                     state_colors,
                     highlight_fallback,
                     WorkspaceMode::Developer);
    ImGui::Separator();
    nav_button(ui_state, UiNavigationMode::View);
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    nav_button(ui_state, UiNavigationMode::Layers);
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    nav_button(ui_state, UiNavigationMode::Telemetry);
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    nav_button(ui_state, UiNavigationMode::Signals);
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    nav_button(ui_state, UiNavigationMode::Capture);
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    nav_button(ui_state, UiNavigationMode::Settings);
    if (mode_visible_in_workspace(UiNavigationMode::Developer, ui_state.workspace_mode))
    {
        ImGui::Separator();
        nav_button(ui_state, UiNavigationMode::Developer);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void draw_settings_panel(const Options &options, UiState &ui_state)
{
    ImGui::Text("Config");
    ImGui::TextWrapped("%s", options.config_path.string().c_str());
    ImGui::Separator();
    ImGui::Text("load %s", options.config_load_status.c_str());
    ImGui::Text("save %s", options.config_save_status.c_str());
    ImGui::Text("dirty %s", options.config_dirty ? "yes" : "no");
    if (ImGui::Button("Save"))
    {
        ui_state.request_config_save = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As Default"))
    {
        ui_state.request_config_save_default = true;
    }
    if (ImGui::Button("Reload"))
    {
        ui_state.request_config_reload = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        ui_state.request_config_reset = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Layout"))
    {
        ui_state.request_workspace_layout_reset = true;
    }
    if (!options.config_diagnostics.empty())
    {
        ImGui::SeparatorText("Warnings");
        const std::size_t first =
            options.config_diagnostics.size() > 8U ? options.config_diagnostics.size() - 8U : 0U;
        for (std::size_t index = first; index < options.config_diagnostics.size(); ++index)
        {
            ImGui::TextWrapped("%s", options.config_diagnostics[index].c_str());
        }
    }
}

void draw_view_panel(const Options &options,
                     const std::filesystem::path &pack_root,
                     const Camera &camera,
                     Map2DCamera &map_camera,
                     int selected_zoom,
                     const TelemetryPlaybackState &playback,
                     PlanVisualizationState &plan_state,
                     UiState &ui_state,
                     bool &state_colors,
                     bool &highlight_fallback)
{
    ImGui::Text("Viewport");
    if (ImGui::Button("3D"))
    {
        ui_state.view_mode = ViewMode::Terrain3D;
    }
    ImGui::SameLine();
    if (ImGui::Button("2D"))
    {
        ui_state.view_mode = ViewMode::Map2D;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::Button("2.5D");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(text_muted, "%s", view_mode_label(ui_state.view_mode));

    if (ui_state.view_mode == ViewMode::Map2D)
    {
        ImGui::SeparatorText("Map");
        if (ImGui::Button("North"))
        {
            ui_state.view_mode = ViewMode::Map2D;
            map_camera.orientation = MapOrientationMode::NorthUp;
        }
        ImGui::SameLine();
        if (ImGui::Button("Track"))
        {
            map_camera.orientation = MapOrientationMode::TrackUp;
        }
        ImGui::SameLine();
        if (ImGui::Button("Free"))
        {
            map_camera.orientation = MapOrientationMode::FreeRotate;
        }
        ImGui::SameLine();
        ImGui::TextColored(text_muted, "%s", orientation_label(map_camera.orientation));
    }
    ImGui::SeparatorText("Actions");
    ImGui::Checkbox("Follow selected", &ui_state.follow_selected_entity);
    if (ImGui::Button("Fit all"))
    {
        ui_state.request_fit_all_entities = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Latest"))
    {
        ui_state.request_jump_latest_sample = true;
    }
    if (ImGui::Button("Home"))
    {
        ui_state.request_home_view = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("-"))
    {
        ui_state.zoom_steps -= 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("+"))
    {
        ui_state.zoom_steps += 1;
    }
    ImGui::Separator();
    ImGui::Text("zoom %d  range %d..%d", selected_zoom, options.min_z, options.max_z);
    if (ui_state.view_mode == ViewMode::Map2D)
    {
        ImGui::Text("map %.2f %.2f  scale %.2f rot %.1f deg",
                    map_camera.target_x,
                    map_camera.target_z,
                    map_camera.distance,
                    map_camera.rotation_rad * 180.0F / 3.1415926535F);
    }
    else
    {
        ImGui::Text("camera %.2f %.2f %.2f  distance %.2f",
                    camera.target.x,
                    camera.target.y,
                    camera.target.z,
                    camera.distance);
    }
    draw_plan_controls(plan_state, playback, ui_state);
    ImGui::Separator();
    ImGui::Text("pack");
    ImGui::TextWrapped("%s", pack_root.string().c_str());
    ImGui::Text(
        "center %d/%d  height %.6f", options.center_x, options.center_y, options.height_scale);
    ImGui::Checkbox("State colors", &state_colors);
    ImGui::SameLine();
    ImGui::Checkbox("Fallback highlight", &highlight_fallback);
    ui_state.layers.tile_state_debug_visible = state_colors;
    ui_state.layers.fallback_highlight_visible = highlight_fallback;
}

void draw_layer_row(const char *name,
                    const char *visible,
                    const char *opacity,
                    const char *order,
                    const char *source,
                    const char *health,
                    const std::string &details)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    table_text(name);
    ImGui::TableSetColumnIndex(1);
    table_text(visible);
    ImGui::TableSetColumnIndex(2);
    table_text(opacity);
    ImGui::TableSetColumnIndex(3);
    table_text(order);
    ImGui::TableSetColumnIndex(4);
    table_text(source);
    ImGui::TableSetColumnIndex(5);
    table_text(health);
    ImGui::TableSetColumnIndex(6);
    detail_popup(name, details);
}

void draw_layer_stack(const Options &options,
                      const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                      const TelemetryPlaybackState &playback,
                      PlanVisualizationState &plan_state,
                      UiState &ui_state,
                      bool &state_colors,
                      bool &highlight_fallback,
                      bool &overlay_enabled,
                      float &overlay_opacity)
{
    ImGui::SeparatorText("Presets");
    constexpr std::array<const char *, 5> presets = {
        "Operator Clean",
        "Terrain Analysis",
        "Mission Review",
        "Debug Tiles",
        "Capture Mode",
    };
    for (const char *preset : presets)
    {
        if (ImGui::Button(preset))
        {
            apply_layer_preset(preset,
                               options,
                               ui_state,
                               plan_state.data.has_value(),
                               state_colors,
                               highlight_fallback,
                               overlay_enabled,
                               overlay_opacity);
        }
        if (ImGui::GetContentRegionAvail().x > 155.0F)
        {
            ImGui::SameLine();
        }
    }

    ImGui::SeparatorText("Display");
    ImGui::Checkbox("Vehicle icons", &ui_state.layers.vehicle_icons_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Vehicle labels", &ui_state.layers.vehicle_labels_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Track tail", &ui_state.layers.track_tail_visible);
    int tail_points = static_cast<int>(std::min<std::size_t>(ui_state.selected_entity_tail_points,
                                                             static_cast<std::size_t>(100000U)));
    if (ImGui::SliderInt("Selected tail points", &tail_points, 2, 5000))
    {
        ui_state.selected_entity_tail_points = static_cast<std::size_t>(tail_points);
        ui_state.layers.selected_entity_tail_points = ui_state.selected_entity_tail_points;
    }
    ImGui::Checkbox("Heading vectors", &ui_state.layers.heading_vectors_visible);
    ImGui::SeparatorText("Mission");
    ImGui::Checkbox("Planned route", &ui_state.layers.planned_route_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Geofence/rally", &ui_state.layers.geofence_rally_visible);
    ImGui::SeparatorText("Terrain");
    ImGui::Checkbox("Terrain confidence", &ui_state.layers.terrain_confidence_visible);
    ImGui::BeginDisabled();
    ImGui::Checkbox("Clearance heatmap", &ui_state.layers.terrain_clearance_heatmap_visible);
    ImGui::EndDisabled();
    ImGui::Checkbox("GeoTIFF overlay", &ui_state.layers.geotiff_overlay_visible);
    ImGui::SliderFloat(
        "GeoTIFF opacity", &ui_state.layers.geotiff_overlay_opacity, 0.0F, 1.0F, "%.2f");
    bool bathymetry_mutable = ui_state.layers.bathymetry_visible;
    if (!options.use_bathymetry || options.bathymetry_geotiff.empty())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Bathymetry", &bathymetry_mutable))
    {
        ui_state.layers.bathymetry_visible = bathymetry_mutable;
    }
    if (!options.use_bathymetry || options.bathymetry_geotiff.empty())
    {
        ImGui::EndDisabled();
        if (options.use_bathymetry && options.bathymetry_geotiff.empty())
        {
            ImGui::SameLine();
            muted_text("not configured");
        }
    }
    ImGui::Checkbox("Hillshade", &ui_state.layers.hillshade_visible);
    ImGui::SeparatorText("Debug");
    ImGui::Checkbox("Tile state debug", &ui_state.layers.tile_state_debug_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Fallback highlight", &ui_state.layers.fallback_highlight_visible);

    ui_state.telemetry_tracks_visible = ui_state.layers.track_tail_visible;
    ui_state.telemetry_labels_visible = ui_state.layers.vehicle_labels_visible;
    ui_state.bathymetry_enabled = ui_state.layers.bathymetry_visible;
    state_colors = ui_state.layers.tile_state_debug_visible;
    highlight_fallback = ui_state.layers.fallback_highlight_visible;
    overlay_enabled = ui_state.layers.geotiff_overlay_visible;
    overlay_opacity = ui_state.layers.geotiff_overlay_opacity;
    plan_state.overlay_visible =
        ui_state.layers.planned_route_visible || ui_state.layers.geofence_rally_visible;

    LayerStackContext context;
    context.telemetry_loaded = playback.loaded;
    context.telemetry_live = playback.live;
    context.plan_loaded = plan_state.data.has_value();
    context.plan_error = !plan_state.error.empty();
    context.plan_diagnostic_count = plan_state.diagnostics.size();
    if (plan_state.data)
    {
        context.plan_route_points = plan_state.data->mission_waypoints.size();
        context.plan_geofence_items =
            plan_state.data->geofence_polygons.size() + plan_state.data->geofence_circles.size();
        context.plan_rally_points = plan_state.data->rally_points.size();
    }
    context.geotiff_configured = !options.overlay_geotiff.empty() || !options.overlays.empty();
    context.geotiff_missing =
        !options.overlay_geotiff.empty() && !std::filesystem::exists(options.overlay_geotiff);
    context.bathymetry_configured = !options.bathymetry_geotiff.empty();
    context.bathymetry_missing =
        !options.bathymetry_geotiff.empty() && !std::filesystem::exists(options.bathymetry_geotiff);
    context.bathymetry_runtime_enabled = ui_state.bathymetry_enabled;
    context.terrain_tiles_loaded = snapshot.resident_gpu_tiles > 0U;
    context.failed_tiles = snapshot.failed_tiles;
    for (const auto &tile : snapshot.tiles)
    {
        if (tile.state == animus::terrain_core::TileState::UsingFallback)
        {
            ++context.fallback_tiles;
        }
        if (tile.synthetic)
        {
            ++context.synthetic_tiles;
        }
    }
    context.terrain_confidence_available = ui_state.telemetry_entity_selected;
    context.terrain_clearance_available =
        playback.selected_entity_terrain.terrain_clearance_m.has_value();
    const std::vector<LayerStackRow> rows = build_layer_stack_rows(ui_state.layers, context);
    ImGui::SeparatorText("Layer groups");
    for (const LayerStackCategory category : {LayerStackCategory::Display,
                                              LayerStackCategory::Mission,
                                              LayerStackCategory::Terrain,
                                              LayerStackCategory::Debug})
    {
        const std::size_t visible_count = static_cast<std::size_t>(
            std::count_if(rows.begin(),
                          rows.end(),
                          [category](const LayerStackRow &row)
                          { return row.category == category && row.visible; }));
        ImGui::Text("%s %zu", layer_stack_category_label(category), visible_count);
        if (category != LayerStackCategory::Debug)
        {
            ImGui::SameLine();
        }
    }

    if (ImGui::CollapsingHeader("Details"))
    {
        if (ImGui::BeginTable("layer_stack",
                              7,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("layer");
            ImGui::TableSetupColumn("visible");
            ImGui::TableSetupColumn("opacity");
            ImGui::TableSetupColumn("order");
            ImGui::TableSetupColumn("source");
            ImGui::TableSetupColumn("health");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();

            for (const LayerStackRow &row : rows)
            {
                char opacity_text[32]{};
                std::snprintf(opacity_text, sizeof(opacity_text), "%.2f", row.opacity);
                const std::string visible = row.available ? bool_label(row.visible) : "unavailable";
                const std::string health = row.warning == LayerWarningLevel::None
                                               ? row.status
                                               : row.status + " / " + row.warning_badge;
                draw_layer_row(row.label.c_str(),
                               visible.c_str(),
                               row.has_opacity ? opacity_text : "-",
                               row.has_draw_order ? row.order_label.c_str() : "-",
                               row.source.c_str(),
                               health.c_str(),
                               row.details);
            }
            ImGui::EndTable();
        }
    }
}

void draw_map_packs_panel(
    const Options &options,
    const std::filesystem::path &pack_root,
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles)
{
    ImGui::SeparatorText("Map Packs");
    const PrewarmPreview preview = build_prewarm_preview(options, pack_root, visible_tiles);
    ImGui::Text("pack root");
    ImGui::TextWrapped("%s", pack_root.string().c_str());
    ImGui::Text("cache root");
    ImGui::TextWrapped("%s", options.cache_root.string().c_str());
    ImGui::Text("bbox %.6f, %.6f, %.6f, %.6f",
                preview.bounds.west_deg,
                preview.bounds.south_deg,
                preview.bounds.east_deg,
                preview.bounds.north_deg);
    ImGui::Text("zoom %d..%d  estimated tiles %llu  layers %s",
                options.min_z,
                options.max_z,
                static_cast<unsigned long long>(preview.tile_count),
                preview.layers.c_str());
    ImGui::BeginChild("prewarm_command", ImVec2(-1.0F, 72.0F), true);
    ImGui::TextWrapped("%s", preview.command.c_str());
    ImGui::EndChild();
    if (ImGui::Button("Copy prewarm command"))
    {
        ImGui::SetClipboardText(preview.command.c_str());
    }
    ImGui::TextColored(text_muted, "%s", "Preview only. Commands are not executed by the app.");
}

void draw_layer_panel(const Options &options,
                      const std::filesystem::path &pack_root,
                      const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                      const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                      const TelemetryPlaybackState &playback,
                      PlanVisualizationState &plan_state,
                      UiState &ui_state,
                      bool &state_colors,
                      bool &highlight_fallback,
                      bool &overlay_enabled,
                      float &overlay_opacity)
{
    draw_layer_stack(options,
                     snapshot,
                     playback,
                     plan_state,
                     ui_state,
                     state_colors,
                     highlight_fallback,
                     overlay_enabled,
                     overlay_opacity);
    draw_map_packs_panel(options, pack_root, visible_tiles);
}

void draw_timeline_controls(TelemetryPlaybackState &playback, UiState &ui)
{
    bool paused = playback.clock.paused();
    if (ImGui::Checkbox("Paused", &paused))
    {
        playback.clock.set_paused(paused);
    }
    ImGui::SameLine();
    bool looping = playback.clock.looping();
    if (ImGui::Checkbox("Loop", &looping))
    {
        playback.clock.set_looping(looping);
    }
    float rate = static_cast<float>(playback.clock.rate());
    if (ImGui::SliderFloat("Rate", &rate, 0.1F, 16.0F, "%.2fx"))
    {
        playback.clock.set_rate(rate);
    }
    double current_time = playback.clock.time_s();
    if (ImGui::SliderScalar("Time",
                            ImGuiDataType_Double,
                            &current_time,
                            &playback.timeline.start_time_s,
                            &playback.timeline.end_time_s,
                            "%.3f s"))
    {
        ui.request_timeline_seek_time_s = current_time;
    }
}

void draw_entity_list(TelemetryPlaybackState &playback, UiState &ui)
{
    if (playback.timeline.entities.empty())
    {
        muted_text("No telemetry entities.");
        return;
    }

    ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "Entities");
    ImGui::SameLine();
    ImGui::TextColored(text_muted, "%zu", playback.timeline.entities.size());
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##entity_filter",
                             "Filter system:component or type",
                             ui.telemetry_entity_filter.data(),
                             ui.telemetry_entity_filter.size());
    ImGui::Dummy(ImVec2(0.0F, 2.0F));
    std::size_t visible_count = 0U;
    for (const auto &entity : playback.timeline.entities)
    {
        if (!entity_matches_filter(entity, ui.telemetry_entity_filter.data()))
        {
            continue;
        }
        ++visible_count;
        const auto sample = current_entity_sample(playback, entity.id);
        const bool selected = ui.telemetry_entity_selected && entity.id == playback.selected_entity;
        const bool stale = sample ? entity_stale(playback, *sample) : true;
        const bool degraded = !sample || entity_degraded(*sample);
        const ImVec4 state_color = degraded || stale ? stale_amber : live_green;
        const std::string id = entity_label(entity.id);
        ImGui::PushID(id.c_str());
        const float row_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 row_size(row_width, 54.0F);
        ImGui::InvisibleButton("entity_row", row_size);
        if (ImGui::IsItemClicked())
        {
            playback.selected_entity = entity.id;
            ui.telemetry_entity_selected = true;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            playback.selected_entity = entity.id;
            ui.telemetry_entity_selected = true;
            ui.follow_selected_entity = true;
        }
        const ImVec2 row_min = ImGui::GetItemRectMin();
        const ImVec2 row_max = ImGui::GetItemRectMax();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(row_min,
                            row_max,
                            selected ? IM_COL32(28, 55, 68, 232) : IM_COL32(18, 22, 25, 118),
                            7.0F);
        if (selected)
        {
            draw->AddRect(
                row_min, row_max, ImGui::ColorConvertFloat4ToU32(accent_blue), 7.0F, 0, 1.2F);
        }
        draw->AddCircleFilled(ImVec2(row_min.x + 14.0F, row_min.y + 18.0F),
                              4.0F,
                              ImGui::ColorConvertFloat4ToU32(state_color),
                              16);
        draw->AddText(ImVec2(row_min.x + 26.0F, row_min.y + 9.0F),
                      selected ? IM_COL32(238, 247, 252, 255) : IM_COL32(224, 229, 233, 242),
                      id.c_str());
        draw->AddText(ImVec2(row_min.x + row_width - 118.0F, row_min.y + 9.0F),
                      IM_COL32(151, 159, 166, 232),
                      "Generic RC Plane");
        std::string secondary = "no current pose";
        if (sample)
        {
            secondary = format_altitude(sample_altitude_m(*sample), sample->altitude_datum) +
                        "    " + format_speed(sample->ground_speed_mps);
            if (stale)
            {
                secondary += "    last " + format_age(entity_age_s(playback, *sample));
            }
            else if (degraded)
            {
                secondary += "    degraded";
            }
        }
        draw->AddText(ImVec2(row_min.x + 26.0F, row_min.y + 31.0F),
                      degraded || stale ? IM_COL32(226, 181, 125, 242)
                                        : IM_COL32(164, 174, 181, 235),
                      secondary.c_str());
        if (sample && sample->heading_deg)
        {
            const std::string heading = format_value("%.0f deg", *sample->heading_deg);
            draw->AddText(ImVec2(row_min.x + row_width - 58.0F, row_min.y + 31.0F),
                          IM_COL32(151, 159, 166, 220),
                          heading.c_str());
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0F, 2.0F));
    }
    if (visible_count == 0U)
    {
        muted_text("No entities match the filter.");
    }
}

void draw_telemetry_panel(const Options &options,
                          TelemetryPlaybackState &playback,
                          UiState &ui,
                          const PlanVisualizationState &plan_state,
                          bool diagnostics_available)
{
    if (!playback.loaded)
    {
        muted_text("No telemetry source is active.");
        return;
    }

    if (playback.live)
    {
        ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "Live UDP");
        ImGui::TextColored(text_muted, "%s", playback.live_endpoint.c_str());
        draw_status_dot(telemetry_state_color(playback));
        ImGui::SameLine();
        ImGui::TextUnformatted(playback.receiver_stats.connected
                                   ? (playback.receiver_stats.stale ? "stale" : "connected")
                                   : "waiting");
        ImGui::Text("datagrams %llu  age %.3f s",
                    static_cast<unsigned long long>(playback.receiver_stats.datagrams),
                    playback.receiver_stats.last_packet_age_s);
    }
    else
    {
        ImGui::Text("format %s",
                    animus::telemetry_core::to_string(playback.timeline.source_format));
        ImGui::TextWrapped("%s", options.telemetry.string().c_str());
        draw_timeline_controls(playback, ui);
        if (ui.telemetry_entity_selected)
        {
            draw_review_jump_buttons(playback.review, ui);
            draw_review_filters(ui);
            draw_review_charts(playback.review);
        }
        if (ui.workspace_mode == WorkspaceMode::Analyze)
        {
            draw_ghost_replay_foundation(plan_state, playback, options, ui);
        }
    }
    ImGui::Separator();
    draw_entity_list(playback, ui);
    ImGui::Separator();
    ImGui::TextColored(text_muted,
                       "entities %zu  samples %zu  events %zu",
                       playback.timeline.entities.size(),
                       playback.timeline.samples.size(),
                       playback.timeline.events.size());
    ImGui::Text("terrain height %s",
                playback.terrain_height_unavailable ? "unavailable for some samples" : "available");
    if (playback.unknown_datum_relative_fallback)
    {
        ImGui::TextWrapped("warning: relative altitude used with unknown altitude datum");
    }
    if (playback.geoid_correction_unavailable)
    {
        ImGui::TextWrapped("warning: ellipsoid altitude datum needs a configured geoid grid");
    }

    if (!diagnostics_available)
    {
        return;
    }

    ImGui::Checkbox("Diagnostics", &ui.telemetry_diagnostics_visible);
    if (!ui.telemetry_diagnostics_visible)
    {
        return;
    }

    ImGui::Checkbox("Info events", &ui.telemetry_event_filters.show_info);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &ui.telemetry_event_filters.show_warnings);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &ui.telemetry_event_filters.show_errors);

    if (!playback.timeline.events.empty())
    {
        ImGui::Separator();
        if (ImGui::BeginTable("events", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("time");
            ImGui::TableSetupColumn("entity");
            ImGui::TableSetupColumn("msg");
            ImGui::TableSetupColumn("event");
            ImGui::TableHeadersRow();
            for (const auto &event : playback.timeline.events)
            {
                if (!telemetry_event_visible(event, ui.telemetry_event_filters))
                {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.3f", event.time_s);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u:%u",
                            static_cast<unsigned>(event.entity_id.system_id),
                            static_cast<unsigned>(event.entity_id.component_id));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", event.message_id);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(event.message.c_str());
            }
            ImGui::EndTable();
        }
    }
}

const char *
mavlink_observation_label(const animus::telemetry_core::MavlinkFieldObservationStatus status)
{
    switch (status)
    {
    case animus::telemetry_core::MavlinkFieldObservationStatus::Unsupported:
        return "unsupported";
    case animus::telemetry_core::MavlinkFieldObservationStatus::SupportedNotObserved:
        return "supported, not observed";
    case animus::telemetry_core::MavlinkFieldObservationStatus::ObservedNumeric:
        return "observed numeric";
    case animus::telemetry_core::MavlinkFieldObservationStatus::ObservedNonNumeric:
        return "observed non-numeric";
    }
    return "supported, not observed";
}

bool signal_matches_filter(const SignalInfo &signal, const char *filter)
{
    const std::string needle = lower_ascii(filter == nullptr ? std::string() : std::string(filter));
    if (needle.empty())
    {
        return true;
    }
    const std::string haystack = lower_ascii(signal.display_name + " " + signal.ref.field_path +
                                             " " + signal.ref.mavlink_message + " " +
                                             signal.ref.mavlink_field + " " + signal.unit);
    return haystack.find(needle) != std::string::npos;
}

std::string signal_latest_value(const SignalCatalog &catalog,
                                const SignalInfo &signal,
                                const TelemetryPlaybackState &playback,
                                const RuntimeSignalInputs &runtime)
{
    const double now_s = playback.live ? playback.timeline.end_time_s : playback.clock.time_s();
    SignalSample sample{.time_s = now_s, .status = SignalSampleStatus::Unavailable};
    if (signal.ref.source == SignalSource::Sample && playback.loaded &&
        !playback.timeline.samples.empty())
    {
        const std::optional<animus::telemetry_core::TelemetrySample> current =
            playback.timeline.sample_at(playback.selected_entity, now_s);
        if (current)
        {
            sample = catalog.extract_sample(signal.ref, *current, signal.default_transform);
        }
    }
    else if (signal.ref.source == SignalSource::Derived ||
             signal.ref.source == SignalSource::Runtime)
    {
        sample = catalog.extract_runtime(signal.ref, runtime, now_s, signal.default_transform);
    }
    else if (signal.ref.source == SignalSource::Mavlink)
    {
        SignalRef ref = signal.ref;
        ref.entity_id = playback.selected_entity;
        sample =
            catalog.extract_mavlink(ref, playback.mavlink_values, now_s, signal.default_transform);
    }

    if (sample.status != SignalSampleStatus::Valid)
    {
        return SignalCatalog::status_name(sample.status);
    }
    return signal.unit.empty() ? format_value("%.3f", sample.value)
                               : format_value("%.3f", sample.value) + " " + signal.unit;
}

std::string signal_availability(const SignalInfo &signal, const TelemetryPlaybackState &playback)
{
    if (signal.ref.source != SignalSource::Mavlink)
    {
        if (playback.live && !signal.live_available)
        {
            return "offline only";
        }
        if (!playback.live && !signal.offline_available)
        {
            return "live only";
        }
        return "available";
    }

    const std::uint32_t message_id = mavlink_message_id(signal.ref.mavlink_message);
    const auto *definition =
        animus::telemetry_core::mavlink_field_definition(message_id, signal.ref.mavlink_field);
    if (definition == nullptr)
    {
        return "unsupported";
    }
    if (!signal.numeric)
    {
        const MavlinkValueStats stats = playback.mavlink_values.stats(playback.selected_entity,
                                                                      message_id,
                                                                      signal.ref.mavlink_field,
                                                                      playback.timeline.end_time_s);
        return mavlink_observation_label(stats.status);
    }
    const MavlinkValueStats stats = playback.mavlink_values.stats(playback.selected_entity,
                                                                  message_id,
                                                                  signal.ref.mavlink_field,
                                                                  playback.timeline.end_time_s);
    return mavlink_observation_label(stats.status);
}

void draw_signal_catalog_table(const char *label,
                               const SignalSource source,
                               const SignalCatalog &catalog,
                               const TelemetryPlaybackState &playback,
                               const RuntimeSignalInputs &runtime,
                               UiState &ui)
{
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (!ImGui::BeginTable(label,
                           6,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                           ImVec2(0.0F, 168.0F)))
    {
        return;
    }
    ImGui::TableSetupColumn("path");
    ImGui::TableSetupColumn("name");
    ImGui::TableSetupColumn("unit");
    ImGui::TableSetupColumn("transform");
    ImGui::TableSetupColumn("status");
    ImGui::TableSetupColumn("latest");
    ImGui::TableHeadersRow();
    for (const SignalInfo &signal : catalog.signals())
    {
        if (signal.ref.source != source || !signal_matches_filter(signal, ui.signal_filter.data()))
        {
            continue;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(signal.ref.field_path.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(signal.display_name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(signal.unit.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(SignalCatalog::transform_name(signal.default_transform));
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(signal_availability(signal, playback).c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::TextUnformatted(signal_latest_value(catalog, signal, playback, runtime).c_str());
    }
    ImGui::EndTable();
}

std::string mavlink_numeric_value_label(const std::optional<double> value)
{
    return value ? format_value("%.6g", *value) : std::string("n/a");
}

void draw_mavlink_inspector(Options &options, TelemetryPlaybackState &playback, UiState &ui)
{
    if (!ui.mavlink_inspector_visible)
    {
        return;
    }

    const double now_s = playback.live ? playback.timeline.end_time_s : playback.clock.time_s();
    const std::vector<MavlinkMessageStats> messages =
        playback.mavlink_values.observed_messages(playback.selected_entity, now_s);
    if (messages.empty())
    {
        ImGui::TextColored(text_muted, "No supported MAVLink messages observed for this entity.");
        return;
    }
    const auto selected_message =
        std::find_if(messages.begin(),
                     messages.end(),
                     [&ui](const MavlinkMessageStats &message)
                     { return message.message_id == ui.selected_mavlink_inspector_message_id; });
    if (selected_message == messages.end())
    {
        ui.selected_mavlink_inspector_message_id = messages.front().message_id;
    }

    if (ImGui::BeginTable("mavlink_inspector_messages",
                          6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0F, 126.0F)))
    {
        ImGui::TableSetupColumn("message");
        ImGui::TableSetupColumn("Hz");
        ImGui::TableSetupColumn("age");
        ImGui::TableSetupColumn("count");
        ImGui::TableSetupColumn("fields");
        ImGui::TableSetupColumn("status");
        ImGui::TableHeadersRow();
        for (const MavlinkMessageStats &message : messages)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(message.message_name.c_str(),
                                  ui.selected_mavlink_inspector_message_id == message.message_id,
                                  ImGuiSelectableFlags_SpanAllColumns))
            {
                ui.selected_mavlink_inspector_message_id = message.message_id;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f", message.approximate_hz);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", message.last_age_s);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(message.count));
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%zu", message.observed_numeric_field_count);
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(mavlink_observation_label(message.status));
        }
        ImGui::EndTable();
    }

    const std::vector<MavlinkInspectorFieldStats> fields = playback.mavlink_values.observed_fields(
        playback.selected_entity, ui.selected_mavlink_inspector_message_id, now_s);
    if (ImGui::BeginTable("mavlink_inspector_fields",
                          5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0F, 156.0F)))
    {
        ImGui::TableSetupColumn("field");
        ImGui::TableSetupColumn("current");
        ImGui::TableSetupColumn("min");
        ImGui::TableSetupColumn("max");
        ImGui::TableSetupColumn("changed");
        ImGui::TableHeadersRow();
        const SignalCatalog catalog;
        for (const MavlinkInspectorFieldStats &field : fields)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string row_id = field.message_name + "." + field.field_name;
            ImGui::Selectable(row_id.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::BeginPopupContextItem(row_id.c_str()))
            {
                if (ImGui::MenuItem("Plot this", nullptr, false, field.numeric))
                {
                    plot_mavlink_inspector_field(options.plots,
                                                 ui.plot_ui,
                                                 catalog,
                                                 field.message_name,
                                                 field.field_name,
                                                 MavlinkInspectorPlotTarget::Dedicated);
                }
                if (ImGui::MenuItem("Add to existing plot", nullptr, false, field.numeric))
                {
                    plot_mavlink_inspector_field(options.plots,
                                                 ui.plot_ui,
                                                 catalog,
                                                 field.message_name,
                                                 field.field_name,
                                                 MavlinkInspectorPlotTarget::Existing);
                }
                if (ImGui::MenuItem("Copy field path"))
                {
                    ImGui::SetClipboardText(
                        mavlink_inspector_field_path(field.message_name, field.field_name).c_str());
                }
                if (ImGui::MenuItem(
                        "Copy current value", nullptr, false, field.latest_value.has_value()))
                {
                    ImGui::SetClipboardText(
                        mavlink_numeric_value_label(field.latest_value).c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(field.numeric
                                       ? mavlink_numeric_value_label(field.latest_value).c_str()
                                       : "non-numeric");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(mavlink_numeric_value_label(field.min_value).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(mavlink_numeric_value_label(field.max_value).c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f s", field.last_changed_age_s);
        }
        ImGui::EndTable();
    }
}

void draw_signals_panel(Options &options,
                        TelemetryPlaybackState &playback,
                        const RuntimeSignalInputs &runtime,
                        UiState &ui)
{
    const SignalCatalog catalog;
    ImGui::InputText("Search", ui.signal_filter.data(), ui.signal_filter.size());
    ImGui::TextColored(text_muted,
                       "signals %zu  selected entity %s",
                       catalog.signals().size(),
                       entity_label(playback.selected_entity).c_str());
    ImGui::Separator();
    draw_signal_catalog_table("Sample", SignalSource::Sample, catalog, playback, runtime, ui);
    draw_signal_catalog_table("Derived", SignalSource::Derived, catalog, playback, runtime, ui);
    draw_signal_catalog_table("Runtime", SignalSource::Runtime, catalog, playback, runtime, ui);
    draw_signal_catalog_table("MAVLink", SignalSource::Mavlink, catalog, playback, runtime, ui);
    ImGui::Separator();
    ImGui::Checkbox("MAVLink Inspector", &ui.mavlink_inspector_visible);
    draw_mavlink_inspector(options, playback, ui);
}

void draw_capture_panel(ScreenshotToolState &screenshot_tool,
                        Mp4RecorderState &mp4_recorder,
                        bool settings_available)
{
    ImGui::InputText("PNG path", screenshot_tool.png_path.data(), screenshot_tool.png_path.size());
    if (ImGui::Button("Save PNG"))
    {
        screenshot_tool.pending_png = true;
        screenshot_tool.status = "saving after this frame";
    }
    ImGui::SameLine();
    if (ImGui::Button("Use default"))
    {
        constexpr const char *default_path = "artifacts/animus/screenshots/manual_screenshot.png";
        std::snprintf(
            screenshot_tool.png_path.data(), screenshot_tool.png_path.size(), "%s", default_path);
    }
    ImGui::TextWrapped("%s", screenshot_tool.status.c_str());
    if (!settings_available)
    {
        return;
    }
    ImGui::Separator();
    ImGui::InputText("MP4 path", mp4_recorder.mp4_path.data(), mp4_recorder.mp4_path.size());
    ImGui::InputInt("FPS", &mp4_recorder.fps);
    mp4_recorder.fps = std::clamp(mp4_recorder.fps, 1, 240);
    bool record = mp4_recorder.recording;
    if (ImGui::Checkbox("Record MP4", &record))
    {
        try
        {
            if (record)
            {
                start_mp4_recording(mp4_recorder);
            }
            else
            {
                mp4_recorder.pending_stop = true;
                mp4_recorder.status = "finishing recording";
            }
        }
        catch (const std::exception &error)
        {
            mp4_recorder.recording = false;
            mp4_recorder.pending_stop = false;
            mp4_recorder.status = std::string("recording failed: ") + error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use video default"))
    {
        constexpr const char *default_path = "artifacts/animus/videos/manual_recording.mp4";
        std::snprintf(
            mp4_recorder.mp4_path.data(), mp4_recorder.mp4_path.size(), "%s", default_path);
    }
    ImGui::Text("recorded frames %d", mp4_recorder.frame_count);
    ImGui::TextWrapped("%s", mp4_recorder.status.c_str());
}

ReportExportInput build_report_export_input(const Options &options,
                                            const TelemetryPlaybackState &playback,
                                            const PlanVisualizationState &plan_state,
                                            const UiState &ui)
{
    ReportExportInput input;
    input.run_source = playback.live ? playback.live_endpoint
                                     : (options.telemetry.empty() ? std::string("none")
                                                                  : options.telemetry.string());
    input.config_profile = options.config_path.empty() ? "default" : options.config_path.string();
    if (playback.loaded && ui.telemetry_entity_selected)
    {
        input.run = summarize_telemetry_run(playback.timeline,
                                            playback.selected_entity,
                                            plan_state.data ? &*plan_state.data : nullptr,
                                            options.status_thresholds.telemetry_gap_warning_s);
    }
    input.review = playback.review;
    input.events = playback.review.markers;
    input.selected_vehicle_test = ui.selected_vehicle_test;
    if (playback.ghost_baseline && playback.loaded && ui.telemetry_entity_selected)
    {
        input.ghost = compare_ghost_replay(playback.timeline,
                                           *playback.ghost_baseline,
                                           playback.selected_entity,
                                           plan_state.data ? &*plan_state.data : nullptr,
                                           options.status_thresholds.telemetry_gap_warning_s);
    }
    return input;
}

void draw_export_panel(const Options &options,
                       const TelemetryPlaybackState &playback,
                       const PlanVisualizationState &plan_state,
                       UiState &ui)
{
    ReportExportUiState &export_ui = ui.report_export;
    if (export_ui.output_dir[0] == '\0')
    {
        std::snprintf(export_ui.output_dir.data(),
                      export_ui.output_dir.size(),
                      "%s",
                      ui.report_export_default_dir.string().c_str());
    }
    const ReportExportInput input = build_report_export_input(options, playback, plan_state, ui);
    ImGui::SeparatorText("Report bundle");
    ImGui::InputText("Output dir", export_ui.output_dir.data(), export_ui.output_dir.size());
    ImGui::Checkbox("Summary YAML", &export_ui.include_summary_yaml);
    ImGui::SameLine();
    ImGui::Checkbox("Summary Markdown", &export_ui.include_summary_markdown);
    ImGui::Checkbox("Events CSV", &export_ui.include_events_csv);
    ImGui::SameLine();
    ImGui::Checkbox("Plot CSVs", &export_ui.include_plot_csvs);
    ImGui::Checkbox("Screenshots directory", &export_ui.include_screenshots_directory);

    ImGui::SeparatorText("Preview");
    for (const std::string &line : report_export_preview_lines(input, export_ui))
    {
        ImGui::TextWrapped("%s", line.c_str());
    }

    if (!playback.loaded)
    {
        muted_text("No telemetry loaded; export will contain empty run summaries.");
    }
    if (ImGui::Button("Export Bundle"))
    {
        export_ui.diagnostics.clear();
        export_ui.last_result =
            export_report_v1_from_ui(input, export_ui, ui.report_export_default_dir);
        export_ui.diagnostics = export_ui.last_result.diagnostics;
    }
    if (!export_ui.last_result.directory.empty())
    {
        ImGui::Text("last export %s", export_ui.last_result.ok ? "complete" : "failed");
        ImGui::TextWrapped("%s", export_ui.last_result.directory.string().c_str());
        for (const std::filesystem::path &path : export_ui.last_result.files)
        {
            ImGui::TextWrapped("%s", path.string().c_str());
        }
    }
    for (const std::string &diagnostic : export_ui.diagnostics)
    {
        ImGui::TextColored(stale_amber, "%s", diagnostic.c_str());
    }
}

void draw_developer_panel(
    const Options &options,
    const animus::render_core::GlInfo &gl_info,
    const animus::render_core::RenderStats &stats,
    const animus::terrain_core::TerrainStreamSnapshot &snapshot,
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
    std::size_t upload_bytes_used,
    int texture_uploads_used,
    int mesh_uploads_used,
    std::size_t resident_gpu_bytes,
    const TelemetryPlaybackState &playback,
    const VehicleRuntimeStatus &vehicle_status,
    UiState &ui)
{
    ImGui::Checkbox("Show diagnostics", &ui.developer_diagnostics_visible);
    if (!ui.developer_diagnostics_visible)
    {
        ImGui::TextUnformatted("Developer diagnostics are hidden.");
        return;
    }

    if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("visible %zu resident %zu queued %zu loading %zu ready-cpu %zu failed %zu",
                    visible_tiles.size(),
                    snapshot.resident_gpu_tiles,
                    snapshot.queued_jobs,
                    snapshot.loading_jobs,
                    snapshot.ready_cpu_tiles,
                    snapshot.failed_tiles);
        ImGui::Text("uploads textures %d meshes %d bytes %.2f MiB resident %.2f MiB",
                    texture_uploads_used,
                    mesh_uploads_used,
                    static_cast<double>(upload_bytes_used) / (1024.0 * 1024.0),
                    static_cast<double>(resident_gpu_bytes) / (1024.0 * 1024.0));
        ImGui::Text("frames %d last %.3f ms total %.3f s",
                    stats.frame_count(),
                    stats.last_frame_seconds() * 1000.0,
                    stats.total_seconds());
        ImGui::Text("GL vendor %s", gl_info.vendor.c_str());
        ImGui::Text("GL renderer %s", gl_info.renderer.c_str());
        ImGui::Text("GL version %s", gl_info.version.c_str());
    }

    if (ImGui::CollapsingHeader("Cache", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto &cache = snapshot.cache_stats;
        ImGui::Text("L0 gpu tiles %zu bytes %.2f MiB",
                    snapshot.resident_gpu_tiles,
                    static_cast<double>(snapshot.resident_gpu_bytes) / (1024.0 * 1024.0));
        ImGui::Text("L1 prepared %zu %.2f/%.2f MiB hits %llu misses %llu evict %llu",
                    cache.l1_prepared.entries,
                    static_cast<double>(cache.l1_prepared.bytes) / (1024.0 * 1024.0),
                    static_cast<double>(cache.l1_prepared.byte_limit) / (1024.0 * 1024.0),
                    static_cast<unsigned long long>(cache.l1_prepared.counters.hits),
                    static_cast<unsigned long long>(cache.l1_prepared.counters.misses),
                    static_cast<unsigned long long>(cache.l1_prepared.counters.evictions));
        ImGui::Text("L2 rasters %zu %.2f/%.2f MiB hits %llu misses %llu evict %llu",
                    cache.l2_raster.entries,
                    static_cast<double>(cache.l2_raster.bytes) / (1024.0 * 1024.0),
                    static_cast<double>(cache.l2_raster.byte_limit) / (1024.0 * 1024.0),
                    static_cast<unsigned long long>(cache.l2_raster.counters.hits),
                    static_cast<unsigned long long>(cache.l2_raster.counters.misses),
                    static_cast<unsigned long long>(cache.l2_raster.counters.evictions));
        ImGui::Text(
            "L3 hits %llu misses %llu stores %llu synth %llu persisted %llu geotiff-fail %llu",
            static_cast<unsigned long long>(cache.l3_disk.counters.hits),
            static_cast<unsigned long long>(cache.l3_disk.counters.misses),
            static_cast<unsigned long long>(cache.l3_disk.counters.stores),
            static_cast<unsigned long long>(cache.synthesized_tiles),
            static_cast<unsigned long long>(cache.persisted_tiles),
            static_cast<unsigned long long>(cache.geotiff_extraction_failures));
    }

    if (ImGui::CollapsingHeader("Vehicle Models", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("packages %zu definitions %zu",
                    vehicle_status.registry_package_count,
                    vehicle_status.definitions.size());
        for (const auto &definition : vehicle_status.definitions)
        {
            ImGui::BulletText("%s / %s", definition.name.c_str(), definition.type.c_str());
            ImGui::SameLine();
            ImGui::TextColored(definition.model_loaded ? live_green : stale_amber,
                               "%s",
                               definition.model_status.c_str());
        }
        for (const std::string &diagnostic : vehicle_status.diagnostics)
        {
            ImGui::TextColored(stale_amber, "%s", diagnostic.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Telemetry Parser"))
    {
        const auto &diag = playback.timeline.diagnostics;
        ImGui::Text("frames %llu unsupported %llu crc %llu truncated %llu",
                    static_cast<unsigned long long>(diag.frames_decoded),
                    static_cast<unsigned long long>(diag.unsupported_messages),
                    static_cast<unsigned long long>(diag.crc_failures),
                    static_cast<unsigned long long>(diag.truncated_frames));
        ImGui::Text("signed-v2 %llu bad-version %llu malformed %llu",
                    static_cast<unsigned long long>(diag.signed_v2_frames),
                    static_cast<unsigned long long>(diag.unsupported_versions),
                    static_cast<unsigned long long>(diag.malformed_frames));
        ImGui::Text("schema %llu channels %llu layouts %llu decoded-fail %llu",
                    static_cast<unsigned long long>(diag.schema_mismatches),
                    static_cast<unsigned long long>(diag.unsupported_channels),
                    static_cast<unsigned long long>(diag.unsupported_layouts),
                    static_cast<unsigned long long>(diag.decode_failures));
        ImGui::Text("skipped %llu non-monotonic %llu missing-required %llu",
                    static_cast<unsigned long long>(diag.skipped_records),
                    static_cast<unsigned long long>(diag.non_monotonic_timestamps),
                    static_cast<unsigned long long>(diag.missing_required_fields));
        if (playback.live)
        {
            ImGui::Text("queue %zu high %zu drained %zu before %zu",
                        playback.receiver_stats.queued_datagrams,
                        playback.receiver_stats.queue_high_water,
                        playback.receiver_stats.last_drain_datagrams,
                        playback.receiver_stats.last_drain_queue_before);
            ImGui::Text("dropped datagrams %llu samples %llu",
                        static_cast<unsigned long long>(playback.receiver_stats.dropped_datagrams),
                        static_cast<unsigned long long>(playback.live_stats.dropped_samples));
            ImGui::Text("live ms ingest %.3f prune/finalize %.3f copy %.3f overlay %.3f",
                        playback.live_ingest_ms,
                        playback.live_prune_finalize_ms,
                        playback.live_snapshot_copy_ms,
                        playback.live_overlay_draw_ms);
        }
    }

    if (ImGui::CollapsingHeader("Vehicles", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("packages %zu", vehicle_status.registry_package_count);
        ImGui::Text("default %s", vehicle_status.default_vehicle_id.c_str());
        ImGui::Text("type %s", vehicle_status.default_vehicle_type.c_str());
        ImGui::Text("model %s", vehicle_status.model_status.c_str());
        if (!vehicle_status.diagnostics.empty())
        {
            ImGui::SeparatorText("Asset diagnostics");
            for (const auto &diagnostic : vehicle_status.diagnostics)
            {
                ImGui::TextWrapped("%s", diagnostic.c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Tiles"))
    {
        if (ImGui::BeginTable("tiles", 10, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("coord");
            ImGui::TableSetupColumn("state");
            ImGui::TableSetupColumn("tier");
            ImGui::TableSetupColumn("source");
            ImGui::TableSetupColumn("synth");
            ImGui::TableSetupColumn("priority");
            ImGui::TableSetupColumn("parent");
            ImGui::TableSetupColumn("age");
            ImGui::TableSetupColumn("height");
            ImGui::TableSetupColumn("error");
            ImGui::TableHeadersRow();
            for (const auto &tile : snapshot.tiles)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d/%d/%d", tile.coord.z, tile.coord.x, tile.coord.y);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(
                    std::string(animus::terrain_core::to_string(tile.state)).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    std::string(animus::terrain_core::to_string(tile.cache_tier)).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(
                    std::string(animus::terrain_core::to_string(tile.source_type)).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s/%d", tile.synthetic ? "yes" : "no", tile.synthesis_depth);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.2f", tile.priority);
                ImGui::TableSetColumnIndex(6);
                if (tile.parent)
                {
                    ImGui::Text("%d/%d/%d", tile.parent->z, tile.parent->x, tile.parent->y);
                }
                ImGui::TableSetColumnIndex(7);
                ImGui::Text("%llu",
                            static_cast<unsigned long long>(snapshot.frame - tile.state_frame));
                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%.1f..%.1f", tile.min_height_m, tile.max_height_m);
                ImGui::TableSetColumnIndex(9);
                ImGui::TextUnformatted(tile.error.c_str());
            }
            ImGui::EndTable();
        }
    }

    (void)options;
}

void metric_row(const char *label, const std::string &value)
{
    ImGui::TextColored(text_muted, "%s", label);
    ImGui::SameLine(118.0F);
    ImGui::TextWrapped("%s", value.c_str());
}

ImVec4 selected_vehicle_status_color(const SelectedVehicleCardStatus status)
{
    switch (status)
    {
    case SelectedVehicleCardStatus::Ok:
        return live_green;
    case SelectedVehicleCardStatus::Caution:
        return stale_amber;
    case SelectedVehicleCardStatus::Warning:
        return ImVec4(0.94F, 0.28F, 0.28F, 1.0F);
    case SelectedVehicleCardStatus::Unknown:
        return quiet_gray;
    }
    return quiet_gray;
}

void metric_grid(const char *id, const std::vector<SelectedVehicleCardMetric> &metrics)
{
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame))
    {
        for (const auto &metric : metrics)
        {
            ImGui::TableNextColumn();
            ImGui::TextColored(text_muted, "%s", metric.label.c_str());
            ImGui::TextUnformatted(metric.value.c_str());
        }
        ImGui::EndTable();
    }
}

std::string selected_metric_value(const std::vector<SelectedVehicleCardMetric> &metrics,
                                  const char *label)
{
    const auto found = std::find_if(metrics.begin(),
                                    metrics.end(),
                                    [label](const SelectedVehicleCardMetric &metric)
                                    { return metric.label == label; });
    return found == metrics.end() ? std::string("--") : found->value;
}

void draw_selected_vehicle_compact_metrics(const SelectedVehicleCardModel &card)
{
    metric_row("Status", card.telemetry_state);
    metric_row("Visual", card.detected_type + " / " + card.visual_status);
    metric_row("Age", card.telemetry_age);
    metric_row("Alt MSL", selected_metric_value(card.position_metrics, "Alt MSL"));
    metric_row("Alt rel", selected_metric_value(card.position_metrics, "Alt Rel"));
    metric_row("Clearance", selected_metric_value(card.position_metrics, "Clearance"));
    metric_row("Ground", selected_metric_value(card.motion_metrics, "Ground"));
    metric_row("Climb", selected_metric_value(card.motion_metrics, "Climb"));
    metric_row("Heading", selected_metric_value(card.motion_metrics, "Heading"));
    metric_row("Roll", selected_metric_value(card.motion_metrics, "Roll"));
    metric_row("Pitch", selected_metric_value(card.motion_metrics, "Pitch"));
    metric_row("Terrain", card.terrain_confidence);
    metric_row("Forward", card.forward_clearance_summary);
}

void draw_selected_entity_card(Options &options,
                               TelemetryPlaybackState &playback,
                               const VehicleRuntimeStatus &vehicle_status,
                               const AppConfigStatusThresholds &thresholds,
                               UiState &ui_state)
{
    const SelectedVehicleCardModel card =
        build_selected_vehicle_card_model(playback, ui_state, vehicle_status, thresholds);
    const ImVec4 state_color = selected_vehicle_status_color(card.status);
    ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "%s", card.entity_label.c_str());
    ImGui::SameLine();
    draw_status_dot(state_color);
    ImGui::TextColored(state_color, "%s", card.status_label.c_str());
    ImGui::TextColored(text_muted, "%s", card.visual_assignment.c_str());
    ImGui::TextColored(text_muted, "model %s", card.visual_status.c_str());
    if (card.visual_fallback != "--")
    {
        ImGui::TextColored(stale_amber, "%s", card.visual_fallback.c_str());
    }
    if (ImGui::Button(ui_state.selected_vehicle_inspector_mode ==
                              SelectedVehicleInspectorMode::Compact
                          ? "Expanded details"
                          : "Compact view"))
    {
        ui_state.selected_vehicle_inspector_mode =
            ui_state.selected_vehicle_inspector_mode == SelectedVehicleInspectorMode::Compact
                ? SelectedVehicleInspectorMode::Expanded
                : SelectedVehicleInspectorMode::Compact;
    }

    if (ui_state.selected_vehicle_inspector_mode == SelectedVehicleInspectorMode::Compact)
    {
        ImGui::SeparatorText("Telemetry");
        draw_selected_vehicle_compact_metrics(card);
        if (!card.warnings.empty())
        {
            ImGui::SeparatorText("Warnings");
            for (const std::string &warning : card.warnings)
            {
                ImGui::TextColored(stale_amber, "%s", warning.c_str());
            }
        }
        return;
    }

    ImGui::SeparatorText("Controls");
    ImGui::Checkbox("Follow selected", &ui_state.follow_selected_entity);
    if (ImGui::Button("Center"))
    {
        ui_state.request_center_selected_entity = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit selected"))
    {
        ui_state.request_fit_selected_entity = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &ui_state.telemetry_labels_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Tracks", &ui_state.telemetry_tracks_visible);

    ImGui::SeparatorText("Vehicle visual");
    metric_row("Detected", card.detected_type);
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        muted_text("Select telemetry to assign a vehicle visual.");
    }
    else
    {
        const std::string entity_key = vehicle_assignment_entity_key(playback.selected_entity);
        VehicleVisualAssignment assignment;
        if (const auto existing = options.vehicle_visuals.entities.find(entity_key);
            existing != options.vehicle_visuals.entities.end())
        {
            assignment = existing->second;
        }
        else
        {
            assignment.vehicle_id = vehicle_status.selected_vehicle_id.empty()
                                        ? vehicle_status.default_vehicle_id
                                        : vehicle_status.selected_vehicle_id;
            assignment.force_icon_only = vehicle_status.selected_force_icon_only;
            assignment.scale = vehicle_status.selected_scale;
            assignment.heading_source = vehicle_status.selected_heading_source;
            assignment.altitude_placement = vehicle_status.selected_altitude_placement;
        }
        if (assignment.vehicle_id.empty())
        {
            assignment.vehicle_id = vehicle_status.selected_vehicle_id.empty()
                                        ? vehicle_status.default_vehicle_id
                                        : vehicle_status.selected_vehicle_id;
        }
        if (assignment.heading_source.empty())
        {
            assignment.heading_source = "auto";
        }
        if (assignment.altitude_placement.empty())
        {
            assignment.altitude_placement = "terrain_resolved";
        }

        int selected_index = 0;
        for (std::size_t index = 0; index < vehicle_status.definitions.size(); ++index)
        {
            if (vehicle_status.definitions[index].id == assignment.vehicle_id)
            {
                selected_index = static_cast<int>(index);
                break;
            }
        }
        const char *preview =
            vehicle_status.definitions.empty()
                ? "No models"
                : vehicle_status.definitions[static_cast<std::size_t>(selected_index)].name.c_str();
        if (ImGui::BeginCombo("Assigned model", preview))
        {
            for (std::size_t index = 0; index < vehicle_status.definitions.size(); ++index)
            {
                const bool selected = static_cast<int>(index) == selected_index;
                const auto &definition = vehicle_status.definitions[index];
                const std::string label = definition.name + "##" + definition.id;
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    assignment.vehicle_id = definition.id;
                    options.vehicle_visuals.entities[entity_key] = assignment;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("Force icon-only", &assignment.force_icon_only))
        {
            options.vehicle_visuals.entities[entity_key] = assignment;
        }
        if (ImGui::SliderFloat("Scale", &assignment.scale, 0.1F, 10.0F, "%.2f"))
        {
            options.vehicle_visuals.entities[entity_key] = assignment;
        }
        const char *heading_items[] = {"auto", "none"};
        int heading_index = assignment.heading_source == "none" ? 1 : 0;
        if (ImGui::Combo("Heading source", &heading_index, heading_items, 2))
        {
            assignment.heading_source = heading_items[heading_index];
            options.vehicle_visuals.entities[entity_key] = assignment;
        }
        const char *altitude_items[] = {"terrain_resolved"};
        int altitude_index = 0;
        if (ImGui::Combo("Altitude placement", &altitude_index, altitude_items, 1))
        {
            assignment.altitude_placement = altitude_items[altitude_index];
            options.vehicle_visuals.entities[entity_key] = assignment;
        }
        if (ImGui::Button("Use as type default"))
        {
            options.vehicle_visuals.defaults_by_type[vehicle_status.selected_detected_type] =
                assignment.vehicle_id;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear entity override"))
        {
            options.vehicle_visuals.entities.erase(entity_key);
        }
        metric_row("Icon", vehicle_status.selected_vehicle_type);
        metric_row("Heading", card.heading_source);
        metric_row("Altitude", card.altitude_placement);
    }

    ImGui::SeparatorText("Test");
    char test_name[96]{};
    char test_phase[64]{};
    char target_speed[64]{};
    char target_altitude[64]{};
    char target_heading[64]{};
    std::snprintf(
        test_name, sizeof(test_name), "%s", ui_state.selected_vehicle_test.test_name.c_str());
    std::snprintf(
        test_phase, sizeof(test_phase), "%s", ui_state.selected_vehicle_test.phase.c_str());
    std::snprintf(target_speed,
                  sizeof(target_speed),
                  "%s",
                  ui_state.selected_vehicle_test.target_speed.c_str());
    std::snprintf(target_altitude,
                  sizeof(target_altitude),
                  "%s",
                  ui_state.selected_vehicle_test.target_altitude.c_str());
    std::snprintf(target_heading,
                  sizeof(target_heading),
                  "%s",
                  ui_state.selected_vehicle_test.target_heading.c_str());
    if (ImGui::InputText("Test name", test_name, sizeof(test_name)))
    {
        ui_state.selected_vehicle_test.test_name = test_name;
    }
    if (ImGui::InputText("Phase", test_phase, sizeof(test_phase)))
    {
        ui_state.selected_vehicle_test.phase = test_phase;
    }
    if (ImGui::InputText("Target speed", target_speed, sizeof(target_speed)))
    {
        ui_state.selected_vehicle_test.target_speed = target_speed;
    }
    if (ImGui::InputText("Target altitude", target_altitude, sizeof(target_altitude)))
    {
        ui_state.selected_vehicle_test.target_altitude = target_altitude;
    }
    if (ImGui::InputText("Target heading", target_heading, sizeof(target_heading)))
    {
        ui_state.selected_vehicle_test.target_heading = target_heading;
    }
    metric_row("Test", card.test);
    metric_row("Phase", card.phase);
    metric_row("Target", card.target);

    ImGui::SeparatorText("Telemetry");
    metric_row("Mode", card.mode);
    metric_row("State", card.telemetry_state);
    metric_row("Age", card.telemetry_age);
    metric_row("Confidence", card.terrain_confidence);
    metric_row("Forward", card.forward_clearance_summary);

    ImGui::SeparatorText("Position");
    metric_grid("##selected_vehicle_position_metrics", card.position_metrics);
    ImGui::SeparatorText("Motion");
    metric_grid("##selected_vehicle_motion_metrics", card.motion_metrics);

    if (!card.warnings.empty())
    {
        ImGui::SeparatorText("Warnings");
        for (const std::string &warning : card.warnings)
        {
            ImGui::TextColored(stale_amber, "%s", warning.c_str());
        }
    }
}

void draw_inspector(Options &options,
                    TelemetryPlaybackState &playback,
                    const VehicleRuntimeStatus &vehicle_status,
                    const AppConfigStatusThresholds &thresholds,
                    UiState &ui_state,
                    AppWorkspaceLayout &layout)
{
    if (!ui_state.inspector_visible || ui_state.inspector_target == InspectorTarget::None ||
        ImGui::GetIO().DisplaySize.x < 980.0F)
    {
        return;
    }
    const AppWindowRect fallback{ImGui::GetIO().DisplaySize.x - inspector_width - chrome_margin,
                                 status_bar_height + chrome_margin,
                                 inspector_width,
                                 520.0F};
    const AppWindowRect rect =
        clamp_window_rect(layout.inspector, fallback, ImGui::GetIO().DisplaySize, {260.0F, 220.0F});
    const ImGuiCond rect_condition =
        ui_state.inspector_restore_pending ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), rect_condition);
    ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height), rect_condition);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panel_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, panel_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 12.0F));
    ImGui::Begin("Inspector", nullptr, flags);
    if (!playback.loaded)
    {
        ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "Telemetry");
        muted_text("No telemetry loaded.");
    }
    else if (ui_state.inspector_target == InspectorTarget::Entity)
    {
        draw_selected_entity_card(options, playback, vehicle_status, thresholds, ui_state);
    }
    else if (ui_state.inspector_target == InspectorTarget::TelemetrySource)
    {
        ImGui::Text("source %s", playback.live ? "Live UDP" : "Playback");
        ImGui::Text("samples %zu", playback.timeline.samples.size());
        ImGui::Text("events %zu", playback.timeline.events.size());
    }
    else
    {
        ImGui::TextUnformatted("Terrain view");
    }
    capture_current_window_rect(layout.inspector);
    ui_state.inspector_restore_pending = false;
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

bool marker_in_tape_row(const TimelineReviewMarkerCategory category, const int row)
{
    switch (row)
    {
    case 0:
        return category == TimelineReviewMarkerCategory::Gap;
    case 1:
        return category == TimelineReviewMarkerCategory::LowClearance ||
               category == TimelineReviewMarkerCategory::MinClearance ||
               category == TimelineReviewMarkerCategory::TerrainFallback;
    case 2:
        return category == TimelineReviewMarkerCategory::PlanDeviation ||
               category == TimelineReviewMarkerCategory::PlanAltitude ||
               category == TimelineReviewMarkerCategory::Geofence;
    case 3:
        return category == TimelineReviewMarkerCategory::Attitude ||
               category == TimelineReviewMarkerCategory::FrameTime ||
               category == TimelineReviewMarkerCategory::Degraded ||
               category == TimelineReviewMarkerCategory::ImportWarning ||
               category == TimelineReviewMarkerCategory::ImportError ||
               category == TimelineReviewMarkerCategory::MaxSpeed ||
               category == TimelineReviewMarkerCategory::LowLinkHz ||
               category == TimelineReviewMarkerCategory::SpeedExcursion ||
               category == TimelineReviewMarkerCategory::ClimbExcursion ||
               category == TimelineReviewMarkerCategory::ModelFallback;
    case 4:
        return category == TimelineReviewMarkerCategory::Bookmark ||
               category == TimelineReviewMarkerCategory::Capture;
    }
    return false;
}

void draw_timeline_filter_toggle(const char *label, bool &value)
{
    ImGui::Checkbox(label, &value);
}

void draw_review_filters(UiState &ui_state)
{
    ui_state.timeline_review_filter_preset =
        classify_timeline_review_filter(ui_state.timeline_review_filters);
    for (const TimelineFilterPreset preset : {TimelineFilterPreset::All,
                                              TimelineFilterPreset::Critical,
                                              TimelineFilterPreset::Warnings,
                                              TimelineFilterPreset::Bookmarks,
                                              TimelineFilterPreset::Custom})
    {
        const bool selected = ui_state.timeline_review_filter_preset == preset;
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.30F, 0.37F, 1.0F));
        }
        if (ImGui::SmallButton(timeline_filter_preset_label(preset)))
        {
            if (preset == TimelineFilterPreset::Custom)
            {
                ImGui::OpenPopup("Timeline filters");
            }
            else
            {
                ui_state.timeline_review_filters = timeline_review_filter_preset(preset);
                ui_state.timeline_review_filter_preset = preset;
            }
        }
        if (selected)
        {
            ImGui::PopStyleColor();
        }
        if (preset != TimelineFilterPreset::Custom)
        {
            ImGui::SameLine();
        }
    }
    if (ImGui::BeginPopup("Timeline filters"))
    {
        TimelineReviewFilterState &filters = ui_state.timeline_review_filters;
        draw_timeline_filter_toggle("Info", filters.show_info);
        draw_timeline_filter_toggle("Caution", filters.show_caution);
        draw_timeline_filter_toggle("Warning", filters.show_warning);
        ImGui::Separator();
        draw_timeline_filter_toggle("Gaps", filters.show_gap);
        draw_timeline_filter_toggle("Terrain / clearance", filters.show_clearance);
        draw_timeline_filter_toggle("Plan / geofence", filters.show_plan);
        draw_timeline_filter_toggle("Attitude / performance", filters.show_attitude);
        draw_timeline_filter_toggle("Frame time", filters.show_frame_time);
        draw_timeline_filter_toggle("Bookmarks / capture", filters.show_bookmark);
        draw_timeline_filter_toggle("Degraded / model", filters.show_degraded);
        draw_timeline_filter_toggle("Min/max", filters.show_min_max);
        draw_timeline_filter_toggle("Import", filters.show_import);
        ui_state.timeline_review_filter_preset = TimelineFilterPreset::Custom;
        ImGui::EndPopup();
    }
}

void draw_timeline_scrub_bar(TelemetryPlaybackState &playback, UiState &ui_state, const float width)
{
    const double start = playback.timeline.start_time_s;
    const double end = playback.timeline.end_time_s;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 tape_min = ImGui::GetCursorScreenPos();
    const ImVec2 tape_size(width, 64.0F);
    const ImVec2 tape_max(tape_min.x + tape_size.x, tape_min.y + tape_size.y);
    ImGui::InvisibleButton("timeline_scrub_bar", tape_size);
    draw->AddRectFilled(tape_min, tape_max, IM_COL32(12, 15, 18, 235), 5.0F);
    draw->AddRect(tape_min, tape_max, IM_COL32(65, 73, 80, 230), 5.0F);
    constexpr int row_count = 5;
    constexpr float ruler_height = 14.0F;
    const float row_height = (tape_size.y - ruler_height - 8.0F) / static_cast<float>(row_count);
    for (int tick = 0; tick <= 4; ++tick)
    {
        const float x = tape_min.x + tape_size.x * static_cast<float>(tick) / 4.0F;
        draw->AddLine(ImVec2(x, tape_min.y + 2.0F),
                      ImVec2(x, tape_max.y - 3.0F),
                      IM_COL32(55, 62, 68, 180),
                      1.0F);
        const double tick_time =
            timeline_fraction_to_time(start, end, static_cast<double>(tick) / 4.0);
        const std::string label = format_value("%.1f", tick_time);
        draw->AddText(
            ImVec2(x + 3.0F, tape_min.y + 1.0F), IM_COL32(122, 130, 137, 220), label.c_str());
    }
    const char *row_labels[row_count] = {"Telemetry gaps",
                                         "Terrain / clearance",
                                         "Plan / geofence",
                                         "Attitude / performance / data",
                                         "Bookmarks / capture"};
    for (int row = 0; row < row_count; ++row)
    {
        const float y = tape_min.y + ruler_height + static_cast<float>(row) * row_height;
        draw->AddLine(
            ImVec2(tape_min.x, y), ImVec2(tape_max.x, y), IM_COL32(43, 49, 54, 210), 1.0F);
        draw->AddText(
            ImVec2(tape_min.x + 5.0F, y + 1.0F), IM_COL32(120, 128, 135, 210), row_labels[row]);
    }
    for (std::size_t index = 0U; index < playback.review.markers.size(); ++index)
    {
        const auto &marker = playback.review.markers[index];
        if (!timeline_review_marker_visible(marker, ui_state.timeline_review_filters))
        {
            continue;
        }
        const float x =
            tape_min.x +
            static_cast<float>(time_to_timeline_fraction(start, end, marker.time_s)) * tape_size.x;
        const bool selected = ui_state.selected_review_marker_index &&
                              *ui_state.selected_review_marker_index == index;
        for (int row = 0; row < row_count; ++row)
        {
            if (!marker_in_tape_row(marker.category, row))
            {
                continue;
            }
            const float row_min_y =
                tape_min.y + ruler_height + static_cast<float>(row) * row_height;
            const float row_max_y = row_min_y + row_height;
            const ImU32 color = marker_color(marker.category);
            if (marker.end_time_s)
            {
                const float end_x =
                    tape_min.x +
                    static_cast<float>(time_to_timeline_fraction(start, end, *marker.end_time_s)) *
                        tape_size.x;
                draw->AddRectFilled(ImVec2(x, row_min_y + 3.0F),
                                    ImVec2(std::max(x + 2.0F, end_x), row_max_y - 2.0F),
                                    color,
                                    2.0F);
            }
            else
            {
                draw->AddLine(ImVec2(x, row_min_y + 3.0F),
                              ImVec2(x, row_max_y - 2.0F),
                              color,
                              selected ? 3.2F : 1.7F);
            }
            if (selected)
            {
                draw->AddCircle(ImVec2(x, (row_min_y + row_max_y) * 0.5F),
                                5.0F,
                                IM_COL32(245, 250, 255, 255),
                                16,
                                1.4F);
            }
        }
    }
    const float loaded_start_x = tape_min.x;
    const float loaded_end_x = tape_max.x;
    draw->AddRectFilled(ImVec2(loaded_start_x, tape_max.y - 7.0F),
                        ImVec2(loaded_end_x, tape_max.y - 3.0F),
                        playback.live ? IM_COL32(76, 179, 119, 210) : IM_COL32(91, 152, 205, 210),
                        2.0F);
    const float current_x =
        tape_min.x +
        static_cast<float>(time_to_timeline_fraction(start, end, playback.clock.time_s())) *
            tape_size.x;
    draw->AddTriangleFilled(ImVec2(current_x, tape_min.y - 2.0F),
                            ImVec2(current_x - 5.0F, tape_min.y + 7.0F),
                            ImVec2(current_x + 5.0F, tape_min.y + 7.0F),
                            IM_COL32(235, 244, 250, 255));
    draw->AddLine(ImVec2(current_x, tape_min.y + 4.0F),
                  ImVec2(current_x, tape_max.y),
                  IM_COL32(235, 244, 250, 255),
                  1.4F);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const float local_x = std::clamp(ImGui::GetIO().MousePos.x - tape_min.x, 0.0F, tape_size.x);
        ui_state.request_timeline_seek_time_s =
            timeline_fraction_to_time(start, end, static_cast<double>(local_x / tape_size.x));
        if (playback.live)
        {
            ui_state.timeline_follow_latest = false;
            playback.clock.set_paused(true);
        }
    }
}

void draw_timeline_transport_controls(TelemetryPlaybackState &playback, UiState &ui_state)
{
    const double start = playback.timeline.start_time_s;
    const double end = playback.timeline.end_time_s;
    constexpr double step_s = 1.0;
    const bool live_following_latest = playback.live && ui_state.timeline_follow_latest;
    if (live_following_latest)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(playback.clock.paused() ? "Play" : "Pause"))
    {
        playback.clock.set_paused(!playback.clock.paused());
        if (playback.live)
        {
            ui_state.timeline_follow_latest = false;
        }
    }
    if (live_following_latest)
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("|<"))
    {
        ui_state.request_timeline_seek_time_s = start;
        if (playback.live)
        {
            ui_state.timeline_follow_latest = false;
            playback.clock.set_paused(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("<"))
    {
        ui_state.request_timeline_seek_time_s =
            timeline_step_time(start, end, playback.clock.time_s(), -step_s);
        if (playback.live)
        {
            ui_state.timeline_follow_latest = false;
            playback.clock.set_paused(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(">"))
    {
        ui_state.request_timeline_seek_time_s =
            timeline_step_time(start, end, playback.clock.time_s(), step_s);
        if (playback.live)
        {
            ui_state.timeline_follow_latest = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(">|"))
    {
        ui_state.request_timeline_seek_time_s = end;
        if (playback.live)
        {
            ui_state.timeline_follow_latest = true;
        }
    }
    if (playback.live)
    {
        ImGui::SameLine();
        if (ui_state.timeline_follow_latest)
        {
            ImGui::BeginDisabled();
            ImGui::Button("Live");
            ImGui::EndDisabled();
        }
        else if (ImGui::Button("Go Live"))
        {
            ui_state.timeline_follow_latest = true;
            ui_state.request_timeline_seek_time_s = end;
        }
    }
    ImGui::SameLine();
    bool looping = playback.clock.looping();
    if (ImGui::Checkbox("Loop", &looping))
    {
        playback.clock.set_looping(looping);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    float rate = static_cast<float>(playback.clock.rate());
    if (ImGui::SliderFloat("Rate", &rate, 0.1F, 16.0F, "%.2fx"))
    {
        playback.clock.set_rate(rate);
    }
}

TimelineReviewSeverity bookmark_severity_from_index(const int index)
{
    switch (index)
    {
    case 1:
        return TimelineReviewSeverity::Caution;
    case 2:
        return TimelineReviewSeverity::Warning;
    default:
        return TimelineReviewSeverity::Info;
    }
}

TimelineReviewMarkerCategory bookmark_category_from_index(const int index)
{
    switch (index)
    {
    case 1:
        return TimelineReviewMarkerCategory::Gap;
    case 2:
        return TimelineReviewMarkerCategory::LowClearance;
    case 3:
        return TimelineReviewMarkerCategory::Attitude;
    case 4:
        return TimelineReviewMarkerCategory::FrameTime;
    default:
        return TimelineReviewMarkerCategory::Bookmark;
    }
}

void draw_bookmark_popup(TelemetryPlaybackState &playback, UiState &ui_state)
{
    if (ImGui::Button("Bookmark..."))
    {
        ImGui::OpenPopup("Bookmark note");
    }
    if (ImGui::BeginPopup("Bookmark note"))
    {
        ImGui::SetNextItemWidth(260.0F);
        ImGui::InputTextWithHint("Note",
                                 "Session note",
                                 ui_state.timeline_bookmark_note.data(),
                                 ui_state.timeline_bookmark_note.size());
        const char *severities[] = {"Info", "Caution", "Warning"};
        ImGui::Combo("Severity", &ui_state.timeline_bookmark_severity, severities, 3);
        const char *categories[] = {"Bookmark", "Gap", "Terrain", "Attitude", "Frame"};
        ImGui::Combo("Category", &ui_state.timeline_bookmark_category, categories, 5);
        if (ImGui::Button("Add"))
        {
            TimelineBookmark bookmark;
            bookmark.time_s = playback.clock.time_s();
            bookmark.note = ui_state.timeline_bookmark_note.data();
            bookmark.severity = bookmark_severity_from_index(ui_state.timeline_bookmark_severity);
            bookmark.category = bookmark_category_from_index(ui_state.timeline_bookmark_category);
            add_timeline_bookmark(ui_state.timeline_bookmarks, std::move(bookmark));
            ui_state.timeline_bookmark_note.fill('\0');
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void draw_bottom_timeline(TelemetryPlaybackState &playback,
                          UiState &ui_state,
                          AppWorkspaceLayout &layout)
{
    const bool plan_default_timeline =
        ui_state.workspace_mode == WorkspaceMode::Plan && playback.loaded;
    if (!playback.loaded || (!ui_state.timeline_visible && !plan_default_timeline))
    {
        return;
    }
    const float left = chrome_margin + nav_width + panel_gap;
    const float right_reserve =
        ImGui::GetIO().DisplaySize.x >= 980.0F ? inspector_width + 36.0F : 24.0F;
    const float available_width = ImGui::GetIO().DisplaySize.x - left - right_reserve;
    if (available_width < 260.0F)
    {
        return;
    }
    const AppWindowRect fallback{left,
                                 ImGui::GetIO().DisplaySize.y - ui_state.timeline_height_px - 6.0F,
                                 available_width,
                                 ui_state.timeline_height_px};
    const AppWindowRect rect =
        clamp_window_rect(layout.timeline, fallback, ImGui::GetIO().DisplaySize, {260.0F, 28.0F});
    const ImGuiCond rect_condition =
        ui_state.timeline_restore_pending ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), rect_condition);
    ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height), rect_condition);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055F, 0.064F, 0.072F, 0.88F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 10.0F));
    ImGui::Begin("Timeline", nullptr, flags);
    if (ui_state.timeline_height_px <= 40.0F)
    {
        ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F),
                           "Events %zu  markers %zu",
                           playback.timeline.events.size(),
                           playback.review.markers.size());
        ImGui::SameLine();
        if (playback.review.min_clearance_marker)
        {
            ImGui::TextColored(
                text_muted, "min clearance %.1f s", playback.review.min_clearance_marker->time_s);
        }
        capture_current_window_rect(layout.timeline);
        ui_state.timeline_restore_pending = false;
        ui_state.timeline_height_px = ImGui::GetWindowSize().y;
        layout.timeline_height_px = ui_state.timeline_height_px;
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        return;
    }
    if (playback.live)
    {
        ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F),
                           "%s %.3f s / %.3f s",
                           ui_state.timeline_follow_latest ? "Live" : "Review",
                           playback.clock.time_s(),
                           playback.timeline.end_time_s);
        ImGui::SameLine();
        ImGui::TextColored(text_muted,
                           "samples %llu",
                           static_cast<unsigned long long>(playback.live_stats.produced_samples));
        ImGui::SameLine();
        ImGui::TextColored(text_muted,
                           "selected %s",
                           ui_state.telemetry_entity_selected
                               ? entity_label(playback.selected_entity).c_str()
                               : "none");
        ImGui::SameLine();
        ImGui::TextColored(
            playback.receiver_stats.stale ? stale_amber : live_green,
            "%s",
            playback.receiver_stats.stale
                ? "stale"
                : (ui_state.timeline_follow_latest ? "tracking latest" : "retained history"));
        ImGui::SameLine();
        ImGui::TextColored(
            text_muted,
            "markers %zu/%zu",
            visible_review_marker_count(playback.review.markers, ui_state.timeline_review_filters),
            playback.review.markers.size());
        draw_timeline_scrub_bar(playback, ui_state, ImGui::GetContentRegionAvail().x);
        draw_review_filters(ui_state);
        draw_timeline_transport_controls(playback, ui_state);
    }
    else
    {
        ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F),
                           "%.3f s / %.3f s",
                           playback.clock.time_s(),
                           playback.timeline.end_time_s);
        ImGui::SameLine();
        ImGui::TextColored(text_muted,
                           "selected %s",
                           ui_state.telemetry_entity_selected
                               ? entity_label(playback.selected_entity).c_str()
                               : "none");
        ImGui::SameLine();
        ImGui::TextColored(
            text_muted,
            "markers %zu/%zu",
            visible_review_marker_count(playback.review.markers, ui_state.timeline_review_filters),
            playback.review.markers.size());
        draw_timeline_scrub_bar(playback, ui_state, ImGui::GetContentRegionAvail().x);
        draw_review_filters(ui_state);
        draw_timeline_transport_controls(playback, ui_state);
        const auto previous = previous_review_marker(
            playback.review.markers, playback.clock.time_s(), ui_state.timeline_review_filters);
        ImGui::SameLine();
        if (!previous)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Prev marker") && previous)
        {
            request_review_marker_jump(ui_state, playback.review.markers, *previous);
        }
        if (!previous)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const auto next = next_review_marker(
            playback.review.markers, playback.clock.time_s(), ui_state.timeline_review_filters);
        if (!next)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Next marker") && next)
        {
            request_review_marker_jump(ui_state, playback.review.markers, *next);
        }
        if (!next)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const auto critical = next_critical_review_marker(
            playback.review.markers, playback.clock.time_s(), ui_state.timeline_review_filters);
        if (!critical)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Next critical") && critical)
        {
            request_review_marker_jump(ui_state, playback.review.markers, *critical);
        }
        if (!critical)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Bookmark"))
        {
            add_timeline_bookmark(ui_state.timeline_bookmarks, playback.clock.time_s());
        }
        ImGui::SameLine();
        draw_bookmark_popup(playback, ui_state);
    }
    capture_current_window_rect(layout.timeline);
    ui_state.timeline_restore_pending = false;
    ui_state.timeline_height_px = ImGui::GetWindowSize().y;
    layout.timeline_height_px = ui_state.timeline_height_px;
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace

std::filesystem::path screenshot_path(const ScreenshotToolState &tool)
{
    const std::string path(tool.png_path.data());
    if (path.empty())
    {
        return "artifacts/animus/screenshots/manual_screenshot.png";
    }
    return path;
}

std::filesystem::path recorder_output_path(const Mp4RecorderState &recorder)
{
    const std::string path(recorder.mp4_path.data());
    if (path.empty())
    {
        return "artifacts/animus/videos/manual_recording.mp4";
    }
    return path;
}

std::filesystem::path recorder_sequence_dir(const std::filesystem::path &output_path)
{
    const std::filesystem::path parent =
        output_path.has_parent_path() ? output_path.parent_path() : std::filesystem::path(".");
    const std::string stem =
        output_path.stem().empty() ? "manual_recording" : output_path.stem().string();
    return parent / (stem + "_frames");
}

void start_mp4_recording(Mp4RecorderState &recorder)
{
    const std::filesystem::path output_path = recorder_output_path(recorder);
    recorder.sequence_dir = recorder_sequence_dir(output_path);
    if (std::filesystem::exists(recorder.sequence_dir))
    {
        std::filesystem::remove_all(recorder.sequence_dir);
    }
    std::filesystem::create_directories(recorder.sequence_dir);
    recorder.frame_count = 0;
    recorder.pending_stop = false;
    recorder.recording = true;
    recorder.status = "recording to " + output_path.string();
}

void finish_mp4_recording(Mp4RecorderState &recorder)
{
    const std::filesystem::path output_path = recorder_output_path(recorder);
    if (recorder.frame_count <= 0)
    {
        recorder.status = "recording stopped with no frames";
        recorder.pending_stop = false;
        recorder.recording = false;
        return;
    }
    recorder.status = "encoding " + output_path.string();
    animus::app::encode_mp4_from_png_sequence(recorder.sequence_dir, recorder.fps, output_path);
    std::filesystem::remove_all(recorder.sequence_dir);
    recorder.status =
        "saved " + output_path.string() + " (" + std::to_string(recorder.frame_count) + " frames)";
    recorder.pending_stop = false;
    recorder.recording = false;
}

bool telemetry_event_visible(const animus::telemetry_core::Event &event,
                             const TelemetryEventFilters &filters)
{
    switch (event.severity)
    {
    case animus::telemetry_core::EventSeverity::Info:
        return filters.show_info;
    case animus::telemetry_core::EventSeverity::Warning:
        return filters.show_warnings;
    case animus::telemetry_core::EventSeverity::Error:
        return filters.show_errors;
    }
    return true;
}

void draw_app_workspace(Options &options,
                        const std::filesystem::path &pack_root,
                        const animus::render_core::GlInfo &gl_info,
                        const animus::render_core::RenderStats &stats,
                        const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                        const Camera &camera,
                        Map2DCamera &map_camera,
                        int selected_zoom,
                        const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                        std::size_t upload_bytes_used,
                        int texture_uploads_used,
                        int mesh_uploads_used,
                        std::size_t resident_gpu_bytes,
                        TelemetryPlaybackState &playback,
                        PlanVisualizationState &plan_state,
                        const VehicleRuntimeStatus &vehicle_status,
                        ScreenshotToolState &screenshot_tool,
                        Mp4RecorderState &mp4_recorder,
                        UiState &ui_state,
                        bool &state_colors,
                        bool &highlight_fallback,
                        bool &overlay_enabled,
                        float &overlay_opacity)
{
    sanitize_active_mode(ui_state);
    RuntimeSignalInputs runtime_signals;
    if (!ui_state.workspace_layout_applied)
    {
        apply_workspace_layout(ui_state,
                               map_camera,
                               options,
                               plan_state,
                               state_colors,
                               highlight_fallback,
                               ui_state.workspace_mode);
        ui_state.workspace_layout_applied = true;
    }
    runtime_signals.terrain_elevation_m = playback.selected_entity_terrain.terrain_elevation_m;
    runtime_signals.terrain_clearance_m = playback.selected_entity_terrain.terrain_clearance_m;
    runtime_signals.telemetry_age_s =
        playback.live ? std::optional<double>(playback.receiver_stats.last_packet_age_s)
                      : std::nullopt;
    runtime_signals.telemetry_gap_s =
        playback.live ? runtime_signals.telemetry_age_s : std::nullopt;
    runtime_signals.packet_count = playback.receiver_stats.datagrams;
    runtime_signals.drop_count =
        playback.receiver_stats.dropped_datagrams + playback.live_stats.dropped_samples;
    runtime_signals.frame_time_ms = stats.last_frame_seconds() * 1000.0;
    runtime_signals.resident_tile_count = snapshot.resident_gpu_tiles;
    runtime_signals.upload_bytes_this_frame = upload_bytes_used;
    if (playback.live && playback.timeline.end_time_s > playback.timeline.start_time_s)
    {
        runtime_signals.link_hz = static_cast<double>(playback.live_stats.parsed_messages) /
                                  (playback.timeline.end_time_s - playback.timeline.start_time_s);
    }
    draw_top_status_bar(options,
                        stats,
                        snapshot,
                        runtime_signals,
                        playback,
                        plan_state,
                        vehicle_status,
                        screenshot_tool,
                        mp4_recorder,
                        ui_state,
                        resident_gpu_bytes);
    draw_nav(ui_state, options, map_camera, plan_state, state_colors, highlight_fallback);
    AppWorkspaceLayout &layout = workspace_layout(ui_state, ui_state.workspace_mode);

    const float panel_left = chrome_margin + nav_width + panel_gap;
    const float panel_top = status_bar_height + chrome_margin;
    const float inspector_reserve =
        ImGui::GetIO().DisplaySize.x >= 980.0F ? inspector_width + 36.0F : 24.0F;
    const float main_width =
        std::clamp(ImGui::GetIO().DisplaySize.x - panel_left - inspector_reserve, 320.0F, 470.0F);
    const AppWindowRect main_fallback{panel_left, panel_top, main_width, 430.0F};
    const AppWindowRect main_rect = clamp_window_rect(
        layout.main_panel, main_fallback, ImGui::GetIO().DisplaySize, {320.0F, 220.0F});
    const ImGuiCond main_rect_condition =
        ui_state.main_panel_restore_pending ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImVec2(main_rect.x, main_rect.y), main_rect_condition);
    ImGui::SetNextWindowSize(ImVec2(main_rect.width, main_rect.height), main_rect_condition);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panel_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, panel_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 12.0F));
    ImGui::Begin(mode_label(ui_state.active_mode), nullptr, flags);
    const bool advanced_workspace = ui_state.workspace_mode != WorkspaceMode::FlyTest;
    switch (ui_state.active_mode)
    {
    case UiNavigationMode::View:
        ui_state.inspector_target = InspectorTarget::Terrain;
        draw_view_panel(options,
                        pack_root,
                        camera,
                        map_camera,
                        selected_zoom,
                        playback,
                        plan_state,
                        ui_state,
                        state_colors,
                        highlight_fallback);
        break;
    case UiNavigationMode::Layers:
        ui_state.inspector_target = InspectorTarget::Layer;
        draw_layer_panel(options,
                         pack_root,
                         snapshot,
                         visible_tiles,
                         playback,
                         plan_state,
                         ui_state,
                         state_colors,
                         highlight_fallback,
                         overlay_enabled,
                         overlay_opacity);
        break;
    case UiNavigationMode::Telemetry:
        ui_state.inspector_target =
            playback.loaded ? InspectorTarget::Entity : InspectorTarget::TelemetrySource;
        draw_telemetry_panel(options, playback, ui_state, plan_state, advanced_workspace);
        break;
    case UiNavigationMode::Signals:
        ui_state.inspector_target = InspectorTarget::TelemetrySource;
        draw_signals_panel(options, playback, runtime_signals, ui_state);
        break;
    case UiNavigationMode::Capture:
        ui_state.inspector_target = InspectorTarget::None;
        if (ui_state.workspace_mode == WorkspaceMode::Export)
        {
            draw_export_panel(options, playback, plan_state, ui_state);
        }
        else
        {
            draw_capture_panel(screenshot_tool, mp4_recorder, advanced_workspace);
        }
        break;
    case UiNavigationMode::Settings:
        ui_state.inspector_target = InspectorTarget::None;
        draw_settings_panel(options, ui_state);
        break;
    case UiNavigationMode::Developer:
        ui_state.inspector_target = InspectorTarget::TelemetrySource;
        draw_developer_panel(options,
                             gl_info,
                             stats,
                             snapshot,
                             visible_tiles,
                             upload_bytes_used,
                             texture_uploads_used,
                             mesh_uploads_used,
                             resident_gpu_bytes,
                             playback,
                             vehicle_status,
                             ui_state);
        break;
    }
    capture_current_window_rect(layout.main_panel);
    ui_state.main_panel_restore_pending = false;
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    draw_inspector(options, playback, vehicle_status, options.status_thresholds, ui_state, layout);
    draw_plot_shelf(options,
                    playback,
                    runtime_signals,
                    ui_state.plot_ui,
                    layout.plot_shelf,
                    ui_state.plot_shelf_restore_pending);
    if (options.plots.visible)
    {
        ui_state.plot_shelf_restore_pending = false;
    }
    draw_bottom_timeline(playback, ui_state, layout);
    capture_workspace_layout(ui_state, map_camera, options);
    ui_state.workspace_layout_restore_pending =
        ui_state.main_panel_restore_pending || ui_state.inspector_restore_pending ||
        ui_state.timeline_restore_pending || ui_state.plot_shelf_restore_pending;
}

} // namespace animus::app
