#include "ui.hpp"

#include "capture.hpp"

#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
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
    case UiNavigationMode::Capture:
        return "Capture";
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

const char *telemetry_state_label(const TelemetryPlaybackState &playback)
{
    if (!playback.loaded)
    {
        return "Telemetry idle";
    }
    if (!playback.live)
    {
        return playback.clock.paused() ? "Telemetry paused" : "Telemetry running";
    }
    if (!playback.receiver_stats.connected)
    {
        return "Telemetry waiting";
    }
    return playback.receiver_stats.stale ? "Telemetry stale" : "Telemetry live";
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

PillState telemetry_pill_state(const TelemetryPlaybackState &playback)
{
    if (!playback.loaded)
    {
        return PillState::Inactive;
    }
    if (playback.live && (!playback.receiver_stats.connected || playback.receiver_stats.stale))
    {
        return PillState::Warning;
    }
    if (playback.terrain_height_unavailable || playback.unknown_datum_relative_fallback ||
        playback.geoid_correction_unavailable)
    {
        return PillState::Warning;
    }
    return PillState::Good;
}

std::string selected_entity_summary(const TelemetryPlaybackState &playback, const UiState &ui_state)
{
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return "Selected none";
    }
    const auto sample = current_entity_sample(playback, playback.selected_entity);
    std::string summary = "Selected " + entity_label(playback.selected_entity);
    if (sample && sample->ground_speed_mps)
    {
        summary += " " + format_value("%.1f m/s", *sample->ground_speed_mps);
    }
    return summary;
}

PillState selected_entity_state(const TelemetryPlaybackState &playback, const UiState &ui_state)
{
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return PillState::Inactive;
    }
    const auto sample = current_entity_sample(playback, playback.selected_entity);
    if (!sample)
    {
        return PillState::Warning;
    }
    return entity_stale(playback, *sample) || entity_degraded(*sample) ? PillState::Warning
                                                                       : PillState::Good;
}

