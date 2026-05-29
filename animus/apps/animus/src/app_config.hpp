#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "plot_config.hpp"

namespace animus::app
{

struct AppConfigOverlay
{
    std::filesystem::path path;
    bool enabled = true;
    float opacity = 0.65F;
    int draw_order = 0;
    std::string cache_identity;

    bool operator==(const AppConfigOverlay &) const = default;
};

struct AppLayerSettings
{
    bool vehicle_icons_visible = true;
    bool vehicle_labels_visible = true;
    bool track_tail_visible = true;
    bool heading_vectors_visible = true;
    bool planned_route_visible = true;
    bool geofence_rally_visible = true;
    bool terrain_confidence_visible = true;
    bool terrain_clearance_heatmap_visible = false;
    bool geotiff_overlay_visible = true;
    float geotiff_overlay_opacity = 0.65F;
    int geotiff_overlay_draw_order = 0;
    bool bathymetry_visible = false;
    float bathymetry_opacity = 1.0F;
    bool hillshade_visible = true;
    bool tile_state_debug_visible = false;
    bool fallback_highlight_visible = false;

    bool operator==(const AppLayerSettings &) const = default;
};

struct AppConfigStatusThresholds
{
    double terrain_clearance_warning_m = 30.0;
    double terrain_clearance_critical_m = 10.0;
    double roll_warning_deg = 45.0;
    double pitch_warning_deg = 30.0;
    double frame_time_warning_ms = 33.0;
    double link_hz_warning = 2.0;
    double telemetry_gap_warning_s = 2.0;
    double telemetry_gap_critical_s = 5.0;

    bool operator==(const AppConfigStatusThresholds &) const = default;
};

struct AppWindowRect
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    bool operator==(const AppWindowRect &) const = default;
};

struct AppWorkspaceLayout
{
    std::string active_panel = "view";
    std::string view_mode = "terrain3d";
    std::string map_orientation = "north_up";
    bool plot_shelf_visible = true;
    float plot_shelf_height_px = 220.0F;
    bool timeline_visible = true;
    float timeline_height_px = 144.0F;
    bool inspector_visible = true;
    AppWindowRect main_panel;
    AppWindowRect inspector;
    AppWindowRect timeline;
    AppWindowRect plot_shelf;

    bool operator==(const AppWorkspaceLayout &) const = default;
};

struct AppConfig
{
    int version = 1;
    std::string workspace_mode = "fly_test";
    std::string active_panel = "view";
    int window_width = 1280;
    int window_height = 720;
    std::string view_mode = "terrain3d";
    bool follow_selected = false;
    std::string map_orientation = "north_up";
    bool telemetry_tracks_visible = true;
    bool telemetry_labels_visible = true;
    bool bathymetry_enabled = false;
    bool developer_diagnostics_visible = false;
    bool telemetry_diagnostics_visible = false;
    bool mavlink_inspector_visible = true;
    bool overlay_enabled = true;
    float overlay_opacity = 0.65F;
    AppLayerSettings layers;
    std::vector<AppConfigOverlay> overlays;
    std::string telemetry_live_udp_host = "127.0.0.1";
    std::uint16_t telemetry_live_udp_port = 14550;
    double telemetry_live_buffer_s = 120.0;
    std::size_t telemetry_live_max_samples = 20000U;
    std::size_t telemetry_live_render_max_points = 1000U;
    AppConfigStatusThresholds status_thresholds;
    PlotShelfConfig plots = default_plot_shelf_config();
    std::map<std::string, AppWorkspaceLayout> workspace_layouts;

    bool operator==(const AppConfig &) const = default;
};

enum class AppConfigLoadStatus
{
    Missing,
    Loaded,
    LoadedLegacy,
    Error,
};

struct AppConfigLoadResult
{
    AppConfig config;
    AppConfigLoadStatus status = AppConfigLoadStatus::Missing;
    std::vector<std::string> diagnostics;
};

struct AppConfigSaveResult
{
    bool saved = false;
    std::filesystem::path path;
    std::vector<std::string> diagnostics;
};

[[nodiscard]] AppConfig default_app_config();
[[nodiscard]] std::string canonical_workspace_id(std::string_view value);
[[nodiscard]] AppWorkspaceLayout default_workspace_layout(std::string_view workspace_id);
[[nodiscard]] AppConfigLoadResult load_app_config_file(const std::filesystem::path &path);
[[nodiscard]] AppConfigSaveResult save_app_config_file(const std::filesystem::path &path,
                                                       const AppConfig &config);
[[nodiscard]] const char *app_config_load_status_label(AppConfigLoadStatus status);

} // namespace animus::app
