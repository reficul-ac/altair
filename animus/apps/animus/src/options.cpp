#include "options.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace animus::app
{
namespace
{

int parse_positive_int(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const int parsed = std::stoi(std::string(value), &consumed);
    if (consumed != value.size() || parsed <= 0)
    {
        throw std::invalid_argument(std::string("Expected positive integer for ") +
                                    std::string(name));
    }
    return parsed;
}

int parse_int(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const int parsed = std::stoi(std::string(value), &consumed);
    if (consumed != value.size())
    {
        throw std::invalid_argument("Expected integer for " + std::string(name));
    }
    return parsed;
}

float parse_positive_float(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const float parsed = std::stof(std::string(value), &consumed);
    if (consumed != value.size() || parsed <= 0.0F)
    {
        throw std::invalid_argument(std::string("Expected positive float for ") +
                                    std::string(name));
    }
    return parsed;
}

float parse_unit_float(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const float parsed = std::stof(std::string(value), &consumed);
    if (consumed != value.size() || parsed < 0.0F || parsed > 1.0F)
    {
        throw std::invalid_argument(std::string("Expected 0..1 float for ") + std::string(name));
    }
    return parsed;
}

std::size_t parse_positive_size(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const auto parsed = static_cast<std::size_t>(std::stoull(std::string(value), &consumed));
    if (consumed != value.size() || parsed == 0U)
    {
        throw std::invalid_argument(std::string("Expected positive integer for ") +
                                    std::string(name));
    }
    return parsed;
}

std::uint16_t parse_udp_port(std::string_view name, std::string_view value)
{
    const int parsed = parse_positive_int(name, value);
    if (parsed > 65535)
    {
        throw std::invalid_argument(std::string("Expected UDP port 1..65535 for ") +
                                    std::string(name));
    }
    return static_cast<std::uint16_t>(parsed);
}

WorkspaceMode parse_workspace_mode(std::string_view value)
{
    const std::string canonical = canonical_workspace_id(value);
    if (canonical == "fly_test")
    {
        return WorkspaceMode::FlyTest;
    }
    if (canonical == "plan")
    {
        return WorkspaceMode::Plan;
    }
    if (canonical == "analyze")
    {
        return WorkspaceMode::Analyze;
    }
    if (canonical == "terrain")
    {
        return WorkspaceMode::Terrain;
    }
    if (canonical == "developer")
    {
        return WorkspaceMode::Developer;
    }
    throw std::invalid_argument(
        "workspace mode must be fly_test, plan, analyze, terrain, or developer");
}

ViewMode parse_view_mode(std::string_view value)
{
    if (value == "terrain3d")
    {
        return ViewMode::Terrain3D;
    }
    if (value == "map2d")
    {
        return ViewMode::Map2D;
    }
    if (value == "oblique25d")
    {
        return ViewMode::Oblique25D;
    }
    throw std::invalid_argument("view mode must be terrain3d, map2d, or oblique25d");
}

const char *workspace_mode_config_value(const WorkspaceMode mode)
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
    case WorkspaceMode::Developer:
        return "developer";
    }
    return "fly_test";
}

const char *view_mode_config_value(const ViewMode mode)
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

double parse_positive_double(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const double parsed = std::stod(std::string(value), &consumed);
    if (consumed != value.size() || parsed <= 0.0)
    {
        throw std::invalid_argument(std::string("Expected positive number for ") +
                                    std::string(name));
    }
    return parsed;
}

void parse_host_port(std::string_view option, std::string_view value, Options &options)
{
    const std::size_t colon = value.rfind(':');
    if (colon == std::string_view::npos || colon == 0U || colon + 1U >= value.size())
    {
        throw std::invalid_argument(std::string(option) + " must be HOST:PORT");
    }
    options.telemetry_live_udp_host = std::string(value.substr(0U, colon));
    options.telemetry_live_udp_port = parse_udp_port(option, value.substr(colon + 1U));
    options.telemetry_live_udp_enabled = true;
}

std::string_view next_arg(int argc, char **argv, int &index, std::string_view option)
{
    if (++index >= argc)
    {
        throw std::invalid_argument("Missing value for " + std::string(option));
    }
    return argv[index];
}

bool valid_workspace(std::string_view value)
{
    return !canonical_workspace_id(value).empty();
}

bool valid_view_mode(std::string_view value)
{
    return value == "terrain3d" || value == "map2d";
}