std::string time_summary(const TelemetryPlaybackState &playback)
{
    if (!playback.loaded)
    {
        return "Time idle";
    }
    if (playback.live)
    {
        return format_value("Time %.1f s live", playback.timeline.end_time_s);
    }
    return format_value(playback.clock.paused() ? "Time %.1f s paused" : "Time %.1f s",
                        playback.clock.time_s());
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
                         const TelemetryPlaybackState &playback,
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
    const std::string terrain_summary =
        snapshot.failed_tiles > 0U
            ? "Terrain errors"
            : "Terrain OK - " + std::to_string(snapshot.resident_gpu_tiles) + " resident";
    const PillState terrain_state = snapshot.failed_tiles > 0U
                                        ? PillState::Error
                                        : ((snapshot.resident_gpu_tiles == 0U ||
                                            snapshot.loading_jobs > 0U || snapshot.queued_jobs > 0U)
                                               ? PillState::Warning
                                               : PillState::Good);
    const bool imagery_available =
        !options.imagery_mbtiles.empty() || !options.remote_imagery_url_template.empty();
    const bool elevation_available =
        !options.elevation_geotiff.empty() || snapshot.cache_stats.persisted_tiles > 0U ||
        snapshot.cache_stats.synthesized_tiles > 0U || snapshot.resident_gpu_tiles > 0U;
    const std::string frame_ms = format_value("Perf %.1f ms", stats.last_frame_seconds() * 1000.0);
    const PillState perf_state =
        stats.last_frame_seconds() > 0.033 ? PillState::Warning : PillState::Good;
    const std::string capture_summary =
        recorder.recording ? "Capture recording " + std::to_string(recorder.frame_count)
                           : (screenshot_tool.pending_png ? "Capture saving" : "Capture idle");

    if (draw_status_pill("mode_status",
                         ("Mode " + std::string(workspace_label(ui_state.workspace_mode))).c_str(),
                         PillState::Good))
    {
        ImGui::Text("Workspace %s", workspace_label(ui_state.workspace_mode));
        ImGui::Text("Panel %s", mode_label(ui_state.active_mode));
        ImGui::EndPopup();
    }
    same_line_if_room();
    if (draw_status_pill("terrain_status", terrain_summary.c_str(), terrain_state))
    {
        ImGui::Text("resident %zu", snapshot.resident_gpu_tiles);
        ImGui::Text("queued %zu loading %zu ready-cpu %zu failed %zu",
                    snapshot.queued_jobs,
                    snapshot.loading_jobs,
                    snapshot.ready_cpu_tiles,
                    snapshot.failed_tiles);
        ImGui::Text("gpu %.2f MiB", static_cast<double>(resident_gpu_bytes) / (1024.0 * 1024.0));
        ImGui::EndPopup();
    }
    same_line_if_room();
    if (draw_status_pill("imagery_status",
                         imagery_available ? "Imagery configured" : "Imagery local tiles",
                         imagery_available ? PillState::Good : PillState::Inactive))
    {
        ImGui::Text("MBTiles %s",
                    options.imagery_mbtiles.empty() ? "not set"
                                                    : options.imagery_mbtiles.string().c_str());
        ImGui::Text("remote %s",
                    options.remote_imagery_url_template.empty()
                        ? "not set"
                        : options.remote_imagery_url_template.c_str());
        ImGui::EndPopup();
    }
    same_line_if_room();
    if (draw_status_pill("elevation_status",
                         elevation_available ? "Elevation active" : "Elevation synthetic",
                         elevation_available ? PillState::Good : PillState::Warning))
    {
        ImGui::Text("GeoTIFF %s",
                    options.elevation_geotiff.empty() ? "not set"
                                                      : options.elevation_geotiff.string().c_str());
        ImGui::Text("bathymetry %s", options.use_bathymetry ? "enabled" : "disabled");
        ImGui::Text("synthesized %llu",
                    static_cast<unsigned long long>(snapshot.cache_stats.synthesized_tiles));
        ImGui::EndPopup();
    }
    same_line_if_room();
    if (draw_status_pill(
            "telemetry_status", telemetry_state_label(playback), telemetry_pill_state(playback)))
    {
        ImGui::Text("source %s",
                    playback.live ? "Live UDP" : (!options.telemetry.empty() ? "Log" : "none"));
        ImGui::Text("entities %zu samples %zu events %zu",
                    playback.timeline.entities.size(),
                    playback.timeline.samples.size(),
                    playback.timeline.events.size());
        if (playback.live)
        {
            ImGui::Text("endpoint %s", playback.live_endpoint.c_str());
            ImGui::Text("datagrams %llu age %.3f s",
                        static_cast<unsigned long long>(playback.receiver_stats.datagrams),
                        playback.receiver_stats.last_packet_age_s);
        }
        ImGui::EndPopup();
    }
    same_line_if_room();
    const std::string entity_summary = selected_entity_summary(playback, ui_state);
    if (draw_status_pill("selected_entity_status",
                         entity_summary.c_str(),
                         selected_entity_state(playback, ui_state)))
    {
        ImGui::Text("selected %s",
                    ui_state.telemetry_entity_selected
                        ? entity_label(playback.selected_entity).c_str()
                        : "none");
        ImGui::Text("follow %s", ui_state.follow_selected_entity ? "enabled" : "disabled");
        ImGui::EndPopup();
    }
    same_line_if_room();
    const std::string time_text = time_summary(playback);
    if (draw_status_pill("time_status",
                         time_text.c_str(),
                         playback.loaded ? PillState::Good : PillState::Inactive))
    {
        ImGui::Text(
            "range %.3f..%.3f s", playback.timeline.start_time_s, playback.timeline.end_time_s);
        ImGui::Text("rate %.2fx", playback.clock.rate());
        ImGui::Text("paused %s", playback.clock.paused() ? "yes" : "no");
        ImGui::EndPopup();
    }
    same_line_if_room();
    if (draw_status_pill("capture_status",
                         capture_summary.c_str(),
                         recorder.recording ? PillState::Warning : PillState::Inactive))
    {
        ImGui::Text("PNG %s", screenshot_path(screenshot_tool).string().c_str());
        ImGui::Text("MP4 %s", recorder_output_path(recorder).string().c_str());
        ImGui::Text("status %s", recorder.status.c_str());
        ImGui::EndPopup();
    }
    same_line_if_room();
    if (draw_status_pill("perf_status", frame_ms.c_str(), perf_state))
    {
        ImGui::Text("frames %d", stats.frame_count());
        ImGui::Text("last %.3f ms", stats.last_frame_seconds() * 1000.0);
        ImGui::Text("total %.3f s", stats.total_seconds());
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void draw_nav(UiState &ui_state)
{
    sanitize_active_mode(ui_state);
    ImGui::SetNextWindowPos(ImVec2(chrome_margin, status_bar_height + chrome_margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(nav_width, 342.0F), ImGuiCond_Always);
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
    nav_button(ui_state, UiNavigationMode::Capture);
    if (mode_visible_in_workspace(UiNavigationMode::Developer, ui_state.workspace_mode))
    {
        ImGui::Separator();
        nav_button(ui_state, UiNavigationMode::Developer);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void draw_view_panel(const Options &options,
                     const std::filesystem::path &pack_root,
                     const Camera &camera,
                     int selected_zoom,
                     bool &state_colors,
                     bool &highlight_fallback)
{
    ImGui::Text("zoom %d  range %d..%d", selected_zoom, options.min_z, options.max_z);
    ImGui::Text("camera %.2f %.2f %.2f  distance %.2f",
                camera.target.x,
                camera.target.y,
                camera.target.z,
                camera.distance);
    ImGui::Separator();
    ImGui::Text("pack");
    ImGui::TextWrapped("%s", pack_root.string().c_str());
    ImGui::Text(
        "center %d/%d  height %.6f", options.center_x, options.center_y, options.height_scale);
    ImGui::Checkbox("State colors", &state_colors);
    ImGui::SameLine();
    ImGui::Checkbox("Fallback highlight", &highlight_fallback);
}

void draw_layer_panel(const Options &options, bool &overlay_enabled, float &overlay_opacity)
{
    ImGui::Checkbox("Overlay enabled", &overlay_enabled);
    ImGui::SliderFloat("Overlay opacity", &overlay_opacity, 0.0F, 1.0F, "%.2f");
    ImGui::Text("overlay order %d", options.overlay_order);
    ImGui::Text("overlay layers %zu", options.overlays.size());
    ImGui::Text("bathymetry %s", options.use_bathymetry ? "enabled" : "disabled");
    ImGui::Separator();
    ImGui::Text(
        "elevation GeoTIFF %s",
        options.elevation_geotiff.empty()
            ? "not set"
            : (std::filesystem::exists(options.elevation_geotiff) ? "available" : "missing"));
    ImGui::Text(
        "bathymetry GeoTIFF %s",
        options.bathymetry_geotiff.empty()
            ? "not set"
            : (std::filesystem::exists(options.bathymetry_geotiff) ? "available" : "missing"));
    ImGui::Text("imagery MBTiles %s",
                options.imagery_mbtiles.empty()
                    ? "not set"
                    : (std::filesystem::exists(options.imagery_mbtiles) ? "available" : "missing"));
    ImGui::Text("remote imagery %s",
                options.remote_imagery_url_template.empty() ? "not set" : "configured");
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

void draw_inspector(TelemetryPlaybackState &playback,
                    const VehicleRuntimeStatus &vehicle_status,
                    UiState &ui_state)
{
    if (ui_state.inspector_target == InspectorTarget::None || ImGui::GetIO().DisplaySize.x < 980.0F)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - inspector_width - chrome_margin,
                                   status_bar_height + chrome_margin),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(inspector_width, 330.0F), ImGuiCond_Always);
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
        if (!ui_state.telemetry_entity_selected)
        {
            ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F), "Telemetry");
            muted_text("No entity selected.");
            ImGui::Text("source %s", playback.live ? "Live UDP" : "Playback");
            ImGui::Text("entities %zu", playback.timeline.entities.size());
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            return;
        }
        const auto sample =
            playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
        const bool degraded = !sample || entity_degraded(*sample);
        const bool stale = sample ? entity_stale(playback, *sample) : false;
        ImGui::TextColored(ImVec4(0.88F, 0.92F, 0.95F, 1.0F),
                           "Vehicle %s",
                           entity_label(playback.selected_entity).c_str());
        ImGui::SameLine();
        draw_status_dot(degraded || stale ? stale_amber : live_green);
        ImGui::TextColored(
            degraded || stale ? stale_amber : live_green,
            "%s",
            !sample ? "no current sample"
                    : (degraded ? "invalid position" : (stale ? "stale" : "telemetry valid")));
        ImGui::Text("Vehicle   %s", vehicle_status.default_vehicle_name.c_str());
        ImGui::Text("ID        %s", vehicle_status.default_vehicle_id.c_str());
        ImGui::Text("Type      %s", vehicle_status.default_vehicle_type.c_str());
        ImGui::Text("Model     %s", vehicle_status.model_status.c_str());
        ImGui::Checkbox("Follow selected", &ui_state.follow_selected_entity);
        if (sample)
        {
            ImGui::SeparatorText("Summary");
            ImGui::Text(
                "Altitude  %s",
                format_altitude(sample_altitude_m(*sample), sample->altitude_datum).c_str());
            ImGui::Text("Speed     %s", format_speed(sample->ground_speed_mps).c_str());
            ImGui::Text("Age       %s", format_age(entity_age_s(playback, *sample)).c_str());
            ImGui::SeparatorText("Details");
            ImGui::Text("Lat/Lon   %.7f  %.7f", sample->lat_deg, sample->lon_deg);
            ImGui::Text("Datum     %s", altitude_datum_label(sample->altitude_datum));
            ImGui::Text("Heading   %s",
                        sample->heading_deg ? format_value("%.1f deg", *sample->heading_deg).c_str()
                                            : "n/a");
            ImGui::Text("Roll      %s",
                        sample->roll_rad ? format_value("%.3f rad", *sample->roll_rad).c_str()
                                         : "n/a");
            ImGui::Text("Pitch     %s",
                        sample->pitch_rad ? format_value("%.3f rad", *sample->pitch_rad).c_str()
                                          : "n/a");
            ImGui::Text("Yaw       %s",
                        sample->yaw_rad ? format_value("%.3f rad", *sample->yaw_rad).c_str()
                                        : "n/a");
            ImGui::Text("Sample    %.3f s", sample->time_s);
            if (stale || degraded)
            {
                ImGui::SeparatorText("Warnings");
                if (stale)
                {
                    ImGui::TextColored(stale_amber, "Telemetry is stale");
                }
                if (degraded)
                {
                    ImGui::TextColored(stale_amber, "Invalid position");
                }
            }
        }
        else
        {
            ImGui::Separator();
            muted_text("No current sample for the selected entity.");
        }
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

void draw_bottom_timeline(TelemetryPlaybackState &playback, const UiState &ui_state)
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
    ImGui::SetNextWindowPos(ImVec2(left, ImGui::GetIO().DisplaySize.y - 82.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(available_width, 76.0F), ImGuiCond_Always);
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
        draw_timeline_controls(playback);
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

void draw_app_workspace(const Options &options,
                        const std::filesystem::path &pack_root,
                        const animus::render_core::GlInfo &gl_info,
                        const animus::render_core::RenderStats &stats,
                        const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                        const Camera &camera,
                        int selected_zoom,
                        const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                        std::size_t upload_bytes_used,
                        int texture_uploads_used,
                        int mesh_uploads_used,
                        std::size_t resident_gpu_bytes,
                        TelemetryPlaybackState &playback,
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
    draw_top_status_bar(options,
                        stats,
                        snapshot,
                        playback,
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
    ImGui::SetNextWindowSize(ImVec2(main_width, 390.0F), ImGuiCond_Always);
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
        draw_view_panel(
            options, pack_root, camera, selected_zoom, state_colors, highlight_fallback);
        break;
    case UiNavigationMode::Layers:
        ui_state.inspector_target = InspectorTarget::Layer;
        draw_layer_panel(options, overlay_enabled, overlay_opacity);
        break;
    case UiNavigationMode::Telemetry:
        ui_state.inspector_target =
            playback.loaded ? InspectorTarget::Entity : InspectorTarget::TelemetrySource;
        draw_telemetry_panel(options, playback, ui_state, advanced_workspace);
        break;
    case UiNavigationMode::Capture:
        ui_state.inspector_target = InspectorTarget::None;
        draw_capture_panel(screenshot_tool, mp4_recorder, advanced_workspace);
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

    draw_inspector(playback, vehicle_status, ui_state);
    draw_bottom_timeline(playback, ui_state);
}

} // namespace animus::app
