#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace animus::app
{

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
    std::filesystem::path telemetry_tlog;
    float playback_rate = 1.0F;
    bool playback_start_paused = false;
};

Options parse_options(int argc, char **argv);
void save_app_config(const Options &options);

} // namespace animus::app