bool valid_active_panel(std::string_view value)
{
    return value == "view" || value == "layers" || value == "telemetry" || value == "signals" ||
           value == "capture" || value == "developer" || value == "settings";
}

bool valid_map_orientation(std::string_view value)
{
    return value == "north_up" || value == "track_up" || value == "free_rotate";
}

void load_app_config(Options &options)
{
    if (!options.load_config)
    {
        options.config_load_status = "skipped";
        options.config_diagnostics.push_back("config load skipped by --no-load-config");
        return;
    }

    const AppConfigLoadResult result = load_app_config_file(options.config_path);
    options.config_load_status = app_config_load_status_label(result.status);
    options.config_diagnostics.insert(
        options.config_diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
    if (result.status == AppConfigLoadStatus::Loaded ||
        result.status == AppConfigLoadStatus::LoadedLegacy)
    {
        apply_app_config_to_options(options, result.config);
    }
}

void upsert_compat_overlay(Options &options)
{
    if (options.overlay_geotiff.empty())
    {
        return;
    }
    OverlayLayerConfig layer;
    layer.path = options.overlay_geotiff;
    layer.enabled = options.overlay_enabled;
    layer.opacity = options.overlay_opacity;
    layer.draw_order = options.overlay_order;
    layer.cache_identity = "geotiff:" + layer.path.generic_string();

    if (options.overlays.empty())
    {
        options.overlays.push_back(std::move(layer));
    }
    else
    {
        options.overlays.front() = std::move(layer);
    }
}

void sort_overlays(Options &options)
{
    for (OverlayLayerConfig &layer : options.overlays)
    {
        if (layer.cache_identity.empty() && !layer.path.empty())
        {
            layer.cache_identity = "geotiff:" + layer.path.generic_string();
        }
    }
    std::stable_sort(options.overlays.begin(),
                     options.overlays.end(),
                     [](const OverlayLayerConfig &a, const OverlayLayerConfig &b)
                     {
                         if (a.draw_order != b.draw_order)
                         {
                             return a.draw_order < b.draw_order;
                         }
                         return a.path.generic_string() < b.path.generic_string();
                     });
}

} // namespace

std::filesystem::path default_app_config_path()
{
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
    {
        return std::filesystem::path(xdg) / "animus" / "animus.yaml";
    }
    if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
    {
        return std::filesystem::path(home) / ".config" / "animus" / "animus.yaml";
    }
    return std::filesystem::path(".config") / "animus" / "animus.yaml";
}

