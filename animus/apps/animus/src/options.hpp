#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "animus/telemetry_core/telemetry.hpp"

namespace animus::app
{

enum class WorkspaceMode
{
    FlyTest,
    Plan,
    Analyze,
    Terrain,
    Developer,
};

enum class ViewMode
{
    Terrain3D,
    Map2D,
    Oblique25D,
};

struct OverlayLayerConfig
{
    std::filesystem::path path;
    bool enabled = true;
    float opacity = 0.65F;
    int draw_order = 0;
    std::string diagnostics;
    std::string cache_identity;
};

struct Options
{
    bool smoke = false;
    int frames = 0;
    int width = 1280;
    int height = 720;
    std::filesystem::path pack_root = "animus/data/tiles/lake_tahoe";
    std::filesystem::path cache_root = "animus/cache/terrain";
    std::filesystem::path elevation_geotiff;
    std::filesystem::path bathymetry_geotiff;
    std::filesystem::path imagery_mbtiles;
    std::string remote_imagery_url_template;
    std::string remote_imagery_cache_identity;
    std::string remote_imagery_user_agent = "Animus/0.1";
    int remote_imagery_timeout_ms = 5000;
    std::filesystem::path overlay_geotiff;
    float overlay_opacity = 0.65F;
    int overlay_order = 0;
    bool overlay_enabled = true;
    std::vector<OverlayLayerConfig> overlays;
    std::filesystem::path geoid_grid;
    std::filesystem::path config_path;
    bool load_config = true;
    std::string config_load_status = "not loaded";
    std::string config_save_status = "not saved";
    bool config_dirty = false;
    std::vector<std::string> config_diagnostics;
    WorkspaceMode workspace_mode = WorkspaceMode::FlyTest;
    ViewMode view_mode = ViewMode::Terrain3D;
    std::string active_panel = "view";
    bool follow_selected_entity = false;
    std::string map_orientation = "north_up";
    bool telemetry_tracks_visible = true;
    bool telemetry_labels_visible = true;
    bool developer_diagnostics_visible = false;
    bool telemetry_diagnostics_visible = false;
    bool mavlink_inspector_visible = true;
    AppConfigStatusThresholds status_thresholds;
    std::map<std::string, AppWorkspaceLayout> workspace_layouts;
    bool developer_workspace = false;
    std::filesystem::path capture_ppm;
    std::filesystem::path capture_png;
    std::filesystem::path capture_sequence_dir;
    int capture_sequence_fps = 30;
    int z = 12;
    int min_z = 11;
    int max_z = 13;
    int center_x = 682;
    int center_y = 1563;
    int patch_radius = 1;
    float height_scale = 0.0015F;
    bool debug_overlay = true;
    int worker_count = 2;
    std::size_t tile_budget = 25;
    std::size_t resident_tile_cap = 64;
    std::size_t max_outstanding_jobs = 16;
    int max_texture_uploads = 2;
    int max_mesh_uploads = 2;
    std::size_t max_upload_bytes = 32U * 1024U * 1024U;
    int simulate_slow_load_ms = 0;
    bool use_bathymetry = false;
    std::filesystem::path telemetry;
    std::filesystem::path telemetry_tlog;
    bool telemetry_live_udp_enabled = false;
    std::string telemetry_live_udp_host = "127.0.0.1";
    std::uint16_t telemetry_live_udp_port = 14550;
    double telemetry_live_buffer_s = 120.0;
    std::size_t telemetry_live_max_samples = 20000U;
    std::size_t telemetry_live_render_max_points = 1000U;
    std::filesystem::path telemetry_live_debug_csv;
    PlotShelfConfig plots = default_plot_shelf_config();
    std::filesystem::path plan;
    animus::telemetry_core::TelemetryImportFormat telemetry_format =
        animus::telemetry_core::TelemetryImportFormat::Tlog;
    float playback_rate = 1.0F;
    bool playback_start_paused = false;
};

Options parse_options(int argc, char **argv);
std::filesystem::path default_app_config_path();
void apply_app_config_to_options(Options &options, const AppConfig &config);
AppConfig app_config_from_options(const Options &options);
AppConfigSaveResult save_app_config(Options &options);

} // namespace animus::app
