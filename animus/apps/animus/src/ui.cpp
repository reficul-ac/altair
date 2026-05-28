#include "ui.hpp"

#include "capture.hpp"
#include "layer_offline.hpp"
#include "plot_ui.hpp"
#include "selected_vehicle_card.hpp"
#include "status_ribbon.hpp"

#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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

const char *workspace_label(const WorkspaceMode mode)
{
    switch (mode)
    {
    case WorkspaceMode::Operator:
        return "Operator";
    case WorkspaceMode::Advanced:
        return "Advanced";
    case WorkspaceMode::Developer:
        return "Developer";
    }
    return "Operator";
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
    if (ImGui::Button(mode_label(mode), ImVec2(-1.0F, 0.0F)))
    {
        ui_state.active_mode = mode;
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
    if (mode == UiNavigationMode::Layers)
    {
        return workspace_mode != WorkspaceMode::Operator;
    }
    return true;
}

void sanitize_active_mode(UiState &ui_state)
{
    if (!mode_visible_in_workspace(ui_state.active_mode, ui_state.workspace_mode))
    {
        ui_state.active_mode = UiNavigationMode::View;
    }
}

void workspace_button(UiState &ui_state, const WorkspaceMode mode)
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
        ui_state.workspace_mode = mode;
        sanitize_active_mode(ui_state);
        if (mode == WorkspaceMode::Developer)
        {
            ui_state.developer_diagnostics_visible = true;
        }
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

std::string configured_label(const bool configured)
{
    return configured ? "configured" : "not configured";
}

std::string file_health(const std::filesystem::path &path)
{
    if (path.empty())
    {
        return "not configured";
    }
    return std::filesystem::exists(path) ? "available" : "missing";
}

std::string layer_source_summary(const animus::terrain_core::TerrainStreamSnapshot &snapshot)
{
    std::string summary = "none";
    for (const auto &tile : snapshot.tiles)
    {
        if (tile.source_type == animus::terrain_core::TileSourceType::None)
        {
            continue;
        }
        const std::string value(animus::terrain_core::to_string(tile.source_type));
        if (summary == "none")
        {
            summary = value;
        }
        else if (summary.find(value) == std::string::npos)
        {
            summary += "," + value;
        }
    }
    return summary;
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
    ImGui::SeparatorText("Plan");
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
    if (playback.loaded && ui_state.telemetry_entity_selected)
    {
        const auto *track = playback.timeline.track_for(playback.selected_entity);
        if (track != nullptr)
        {
            const PlanTrackComparison comparison = compare_plan_to_track(plan, *track);
            ImGui::Text("selected track %s",
                        format_route_distance(comparison.selected_track_m).c_str());
            ImGui::Text("first/last nearest %s / %s",
                        format_distance_m(comparison.first_waypoint_nearest_track_m).c_str(),
                        format_distance_m(comparison.last_waypoint_nearest_track_m).c_str());
        }
    }
    for (const std::string &diagnostic : plan_state.diagnostics)
    {
        ImGui::TextWrapped("warning: %s", diagnostic.c_str());
    }
}

void apply_layer_preset(const char *preset,
                        const Options &options,
                        UiState &ui_state,
                        bool &state_colors,
                        bool &highlight_fallback,
                        bool &overlay_enabled,
                        float &overlay_opacity)
{
    const std::string name(preset);
    if (name == "Operator clean" || name == "Capture/export")
    {
        state_colors = false;
        highlight_fallback = false;
        overlay_enabled = options.overlay_enabled;
        overlay_opacity = options.overlay_opacity;
        ui_state.telemetry_tracks_visible = true;
        ui_state.telemetry_labels_visible = true;
        ui_state.bathymetry_enabled = options.use_bathymetry;
    }
    else if (name == "Terrain analysis")
    {
        state_colors = false;
        highlight_fallback = false;
        overlay_enabled = !options.overlay_geotiff.empty() || !options.overlays.empty();
        overlay_opacity = 0.85F;
        ui_state.telemetry_tracks_visible = true;
        ui_state.telemetry_labels_visible = false;
        ui_state.bathymetry_enabled = options.use_bathymetry && !options.bathymetry_geotiff.empty();
    }
    else if (name == "Telemetry review")
    {
        state_colors = false;
        highlight_fallback = false;
        overlay_enabled = options.overlay_enabled;
        overlay_opacity = options.overlay_opacity;
        ui_state.telemetry_tracks_visible = true;
        ui_state.telemetry_labels_visible = true;
        ui_state.bathymetry_enabled = options.use_bathymetry;
    }
    else if (name == "Debug tiles")
    {
        state_colors = true;
        highlight_fallback = true;
        ui_state.telemetry_tracks_visible = true;
        ui_state.telemetry_labels_visible = true;
    }
    else if (name == "Bathymetry")
    {
        state_colors = false;
        highlight_fallback = false;
        overlay_enabled = !options.overlay_geotiff.empty() || !options.overlays.empty();
        overlay_opacity = 0.75F;
        ui_state.telemetry_tracks_visible = true;
        ui_state.telemetry_labels_visible = false;
        ui_state.bathymetry_enabled = options.use_bathymetry && !options.bathymetry_geotiff.empty();
    }
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
    }
    return IM_COL32(180, 186, 192, 255);
}

