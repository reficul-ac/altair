#include "options.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
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

std::string_view next_arg(int argc, char **argv, int &index, std::string_view option)
{
    if (++index >= argc)
    {
        throw std::invalid_argument("Missing value for " + std::string(option));
    }
    return argv[index];
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
    if (key_pos == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
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
    if (key_pos == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
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
    if (key_pos == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t colon = text.find(':', key_pos + key.size());
    if (colon == std::string::npos)
    {
        return std::nullopt;
    }
    const std::string_view value(text.data() + colon + 1U, text.size() - colon - 1U);
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return std::nullopt;
    }
    if (value.substr(first, 4) == "true")
    {
        return true;
    }
    if (value.substr(first, 5) == "false")
    {
        return false;
    }
    return std::nullopt;
}

std::vector<OverlayLayerConfig> json_overlay_layers(const std::string &text)
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

    std::vector<OverlayLayerConfig> layers;
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
        OverlayLayerConfig layer;
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

void load_app_config(Options &options)
{
    if (!options.load_config || options.config_path.empty() ||
        !std::filesystem::exists(options.config_path))
    {
        return;
    }
    std::ifstream input(options.config_path);
    if (!input)
    {
        std::cerr << "failed to load config: " << options.config_path << '\n';
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (const auto value = json_string_field(text, "cache_root"))
    {
        options.cache_root = *value;
    }
    if (const auto value = json_string_field(text, "pack_root"))
    {
        options.pack_root = *value;
    }
    if (const auto value = json_string_field(text, "overlay_geotiff"))
    {
        options.overlay_geotiff = *value;
    }
    if (const auto value = json_string_field(text, "geoid_grid"))
    {
        options.geoid_grid = *value;
    }
    if (const auto value = json_float_field(text, "overlay_opacity"))
    {
        options.overlay_opacity = std::clamp(*value, 0.0F, 1.0F);
    }
    if (auto overlays = json_overlay_layers(text); !overlays.empty())
    {
        options.overlays = std::move(overlays);
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

Options parse_options(int argc, char **argv)
{
    Options options;

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
            std::cout << "usage: animus [--pack-root PATH] [--z N] [--center-x N]\n"
                      << "                   [--center-y N] [--frames N] [--width PX]\n"
                      << "                   [--height PX] [--height-scale F] [--smoke]\n"
                      << "                   [--capture-ppm PATH] [--capture-png PATH]\n"
                      << "                   [--capture-sequence-dir DIR]\n"
                      << "                   [--capture-sequence-fps FPS]\n"
                      << "                   [--debug-overlay] [--no-debug-overlay]\n"
                      << "                   [--min-z N] [--max-z N] [--tile-budget N]\n"
                      << "                   [--cache-root PATH] [--elevation-geotiff PATH]\n"
                      << "                   [--bathymetry-geotiff PATH]\n"
                      << "                   [--imagery-mbtiles PATH]\n"
                      << "                   [--remote-imagery-url URL_TEMPLATE]\n"
                      << "                   [--overlay-geotiff PATH] [--overlay-opacity F]\n"
                      << "                   [--overlay PATH] [--geoid-grid PATH]\n"
                      << "                   [--config PATH] [--no-load-config]\n"
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
    upsert_compat_overlay(options);
    sort_overlays(options);
    return options;
}

void save_app_config(const Options &options)
{
    if (options.config_path.empty())
    {
        return;
    }
    if (!options.config_path.parent_path().empty())
    {
        std::filesystem::create_directories(options.config_path.parent_path());
    }
    std::ofstream output(options.config_path);
    if (!output)
    {
        std::cerr << "failed to save config: " << options.config_path << '\n';
        return;
    }
    output << "{\n"
           << "  \"schema\": \"animus.app_config.v1\",\n"
           << "  \"pack_root\": \"" << options.pack_root.generic_string() << "\",\n"
           << "  \"cache_root\": \"" << options.cache_root.generic_string() << "\",\n"
           << "  \"overlay_geotiff\": \"" << options.overlay_geotiff.generic_string() << "\",\n"
           << "  \"overlay_opacity\": " << options.overlay_opacity << ",\n"
           << "  \"overlay_order\": " << options.overlay_order << ",\n"
           << "  \"geoid_grid\": \"" << options.geoid_grid.generic_string() << "\",\n"
           << "  \"overlays\": [\n";
    for (std::size_t index = 0; index < options.overlays.size(); ++index)
    {
        const OverlayLayerConfig &layer = options.overlays[index];
        output << "    {\"path\": \"" << layer.path.generic_string()
               << "\", \"enabled\": " << (layer.enabled ? "true" : "false")
               << ", \"opacity\": " << layer.opacity << ", \"draw_order\": " << layer.draw_order
               << ", \"cache_identity\": \"" << layer.cache_identity << "\"}";
        output << (index + 1U == options.overlays.size() ? "\n" : ",\n");
    }
    output << "  ],\n"
           << "  \"debug_overlay\": " << (options.debug_overlay ? "true" : "false") << ",\n"
           << "  \"capture_path\": \"" << options.capture_png.generic_string() << "\",\n"
           << "  \"capture_sequence_dir\": \"" << options.capture_sequence_dir.generic_string()
           << "\",\n"
           << "  \"capture_sequence_fps\": " << options.capture_sequence_fps << "\n"
           << "}\n";
}

} // namespace animus::app