Options parse_options(int argc, char **argv)
{
    Options options;
    options.config_path = default_app_config_path();

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg = argv[index];
        if (arg == "--config")
        {
            options.config_path = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--no-load-config")
        {
            options.load_config = false;
        }
    }
    load_app_config(options);

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg = argv[index];
        if (arg == "--smoke")
        {
            options.smoke = true;
        }
        else if (arg == "--frames")
        {
            options.frames = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--width")
        {
            options.width = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--height")
        {
            options.height = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--pack-root")
        {
            options.pack_root = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--cache-root")
        {
            options.cache_root = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--elevation-geotiff")
        {
            options.elevation_geotiff = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--bathymetry-geotiff")
        {
            options.bathymetry_geotiff = std::string(next_arg(argc, argv, index, arg));
            options.use_bathymetry = true;
        }
        else if (arg == "--imagery-mbtiles")
        {
            options.imagery_mbtiles = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--remote-imagery-url")
        {
            options.remote_imagery_url_template = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--remote-imagery-cache-id")
        {
            options.remote_imagery_cache_identity = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--remote-imagery-user-agent")
        {
            options.remote_imagery_user_agent = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--remote-imagery-timeout-ms")
        {
            options.remote_imagery_timeout_ms =
                parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--overlay-geotiff")
        {
            options.overlay_geotiff = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--overlay")
        {
            OverlayLayerConfig layer;
            layer.path = std::string(next_arg(argc, argv, index, arg));
            layer.opacity = options.overlay_opacity;
            layer.draw_order = options.overlay_order + static_cast<int>(options.overlays.size());
            layer.enabled = true;
            layer.cache_identity = "geotiff:" + layer.path.generic_string();
            options.overlays.push_back(std::move(layer));
        }
        else if (arg == "--overlay-opacity")
        {
            options.overlay_opacity = parse_unit_float(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--overlay-order")
        {
            options.overlay_order = parse_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--geoid-grid")
        {
            options.geoid_grid = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--config")
        {
            options.config_path = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--no-load-config")
        {
            options.load_config = false;
        }
        else if (arg == "--z")
        {
            options.z = parse_int(arg, next_arg(argc, argv, index, arg));
            options.min_z = options.z;
            options.max_z = options.z;
        }
        else if (arg == "--min-z")
        {
            options.min_z = parse_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--max-z")
        {
            options.max_z = parse_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--center-x")
        {
            options.center_x = parse_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--center-y")
        {
            options.center_y = parse_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--height-scale")
        {
            options.height_scale = parse_positive_float(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--capture-ppm")
        {
            options.capture_ppm = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--capture-png")
        {
            options.capture_png = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--capture-sequence-dir")
        {
            options.capture_sequence_dir = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--capture-sequence-fps")
        {
            options.capture_sequence_fps =
                parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--debug-overlay")
        {
            options.debug_overlay = true;
        }
        else if (arg == "--developer-workspace")
        {
            options.developer_workspace = true;
            options.workspace_mode = WorkspaceMode::Developer;
        }
        else if (arg == "--view-mode")
        {
            options.view_mode = parse_view_mode(next_arg(argc, argv, index, arg));
            if (options.view_mode == ViewMode::Oblique25D)
            {
                throw std::invalid_argument("--view-mode oblique25d is not implemented yet");
            }
        }
        else if (arg == "--no-debug-overlay")
        {
            options.debug_overlay = false;
        }
        else if (arg == "--worker-count")
        {
            options.worker_count = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--tile-budget")
        {
            options.tile_budget = parse_positive_size(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--resident-tile-cap")
        {
            options.resident_tile_cap = parse_positive_size(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--max-outstanding-jobs")
        {
            options.max_outstanding_jobs =
                parse_positive_size(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--max-texture-uploads")
        {
            options.max_texture_uploads = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--max-mesh-uploads")
        {
            options.max_mesh_uploads = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--max-upload-bytes")
        {
            options.max_upload_bytes = parse_positive_size(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--simulate-slow-load-ms")
        {
            options.simulate_slow_load_ms = parse_int(arg, next_arg(argc, argv, index, arg));
            if (options.simulate_slow_load_ms < 0)
            {
                throw std::invalid_argument("--simulate-slow-load-ms must be non-negative");
            }
        }
        else if (arg == "--telemetry-tlog")
        {
            options.telemetry_tlog = std::string(next_arg(argc, argv, index, arg));
            options.telemetry = options.telemetry_tlog;
            options.telemetry_format = animus::telemetry_core::TelemetryImportFormat::Tlog;
        }
        else if (arg == "--telemetry")
        {
            options.telemetry = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--telemetry-live-udp")
        {
            parse_host_port(arg, next_arg(argc, argv, index, arg), options);
        }
        else if (arg == "--telemetry-live-buffer-s")
        {
            options.telemetry_live_buffer_s =
                parse_positive_double(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--telemetry-live-max-samples")
        {
            options.telemetry_live_max_samples =
                parse_positive_size(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--telemetry-live-render-max-points")
        {
            options.telemetry_live_render_max_points =
                parse_positive_size(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--telemetry-live-debug-csv")
        {
            options.telemetry_live_debug_csv = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--plan")
        {
            options.plan = std::string(next_arg(argc, argv, index, arg));
        }
        else if (arg == "--telemetry-format")
        {
            const std::string value = std::string(next_arg(argc, argv, index, arg));
            if (value == "tlog")
            {
                options.telemetry_format = animus::telemetry_core::TelemetryImportFormat::Tlog;
            }
            else if (value == "mcap")
            {
                options.telemetry_format =
                    animus::telemetry_core::TelemetryImportFormat::McapProtobuf;
            }
            else if (value == "hdf5")
            {
                options.telemetry_format =
                    animus::telemetry_core::TelemetryImportFormat::Hdf5Animus;
            }
            else
            {
                throw std::invalid_argument("--telemetry-format must be tlog, mcap, or hdf5");
            }
        }
        else if (arg == "--playback-rate")
        {
            options.playback_rate = parse_positive_float(arg, next_arg(argc, argv, index, arg));
        }
        else if (arg == "--playback-start-paused")
        {
            options.playback_start_paused = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout
                << "usage: animus [--pack-root PATH] [--z N] [--center-x N]\n"
                << "                   [--center-y N] [--frames N] [--width PX]\n"
                << "                   [--height PX] [--height-scale F] [--smoke]\n"
                << "                   [--capture-ppm PATH] [--capture-png PATH]\n"
                << "                   [--capture-sequence-dir DIR]\n"
                << "                   [--capture-sequence-fps FPS]\n"
                << "                   [--debug-overlay] [--developer-workspace]\n"
                << "                   [--view-mode terrain3d|map2d]\n"
                << "                   [--no-debug-overlay]\n"
                << "                   [--min-z N] [--max-z N] [--tile-budget N]\n"
                << "                   [--cache-root PATH] [--elevation-geotiff PATH]\n"
                << "                   [--bathymetry-geotiff PATH]\n"
                << "                   [--imagery-mbtiles PATH]\n"
                << "                   [--remote-imagery-url URL_TEMPLATE]\n"
                << "                   [--overlay-geotiff PATH] [--overlay-opacity F]\n"
                << "                   [--overlay PATH] [--geoid-grid PATH]\n"
                << "                   [--config PATH] [--no-load-config]\n"
                << "                   [--telemetry PATH] [--telemetry-format tlog|mcap|hdf5]\n"
                << "                   [--telemetry-live-udp HOST:PORT]\n"
                << "                   [--telemetry-live-buffer-s SECONDS]\n"
                << "                   [--telemetry-live-max-samples N]\n"
                << "                   [--telemetry-live-render-max-points N]\n"
                << "                   [--telemetry-live-debug-csv PATH]\n"
                << "                   [--plan PATH]\n"
                << "                   [--telemetry-tlog PATH] [--playback-rate F]\n"
                << "                   [--playback-start-paused]\n";
            std::exit(0);
        }
        else
        {
            throw std::invalid_argument("Unknown argument: " + std::string(arg));
        }
    }

    if (options.smoke && options.frames == 0)
    {
        options.frames = 1;
    }
    if (options.min_z > options.max_z)
    {
        throw std::invalid_argument("--min-z must be <= --max-z");
    }
    if (!options.telemetry.empty() && options.telemetry_live_udp_enabled)
    {
        throw std::invalid_argument("--telemetry and --telemetry-live-udp are mutually exclusive");
    }
    upsert_compat_overlay(options);
    sort_overlays(options);
    return options;
}

void apply_app_config_to_options(Options &options, const AppConfig &config)
{
    if (valid_workspace(config.workspace_mode))
    {
        options.workspace_mode = parse_workspace_mode(config.workspace_mode);
    }
    else
    {
        options.config_diagnostics.push_back("invalid config value for app.workspace: " +
                                             config.workspace_mode);
    }
    if (valid_view_mode(config.view_mode))
    {
        options.view_mode = parse_view_mode(config.view_mode);
    }
    else
    {
        options.config_diagnostics.push_back("invalid config value for view.mode: " +
                                             config.view_mode);
    }
    if (valid_active_panel(config.active_panel))
    {
        options.active_panel = config.active_panel;
    }
    else
    {
        options.config_diagnostics.push_back("invalid config value for panels.active: " +
                                             config.active_panel);
    }
    if (valid_map_orientation(config.map_orientation))
    {
        options.map_orientation = config.map_orientation;
    }
    else
    {
        options.config_diagnostics.push_back("invalid config value for view.map_orientation: " +
                                             config.map_orientation);
    }

    options.width = config.window_width;
    options.height = config.window_height;
    options.follow_selected_entity = config.follow_selected;
    options.telemetry_tracks_visible = config.telemetry_tracks_visible;
    options.telemetry_labels_visible = config.telemetry_labels_visible;
    options.use_bathymetry = config.bathymetry_enabled;
    options.developer_diagnostics_visible = config.developer_diagnostics_visible;
    options.telemetry_diagnostics_visible = config.telemetry_diagnostics_visible;
    options.mavlink_inspector_visible = config.mavlink_inspector_visible;
    options.selected_entity_tail_points = config.selected_entity_tail_points;
    options.selected_vehicle_test = config.selected_vehicle_test;
    options.ghost_recent_baseline_path = config.ghost_recent_baseline_path;
    options.ghost_layer_visible = config.ghost_layer_visible;
    options.report_export_default_dir = config.report_export_default_dir;
    options.overlay_enabled = config.overlay_enabled;
    options.overlay_opacity = config.overlay_opacity;
    options.overlay_order = config.layers.geotiff_overlay_draw_order;
    options.layers = config.layers;
    options.layers.selected_entity_tail_points = options.selected_entity_tail_points;
    options.layers.track_tail_visible = options.telemetry_tracks_visible;
    options.layers.vehicle_labels_visible = options.telemetry_labels_visible;
    options.layers.bathymetry_visible = options.use_bathymetry;
    options.layers.geotiff_overlay_visible = options.overlay_enabled;
    options.layers.geotiff_overlay_opacity = options.overlay_opacity;
    options.status_thresholds = config.status_thresholds;
    options.vehicle_visuals = config.vehicle_visuals;
    options.workspace_layouts = config.workspace_layouts;
    options.overlays.clear();
    for (const AppConfigOverlay &overlay : config.overlays)
    {
        options.overlays.push_back({overlay.path,
                                    overlay.enabled,
                                    overlay.opacity,
                                    overlay.draw_order,
                                    {},
                                    overlay.cache_identity});
    }
    options.telemetry_live_udp_host = config.telemetry_live_udp_host;
    options.telemetry_live_udp_port = config.telemetry_live_udp_port;
    options.telemetry_live_buffer_s = config.telemetry_live_buffer_s;
    options.telemetry_live_max_samples = config.telemetry_live_max_samples;
    options.telemetry_live_render_max_points = config.telemetry_live_render_max_points;
    options.plots = config.plots;
}

AppConfig app_config_from_options(const Options &options)
{
    AppConfig config;
    config.workspace_mode = workspace_mode_config_value(options.workspace_mode);
    config.active_panel = options.active_panel;
    config.window_width = options.width;
    config.window_height = options.height;
    config.view_mode = view_mode_config_value(options.view_mode);
    config.follow_selected = options.follow_selected_entity;
    config.map_orientation = options.map_orientation;
    config.telemetry_tracks_visible = options.telemetry_tracks_visible;
    config.telemetry_labels_visible = options.telemetry_labels_visible;
    config.bathymetry_enabled = options.use_bathymetry;
    config.developer_diagnostics_visible = options.developer_diagnostics_visible;
    config.telemetry_diagnostics_visible = options.telemetry_diagnostics_visible;
    config.mavlink_inspector_visible = options.mavlink_inspector_visible;
    config.selected_entity_tail_points = options.selected_entity_tail_points;
    config.selected_vehicle_test = options.selected_vehicle_test;
    config.ghost_recent_baseline_path = options.ghost_recent_baseline_path;
    config.ghost_layer_visible = options.ghost_layer_visible;
    config.report_export_default_dir = options.report_export_default_dir;
    config.overlay_enabled = options.overlay_enabled;
    config.overlay_opacity = options.overlay_opacity;
    config.layers = options.layers;
    config.layers.selected_entity_tail_points = options.selected_entity_tail_points;
    config.layers.track_tail_visible = options.telemetry_tracks_visible;
    config.layers.vehicle_labels_visible = options.telemetry_labels_visible;
    config.layers.bathymetry_visible = options.use_bathymetry;
    config.layers.geotiff_overlay_visible = options.overlay_enabled;
    config.layers.geotiff_overlay_opacity = options.overlay_opacity;
    config.layers.geotiff_overlay_draw_order = options.overlay_order;
    for (const OverlayLayerConfig &overlay : options.overlays)
    {
        config.overlays.push_back({overlay.path,
                                   overlay.enabled,
                                   overlay.opacity,
                                   overlay.draw_order,
                                   overlay.cache_identity});
    }
    config.telemetry_live_udp_host = options.telemetry_live_udp_host;
    config.telemetry_live_udp_port = options.telemetry_live_udp_port;
    config.telemetry_live_buffer_s = options.telemetry_live_buffer_s;
    config.telemetry_live_max_samples = options.telemetry_live_max_samples;
    config.telemetry_live_render_max_points = options.telemetry_live_render_max_points;
    config.status_thresholds = options.status_thresholds;
    config.vehicle_visuals = options.vehicle_visuals;
    config.plots = options.plots;
    config.workspace_layouts = options.workspace_layouts;
    return config;
}

AppConfigSaveResult save_app_config(Options &options)
{
    AppConfigSaveResult result;
    if (options.config_path.empty())
    {
        options.config_save_status = "save failed";
        options.config_diagnostics.push_back("config path is empty");
        return result;
    }
    result = save_app_config_file(options.config_path, app_config_from_options(options));
    options.config_save_status = result.saved ? "saved" : "save failed";
    options.config_dirty = !result.saved;
    options.config_diagnostics.insert(
        options.config_diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
    if (!result.saved)
    {
        for (const std::string &diagnostic : result.diagnostics)
        {
            std::cerr << diagnostic << '\n';
        }
    }
    return result;
}

} // namespace animus::app