void request_review_jump(UiState &ui, const double time_s)
{
    ui.request_review_jump_time_s = time_s;
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
    ui.request_review_jump_time_s = markers[index].time_s;
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

void draw_nav(UiState &ui_state)
{
    sanitize_active_mode(ui_state);
    ImGui::SetNextWindowPos(ImVec2(chrome_margin, status_bar_height + chrome_margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(nav_width, 424.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panel_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, panel_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F, 12.0F));
    ImGui::Begin("Navigate", nullptr, flags);
    workspace_button(ui_state, WorkspaceMode::Operator);
    workspace_button(ui_state, WorkspaceMode::Advanced);
    workspace_button(ui_state, WorkspaceMode::Developer);
    ImGui::Separator();
    nav_button(ui_state, UiNavigationMode::View);
    if (mode_visible_in_workspace(UiNavigationMode::Layers, ui_state.workspace_mode))
    {
        ImGui::Dummy(ImVec2(0.0F, 2.0F));
        nav_button(ui_state, UiNavigationMode::Layers);
    }
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
    constexpr std::array<const char *, 6> presets = {
        "Operator clean",
        "Terrain analysis",
        "Telemetry review",
        "Debug tiles",
        "Bathymetry",
        "Capture/export",
    };
    for (const char *preset : presets)
    {
        if (ImGui::Button(preset))
        {
            apply_layer_preset(preset,
                               options,
                               ui_state,
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

    ImGui::SeparatorText("Mutable Layers");
    ImGui::Checkbox("GeoTIFF overlays", &overlay_enabled);
    ImGui::SliderFloat("Overlay opacity", &overlay_opacity, 0.0F, 1.0F, "%.2f");
    ImGui::Checkbox("Telemetry tracks", &ui_state.telemetry_tracks_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Entity labels", &ui_state.telemetry_labels_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Plan overlay", &plan_state.overlay_visible);
    bool bathymetry_mutable = ui_state.bathymetry_enabled;
    if (!options.use_bathymetry || options.bathymetry_geotiff.empty())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Bathymetry", &bathymetry_mutable))
    {
        ui_state.bathymetry_enabled = bathymetry_mutable;
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
    ImGui::Checkbox("State colors", &state_colors);
    ImGui::SameLine();
    ImGui::Checkbox("Fallback highlight", &highlight_fallback);

    ImGui::SeparatorText("Layer Stack");
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

        const std::string terrain_sources = layer_source_summary(snapshot);
        const std::string terrain_health =
            snapshot.failed_tiles > 0U ? "degraded"
                                       : (snapshot.resident_gpu_tiles > 0U ? "ok" : "loading");
        draw_layer_row("Base imagery",
                       "on",
                       "1.00",
                       "0",
                       terrain_sources.c_str(),
                       terrain_health.c_str(),
                       "Primary terrain imagery. Source priority is remote HTTP, MBTiles, local "
                       "XYZ, disk cache, then synthetic fallback.");
        draw_layer_row("Terrain shading",
                       "on",
                       "1.00",
                       "built-in",
                       "height raster",
                       snapshot.resident_gpu_tiles > 0U ? "active" : "loading",
                       "Read-only renderer terrain mesh and height shading.");
        draw_layer_row("Elevation color",
                       "on",
                       "1.00",
                       "built-in",
                       options.elevation_geotiff.empty() ? "local_xyz/cache" : "geotiff/cache",
                       file_health(options.elevation_geotiff).c_str(),
                       "Elevation height raster is merged into the terrain mesh and sampled for "
                       "telemetry placement.");
        draw_layer_row("Bathymetry",
                       bool_label(ui_state.bathymetry_enabled).c_str(),
                       "1.00",
                       "merge",
                       options.bathymetry_geotiff.empty() ? "none" : "geotiff",
                       file_health(options.bathymetry_geotiff).c_str(),
                       "Session-only bathymetry merge toggle. It is editable only when bathymetry "
                       "is configured at startup.");
        char overlay_opacity_text[32]{};
        std::snprintf(overlay_opacity_text, sizeof(overlay_opacity_text), "%.2f", overlay_opacity);
        char overlay_order_text[32]{};
        std::snprintf(overlay_order_text, sizeof(overlay_order_text), "%d", options.overlay_order);
        draw_layer_row(
            "GeoTIFF overlays",
            bool_label(overlay_enabled).c_str(),
            overlay_opacity_text,
            overlay_order_text,
            options.overlay_geotiff.empty() && options.overlays.empty() ? "none" : "geotiff",
            (!options.overlay_geotiff.empty() ? file_health(options.overlay_geotiff)
                                              : configured_label(!options.overlays.empty()))
                .c_str(),
            "Configured app overlays are draped over terrain. Opacity applies to the compatibility "
            "overlay.");
        draw_layer_row("MBTiles imagery",
                       options.imagery_mbtiles.empty() ? "off" : "on",
                       "1.00",
                       "source",
                       options.imagery_mbtiles.empty() ? "none" : "mbtiles",
                       file_health(options.imagery_mbtiles).c_str(),
                       "Read-only imagery source configured with --imagery-mbtiles.");
        draw_layer_row("Remote imagery",
                       options.remote_imagery_url_template.empty() ? "off" : "on",
                       "1.00",
                       "source",
                       options.remote_imagery_url_template.empty() ? "none" : "remote-http",
                       configured_label(!options.remote_imagery_url_template.empty()).c_str(),
                       "Read-only imagery source configured with --remote-imagery-url. The app "
                       "does not browse providers.");
        draw_layer_row(
            "Telemetry tracks",
            bool_label(ui_state.telemetry_tracks_visible).c_str(),
            "1.00",
            "overlay",
            playback.live ? "live telemetry" : "offline telemetry",
            playback.loaded ? "active" : "idle",
            "Selected entity trail overlay. Live trails use the configured decimation limit.");
        draw_layer_row("Entity labels",
                       bool_label(ui_state.telemetry_labels_visible).c_str(),
                       "1.00",
                       "overlay",
                       playback.live ? "live telemetry" : "offline telemetry",
                       playback.loaded ? "active" : "idle",
                       "Entity labels remain compact when many entities are visible.");
        draw_layer_row("Plan overlay",
                       bool_label(plan_state.overlay_visible).c_str(),
                       "1.00",
                       "overlay",
                       plan_state.data ? "qgc plan" : "none",
                       !plan_state.error.empty() ? "error" : (plan_state.data ? "active" : "idle"),
                       "Read-only QGroundControl .plan visualization. The app does not add "
                       "mission upload, vehicle commands, drag handles, or write-back paths.");
        draw_layer_row("Debug tile states",
                       bool_label(state_colors).c_str(),
                       "0.35",
                       "debug",
                       "runtime state",
                       state_colors ? "enabled" : "off",
                       "Advanced session toggle for tile state colors. Full runtime tables stay "
                       "Developer-only.");
        draw_layer_row("Fallback highlight",
                       bool_label(highlight_fallback).c_str(),
                       "0.35",
                       "debug",
                       "parent/synthetic",
                       highlight_fallback ? "enabled" : "off",
                       "Highlights parent fallback or synthetic terrain in the current view.");
        ImGui::EndTable();
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

void draw_timeline_controls(TelemetryPlaybackState &playback)
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
        playback.clock.seek(current_time);
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
        draw_timeline_controls(playback);
        if (ui.telemetry_entity_selected)
        {
            draw_review_jump_buttons(playback.review, ui);
            draw_review_charts(playback.review);
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

void draw_signals_panel(TelemetryPlaybackState &playback,
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

void draw_selected_entity_card(TelemetryPlaybackState &playback,
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

    ImGui::SeparatorText("Test");
    metric_row("Test", card.test);
    metric_row("Phase", card.phase);
    metric_row("Target", card.target);

    ImGui::SeparatorText("Telemetry");
    metric_row("Mode", card.mode);
    metric_row("State", card.telemetry_state);
    metric_row("Age", card.telemetry_age);
    metric_row("Confidence", card.terrain_confidence);

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

void draw_inspector(TelemetryPlaybackState &playback,
                    const VehicleRuntimeStatus &vehicle_status,
                    const AppConfigStatusThresholds &thresholds,
                    UiState &ui_state)
{
    if (ui_state.inspector_target == InspectorTarget::None || ImGui::GetIO().DisplaySize.x < 980.0F)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - inspector_width - chrome_margin,
                                   status_bar_height + chrome_margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(inspector_width, 520.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
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
        draw_selected_entity_card(playback, vehicle_status, thresholds, ui_state);
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
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void draw_review_tape(TelemetryPlaybackState &playback, UiState &ui_state, const float width)
{
    const double start = playback.timeline.start_time_s;
    const double end = playback.timeline.end_time_s;
    const double duration = std::max(1.0e-6, end - start);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 tape_min = ImGui::GetCursorScreenPos();
    const ImVec2 tape_size(width, 22.0F);
    const ImVec2 tape_max(tape_min.x + tape_size.x, tape_min.y + tape_size.y);
    ImGui::InvisibleButton("review_tape", tape_size);
    draw->AddRectFilled(tape_min, tape_max, IM_COL32(12, 15, 18, 235), 5.0F);
    draw->AddRect(tape_min, tape_max, IM_COL32(65, 73, 80, 230), 5.0F);
    for (std::size_t index = 0U; index < playback.review.markers.size(); ++index)
    {
        const auto &marker = playback.review.markers[index];
        const float x =
            tape_min.x + static_cast<float>((marker.time_s - start) / duration) * tape_size.x;
        const bool selected = ui_state.selected_review_marker_index &&
                              *ui_state.selected_review_marker_index == index;
        draw->AddLine(ImVec2(x, tape_min.y + 3.0F),
                      ImVec2(x, tape_max.y - 3.0F),
                      marker_color(marker.category),
                      selected ? 3.2F : (marker.end_time_s ? 2.4F : 1.5F));
    }
    const float current_x =
        tape_min.x + static_cast<float>((playback.clock.time_s() - start) / duration) * tape_size.x;
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
        request_review_jump(ui_state,
                            start + duration * static_cast<double>(local_x / tape_size.x));
    }
}

void draw_bottom_timeline(TelemetryPlaybackState &playback, UiState &ui_state)
{
    if (!playback.loaded)
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
    ImGui::SetNextWindowPos(ImVec2(left, ImGui::GetIO().DisplaySize.y - 112.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(available_width, 106.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055F, 0.064F, 0.072F, 0.88F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 10.0F));
    ImGui::Begin("Timeline", nullptr, flags);
    if (playback.live)
    {
        ImGui::TextColored(
            ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "Live %.3f s", playback.timeline.end_time_s);
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
        ImGui::TextColored(playback.receiver_stats.stale ? stale_amber : live_green,
                           "%s",
                           playback.receiver_stats.stale ? "stale" : "tracking latest");
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
        ImGui::TextColored(text_muted, "markers %zu", playback.review.markers.size());
        draw_review_tape(playback, ui_state, ImGui::GetContentRegionAvail().x);
        if (ImGui::Button(playback.clock.paused() ? "Play" : "Pause"))
        {
            playback.clock.set_paused(!playback.clock.paused());
        }
        ImGui::SameLine();
        const auto previous =
            previous_review_marker(playback.review.markers, playback.clock.time_s());
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
        const auto next = next_review_marker(playback.review.markers, playback.clock.time_s());
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
        if (ImGui::Button("Bookmark"))
        {
            add_timeline_bookmark(ui_state.timeline_bookmarks, playback.clock.time_s());
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
    draw_nav(ui_state);

    const float panel_left = chrome_margin + nav_width + panel_gap;
    const float panel_top = status_bar_height + chrome_margin;
    const float inspector_reserve =
        ImGui::GetIO().DisplaySize.x >= 980.0F ? inspector_width + 36.0F : 24.0F;
    const float main_width =
        std::clamp(ImGui::GetIO().DisplaySize.x - panel_left - inspector_reserve, 320.0F, 470.0F);
    ImGui::SetNextWindowPos(ImVec2(panel_left, panel_top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(main_width, 430.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panel_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, panel_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 12.0F));
    ImGui::Begin(mode_label(ui_state.active_mode), nullptr, flags);
    const bool advanced_workspace = ui_state.workspace_mode != WorkspaceMode::Operator;
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
        draw_telemetry_panel(options, playback, ui_state, advanced_workspace);
        break;
    case UiNavigationMode::Signals:
        ui_state.inspector_target = InspectorTarget::TelemetrySource;
        draw_signals_panel(playback, runtime_signals, ui_state);
        break;
    case UiNavigationMode::Capture:
        ui_state.inspector_target = InspectorTarget::None;
        draw_capture_panel(screenshot_tool, mp4_recorder, advanced_workspace);
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
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    draw_inspector(playback, vehicle_status, options.status_thresholds, ui_state);
    draw_plot_shelf(options, playback, runtime_signals, ui_state.plot_ui);
    draw_bottom_timeline(playback, ui_state);
}

} // namespace animus::app
