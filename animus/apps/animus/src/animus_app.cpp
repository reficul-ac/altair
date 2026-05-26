#include "animus/render_core/gl_info.hpp"
#include "animus/render_core/imgui_layer.hpp"
#include "animus/render_core/mesh.hpp"
#include "animus/render_core/render_stats.hpp"
#include "animus/render_core/shader_program.hpp"
#include "animus/render_core/texture.hpp"
#include "animus/render_core/window.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/terrain_core/terrain_data.hpp"
#include "animus/terrain_core/terrain_stream.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace
{

using animus::geo_core::TileCoord;
using animus::terrain_core::Raster;
using animus::terrain_core::TerrainStreamer;

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
    std::filesystem::path capture_ppm;
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

struct Vec3
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Mat4
{
    std::array<float, 16> data{};
};

struct Camera
{
    Vec3 target{0.0F, 3.45F, 0.0F};
    float distance = 4.2F;
    float yaw = -0.72F;
    float pitch = 0.72F;
};

struct InputState
{
    Camera camera;
    bool left_drag = false;
    bool middle_drag = false;
    bool was_reset_pressed = false;
    double last_x = 0.0;
    double last_y = 0.0;
    double pending_scroll_y = 0.0;
};

struct TerrainTileGpu
{
    TileCoord coord;
    animus::render_core::IndexedMesh mesh;
    animus::render_core::Texture2D imagery;
    animus::render_core::Texture2D height_texture;
    Raster heights;
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    std::size_t estimated_gpu_bytes = 0;

    TerrainTileGpu(TileCoord tile_coord,
                   animus::render_core::IndexedMesh tile_mesh,
                   animus::render_core::Texture2D imagery_texture,
                   animus::render_core::Texture2D height_values,
                   Raster height_raster,
                   float min_height,
                   float max_height,
                   std::size_t estimated_bytes)
        : coord(tile_coord), mesh(std::move(tile_mesh)), imagery(std::move(imagery_texture)),
          height_texture(std::move(height_values)), heights(std::move(height_raster)),
          min_height_m(min_height), max_height_m(max_height), estimated_gpu_bytes(estimated_bytes)
    {
    }
};

constexpr std::string_view vertex_shader = R"glsl(
#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 texcoord;

uniform mat4 mvp;

out vec2 uv;
out vec3 world_position;

void main()
{
    uv = texcoord;
    world_position = position;
    gl_Position = mvp * vec4(position, 1.0);
}
)glsl";

constexpr std::string_view fragment_shader = R"glsl(
#version 330 core
in vec2 uv;
in vec3 world_position;

uniform sampler2D imagery_tex;
uniform sampler2D height_tex;
uniform float height_scale;
uniform vec3 debug_tint;
uniform float debug_mix;

out vec4 color;

void main()
{
    vec3 imagery = texture(imagery_tex, uv).rgb;
    vec2 texel = 1.0 / vec2(textureSize(height_tex, 0));
    float h_l = texture(height_tex, uv + vec2(-texel.x, 0.0)).r * height_scale;
    float h_r = texture(height_tex, uv + vec2(texel.x, 0.0)).r * height_scale;
    float h_d = texture(height_tex, uv + vec2(0.0, -texel.y)).r * height_scale;
    float h_u = texture(height_tex, uv + vec2(0.0, texel.y)).r * height_scale;
    vec3 normal = normalize(vec3(h_l - h_r, 0.035, h_d - h_u));
    vec3 light_dir = normalize(vec3(-0.35, 0.75, -0.45));
    float shade = clamp(dot(normal, light_dir), 0.0, 1.0);
    float contour = smoothstep(0.485, 0.515, fract(world_position.y * 16.0));
    vec3 lit = imagery * (0.42 + 0.58 * shade);
    vec3 base = mix(lit, lit * 0.82, contour * 0.08);
    color = vec4(mix(base, debug_tint, debug_mix), 1.0);
}
)glsl";

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

Options parse_options(int argc, char **argv)
{
    Options options;

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
                      << "                   [--capture-ppm PATH] [--debug-overlay]\n"
                      << "                   [--no-debug-overlay]\n"
                      << "                   [--min-z N] [--max-z N] [--tile-budget N]\n"
                      << "                   [--cache-root PATH] [--elevation-geotiff PATH]\n"
                      << "                   [--bathymetry-geotiff PATH]\n"
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
    return options;
}

std::filesystem::path resolve_pack_root(const std::filesystem::path &requested)
{
    if (std::filesystem::exists(requested))
    {
        return requested;
    }
    if (requested.is_relative())
    {
        const std::filesystem::path source_relative =
            std::filesystem::path(ANIMUS_SOURCE_DIR).parent_path() / requested;
        if (std::filesystem::exists(source_relative))
        {
            return source_relative;
        }
    }
    throw std::runtime_error("Terrain pack root does not exist: " + requested.string());
}

std::vector<animus::render_core::TerrainVertex>
to_render_vertices(const animus::terrain_core::TerrainMeshCpu &mesh)
{
    std::vector<animus::render_core::TerrainVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const auto &vertex : mesh.vertices)
    {
        vertices.push_back({vertex.x, vertex.y, vertex.z, vertex.u, vertex.v});
    }
    return vertices;
}

Mat4 identity()
{
    Mat4 matrix;
    matrix.data = {
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
    };
    return matrix;
}

Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, float scale)
{
    return {a.x * scale, a.y * scale, a.z * scale};
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.0F)
    {
        return {};
    }
    return value * (1.0F / length);
}

Mat4 multiply(const Mat4 &a, const Mat4 &b)
{
    Mat4 result = {};
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int k = 0; k < 4; ++k)
            {
                result.data[static_cast<std::size_t>(col * 4 + row)] +=
                    a.data[static_cast<std::size_t>(k * 4 + row)] *
                    b.data[static_cast<std::size_t>(col * 4 + k)];
            }
        }
    }
    return result;
}

Mat4 perspective(float fov_y_radians, float aspect, float near_plane, float far_plane)
{
    const float f = 1.0F / std::tan(fov_y_radians * 0.5F);
    Mat4 matrix = {};
    matrix.data[0] = f / aspect;
    matrix.data[5] = f;
    matrix.data[10] = (far_plane + near_plane) / (near_plane - far_plane);
    matrix.data[11] = -1.0F;
    matrix.data[14] = (2.0F * far_plane * near_plane) / (near_plane - far_plane);
    return matrix;
}

Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up)
{
    const Vec3 forward = normalize(center - eye);
    const Vec3 side = normalize(cross(forward, up));
    const Vec3 up_corrected = cross(side, forward);

    Mat4 matrix = identity();
    matrix.data[0] = side.x;
    matrix.data[4] = side.y;
    matrix.data[8] = side.z;
    matrix.data[1] = up_corrected.x;
    matrix.data[5] = up_corrected.y;
    matrix.data[9] = up_corrected.z;
    matrix.data[2] = -forward.x;
    matrix.data[6] = -forward.y;
    matrix.data[10] = -forward.z;
    matrix.data[12] = -dot(side, eye);
    matrix.data[13] = -dot(up_corrected, eye);
    matrix.data[14] = dot(forward, eye);
    return matrix;
}

Vec3 camera_eye(const Camera &camera)
{
    const float cos_pitch = std::cos(camera.pitch);
    const Vec3 offset{
        camera.distance * cos_pitch * std::sin(camera.yaw),
        camera.distance * std::sin(camera.pitch),
        camera.distance * cos_pitch * std::cos(camera.yaw),
    };
    return camera.target + offset;
}

Mat4 camera_mvp(const Camera &camera, int width, int height)
{
    const float aspect =
        static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
    return multiply(perspective(45.0F * 3.1415926535F / 180.0F, aspect, 0.01F, 100.0F),
                    look_at(camera_eye(camera), camera.target, {0.0F, 1.0F, 0.0F}));
}

void update_camera(GLFWwindow *window,
                   InputState &input,
                   const bool camera_mouse_enabled,
                   const bool camera_keyboard_enabled)
{
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    const bool left = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool middle = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const double dx = x - input.last_x;
    const double dy = y - input.last_y;

    if (camera_mouse_enabled && middle && input.middle_drag)
    {
        input.camera.yaw -= static_cast<float>(dx) * 0.006F;
        input.camera.pitch =
            std::clamp(input.camera.pitch - static_cast<float>(dy) * 0.006F, 0.12F, 1.45F);
    }
    if (camera_mouse_enabled && left && input.left_drag)
    {
        const Vec3 eye = camera_eye(input.camera);
        const Vec3 forward = normalize(input.camera.target - eye);
        const Vec3 right = normalize(cross(forward, {0.0F, 1.0F, 0.0F}));
        const Vec3 up_plane = normalize(cross(right, forward));
        const float scale = input.camera.distance * 0.0015F;
        input.camera.target = input.camera.target + right * static_cast<float>(-dx * scale) +
                              up_plane * static_cast<float>(dy * scale);
    }

    if (camera_mouse_enabled && input.pending_scroll_y != 0.0)
    {
        const float zoom = std::pow(0.88F, static_cast<float>(input.pending_scroll_y));
        input.camera.distance = std::clamp(input.camera.distance * zoom, 0.45F, 40.0F);
    }
    input.pending_scroll_y = 0.0;

    input.left_drag = camera_mouse_enabled && left;
    input.middle_drag = camera_mouse_enabled && middle;
    input.last_x = x;
    input.last_y = y;

    const bool reset_pressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (camera_keyboard_enabled && reset_pressed && !input.was_reset_pressed)
    {
        input.camera = Camera{};
    }
    input.was_reset_pressed = camera_keyboard_enabled && reset_pressed;
}

void scroll_callback(GLFWwindow *window, double, double yoffset)
{
    auto *input = static_cast<InputState *>(glfwGetWindowUserPointer(window));
    if (input == nullptr)
    {
        return;
    }
    input->pending_scroll_y += yoffset;
}

float zoom_scale_from_base(const int zoom, const int base_zoom)
{
    return std::ldexp(1.0F, zoom - base_zoom);
}

animus::terrain_core::TerrainMeshOptions mesh_options_for_tile(const Options &options,
                                                               const TileCoord coord)
{
    const float scale = zoom_scale_from_base(coord.z, options.z);
    const float tile_size = 1.0F / scale;
    return {
        static_cast<float>(coord.x) / scale - static_cast<float>(options.center_x),
        static_cast<float>(coord.y) / scale - static_cast<float>(options.center_y),
        tile_size,
        options.height_scale,
        180.0F * options.height_scale,
    };
}

animus::terrain_core::TerrainViewpoint
terrain_viewpoint(const Options &options, const Camera &camera, const int zoom)
{
    const double scale = static_cast<double>(zoom_scale_from_base(zoom, options.z));
    return {
        (static_cast<double>(options.center_x) + 0.5 + static_cast<double>(camera.target.x)) *
            scale,
        (static_cast<double>(options.center_y) + 0.5 + static_cast<double>(camera.target.z)) *
            scale,
        camera.distance,
    };
}

float request_priority(const animus::terrain_core::TerrainViewpoint &view, const TileCoord coord)
{
    const double tile_center_x = static_cast<double>(coord.x) + 0.5;
    const double tile_center_y = static_cast<double>(coord.y) + 0.5;
    const double dx = tile_center_x - view.center_x;
    const double dy = tile_center_y - view.center_y;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy));
}

bool contains_visible_tile(
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
    const TileCoord coord)
{
    return std::any_of(visible_tiles.begin(),
                       visible_tiles.end(),
                       [coord](const auto &decision) { return decision.coord == coord; });
}

std::vector<TileCoord> add_parent_fallback_requests(std::vector<TileCoord> desired, int min_zoom)
{
    std::vector<TileCoord> requests = desired;
    for (TileCoord coord : desired)
    {
        while (coord.z > min_zoom)
        {
            coord = animus::geo_core::parent(coord);
            requests.push_back(coord);
        }
    }
    std::sort(requests.begin(),
              requests.end(),
              [](TileCoord a, TileCoord b)
              {
                  if (a.z != b.z)
                  {
                      return a.z < b.z;
                  }
                  if (a.y != b.y)
                  {
                      return a.y < b.y;
                  }
                  return a.x < b.x;
              });
    requests.erase(std::unique(requests.begin(), requests.end()), requests.end());
    return requests;
}

std::vector<animus::terrain_core::TileLoadRequest>
build_load_requests(const Options &options,
                    const std::filesystem::path &pack_root,
                    const std::vector<TileCoord> &coords,
                    const Camera &camera,
                    std::uint64_t generation)
{
    std::vector<animus::terrain_core::TileLoadRequest> requests;
    requests.reserve(coords.size());
    for (const TileCoord coord : coords)
    {
        const auto view = terrain_viewpoint(options, camera, coord.z);
        animus::terrain_core::TileLoadRequest request;
        request.coord = coord;
        request.priority =
            request_priority(view, coord) + static_cast<float>(coord.z - options.min_z) * 0.01F;
        request.pack_root = pack_root;
        request.imagery_layer = "imagery";
        request.imagery_extension = "png";
        request.elevation_layer = "elevation";
        request.elevation_extension = "png";
        request.mesh_options = mesh_options_for_tile(options, coord);
        request.request_generation = generation;
        request.simulate_slow_load_ms = options.simulate_slow_load_ms;
        request.cache_root = options.cache_root;
        request.elevation_geotiff = options.elevation_geotiff;
        request.bathymetry_geotiff = options.bathymetry_geotiff;
        request.use_bathymetry = options.use_bathymetry;
        request.imagery_spec.resolution = 256;
        request.imagery_spec.min_zoom = options.min_z;
        request.imagery_spec.max_zoom = options.max_z;
        request.elevation_spec.resolution = 256;
        request.elevation_spec.min_zoom = options.min_z;
        request.elevation_spec.max_zoom = options.max_z;
        request.bathymetry_spec.resolution = 256;
        request.bathymetry_spec.min_zoom = options.min_z;
        request.bathymetry_spec.max_zoom = options.max_z;
        requests.push_back(std::move(request));
    }
    return requests;
}

TerrainTileGpu upload_tile(animus::terrain_core::PreparedTile prepared)
{
    const auto render_vertices = to_render_vertices(prepared.mesh);
    animus::render_core::IndexedMesh gpu_mesh(render_vertices, prepared.mesh.indices);
    animus::render_core::Texture2D imagery_texture;
    imagery_texture.upload_rgba8(
        prepared.imagery.width, prepared.imagery.height, prepared.imagery.byte_data);
    animus::render_core::Texture2D height_texture;
    height_texture.upload_r32f(
        prepared.heights.width, prepared.heights.height, prepared.heights.float_data);
    const std::size_t gpu_bytes =
        prepared.imagery.byte_data.size() + prepared.heights.float_data.size() * sizeof(float) +
        prepared.mesh.vertices.size() * sizeof(animus::render_core::TerrainVertex) +
        prepared.mesh.indices.size() * sizeof(std::uint32_t);
    return TerrainTileGpu(prepared.coord,
                          std::move(gpu_mesh),
                          std::move(imagery_texture),
                          std::move(height_texture),
                          std::move(prepared.heights),
                          prepared.min_height_m,
                          prepared.max_height_m,
                          gpu_bytes);
}

struct TelemetryPlaybackState
{
    animus::telemetry_core::Timeline timeline;
    animus::telemetry_core::PlaybackClock clock;
    bool loaded = false;
    bool terrain_height_unavailable = false;
    animus::telemetry_core::EntityId selected_entity;
};

struct ProjectedPoint
{
    bool visible = false;
    ImVec2 screen;
};

std::optional<float>
sample_resident_terrain_height_m(const Options &options,
                                 const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                                 const double lat_deg,
                                 const double lon_deg)
{
    const auto try_zoom = [&](const int zoom) -> std::optional<float>
    {
        TileCoord coord;
        try
        {
            coord = animus::geo_core::lat_lon_to_tile(lat_deg, lon_deg, zoom);
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }
        const auto it = tiles.find(coord);
        if (it == tiles.end())
        {
            return std::nullopt;
        }
        const auto uv = animus::geo_core::lat_lon_to_tile_uv(lat_deg, lon_deg, coord);
        const auto sample =
            animus::terrain_core::sample_float_raster_bilinear(it->second.heights, uv.u, uv.v);
        if (!sample.available)
        {
            return std::nullopt;
        }
        return sample.value;
    };

    if (const auto base = try_zoom(options.z))
    {
        return base;
    }
    for (const auto &[coord, tile] : tiles)
    {
        (void)tile;
        if (const auto value = try_zoom(coord.z))
        {
            return value;
        }
    }
    return std::nullopt;
}

Vec3 telemetry_world_position(const Options &options,
                              const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                              const animus::telemetry_core::TelemetrySample &sample,
                              bool &terrain_height_unavailable)
{
    const TileCoord coord =
        animus::geo_core::lat_lon_to_tile(sample.lat_deg, sample.lon_deg, options.z);
    const auto uv = animus::geo_core::lat_lon_to_tile_uv(sample.lat_deg, sample.lon_deg, coord);
    const float x = static_cast<float>(static_cast<double>(coord.x - options.center_x) + uv.u);
    const float z = static_cast<float>(static_cast<double>(coord.y - options.center_y) + uv.v);
    float y = 180.0F * options.height_scale;

    if (const auto terrain_m =
            sample_resident_terrain_height_m(options, tiles, sample.lat_deg, sample.lon_deg))
    {
        y = *terrain_m * options.height_scale;
        if (sample.altitude_relative_m)
        {
            y += static_cast<float>(*sample.altitude_relative_m) * options.height_scale;
        }
    }
    else
    {
        terrain_height_unavailable = true;
        if (sample.altitude_msl_m)
        {
            y = static_cast<float>(*sample.altitude_msl_m) * options.height_scale;
        }
    }

    return {x, y, z};
}

ProjectedPoint
project_to_screen(const Mat4 &mvp, const Vec3 world, const int width, const int height)
{
    const float x =
        mvp.data[0] * world.x + mvp.data[4] * world.y + mvp.data[8] * world.z + mvp.data[12];
    const float y =
        mvp.data[1] * world.x + mvp.data[5] * world.y + mvp.data[9] * world.z + mvp.data[13];
    const float w =
        mvp.data[3] * world.x + mvp.data[7] * world.y + mvp.data[11] * world.z + mvp.data[15];
    if (w <= 0.0F)
    {
        return {};
    }
    const float ndc_x = x / w;
    const float ndc_y = y / w;
    if (ndc_x < -1.2F || ndc_x > 1.2F || ndc_y < -1.2F || ndc_y > 1.2F)
    {
        return {};
    }
    return {true,
            ImVec2((ndc_x * 0.5F + 0.5F) * static_cast<float>(width),
                   (0.5F - ndc_y * 0.5F) * static_cast<float>(height))};
}

void draw_telemetry_overlay(const Options &options,
                            const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                            TelemetryPlaybackState &playback,
                            const Camera &camera,
                            const int framebuffer_width,
                            const int framebuffer_height)
{
    playback.terrain_height_unavailable = false;
    if (!playback.loaded || playback.timeline.entities.empty())
    {
        return;
    }
    const auto current =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!current)
    {
        return;
    }

    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const Mat4 mvp = camera_mvp(camera, framebuffer_width, framebuffer_height);
    const auto *track = playback.timeline.track_for(playback.selected_entity);
    if (track != nullptr && track->samples.size() >= 2U)
    {
        std::optional<ImVec2> previous;
        for (const auto &sample : track->samples)
        {
            bool unavailable = false;
            const Vec3 world = telemetry_world_position(options, tiles, sample, unavailable);
            const ProjectedPoint point =
                project_to_screen(mvp, world, framebuffer_width, framebuffer_height);
            if (point.visible && previous)
            {
                draw->AddLine(*previous, point.screen, IM_COL32(255, 214, 82, 210), 2.0F);
            }
            previous = point.visible ? std::optional<ImVec2>(point.screen) : std::nullopt;
        }
    }

    for (const auto &event : playback.timeline.events)
    {
        if (event.time_s > playback.clock.time_s())
        {
            break;
        }
        const auto event_sample = playback.timeline.sample_at(event.entity_id, event.time_s);
        if (!event_sample)
        {
            continue;
        }
        bool unavailable = false;
        const Vec3 event_world =
            telemetry_world_position(options, tiles, *event_sample, unavailable);
        const ProjectedPoint event_point =
            project_to_screen(mvp, event_world, framebuffer_width, framebuffer_height);
        if (event_point.visible)
        {
            draw->AddTriangleFilled(
                ImVec2(event_point.screen.x, event_point.screen.y - 10.0F),
                ImVec2(event_point.screen.x - 6.0F, event_point.screen.y + 2.0F),
                ImVec2(event_point.screen.x + 6.0F, event_point.screen.y + 2.0F),
                IM_COL32(96, 220, 255, 235));
        }
    }

    const Vec3 world =
        telemetry_world_position(options, tiles, *current, playback.terrain_height_unavailable);
    const ProjectedPoint point =
        project_to_screen(mvp, world, framebuffer_width, framebuffer_height);
    if (point.visible)
    {
        draw->AddCircleFilled(point.screen, 7.0F, IM_COL32(255, 80, 64, 255), 18);
        draw->AddCircle(point.screen, 11.0F, IM_COL32(255, 255, 255, 230), 18, 2.0F);
    }
}

void write_ppm_capture(const std::filesystem::path &path, int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        throw std::runtime_error("Failed to open capture path: " + path.string());
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = height - 1; row >= 0; --row)
    {
        const auto offset = static_cast<std::size_t>(row * width * 3);
        out.write(reinterpret_cast<const char *>(pixels.data() + offset), width * 3);
    }
}

void render_frame(const animus::render_core::GlfwWindow &window,
                  const animus::render_core::ShaderProgram &program,
                  const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                  const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                  const Camera &camera,
                  float height_scale,
                  bool state_colors,
                  bool highlight_fallback)
{
    const int framebuffer_width = window.framebuffer_width();
    const int framebuffer_height = window.framebuffer_height();
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(0.11F, 0.15F, 0.17F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Mat4 mvp = camera_mvp(camera, framebuffer_width, framebuffer_height);
    program.use();
    glUniformMatrix4fv(glGetUniformLocation(program.id(), "mvp"), 1, GL_FALSE, mvp.data.data());
    glUniform1i(glGetUniformLocation(program.id(), "imagery_tex"), 0);
    glUniform1i(glGetUniformLocation(program.id(), "height_tex"), 1);
    glUniform1f(glGetUniformLocation(program.id(), "height_scale"), height_scale);

    for (const auto &decision : visible_tiles)
    {
        const auto it = tiles.find(decision.coord);
        if (it == tiles.end())
        {
            continue;
        }
        const TerrainTileGpu &tile = it->second;
        const std::array<float, 3> tint = decision.using_fallback
                                              ? std::array<float, 3>{1.0F, 0.82F, 0.18F}
                                              : std::array<float, 3>{0.10F, 0.65F, 1.0F};
        glUniform3fv(glGetUniformLocation(program.id(), "debug_tint"), 1, tint.data());
        glUniform1f(glGetUniformLocation(program.id(), "debug_mix"),
                    state_colors || (highlight_fallback && decision.using_fallback) ? 0.35F : 0.0F);
        tile.imagery.bind_to_unit(0);
        tile.height_texture.bind_to_unit(1);
        tile.mesh.draw();
    }
}

void draw_developer_workspace(
    const Options &options,
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
    bool &state_colors,
    bool &highlight_fallback)
{
    ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(680.0F, 520.0F), ImGuiCond_FirstUseEver);
    ImGui::Begin("Animus Workspace");

    if (ImGui::BeginTabBar("workspace_tabs"))
    {
        if (ImGui::BeginTabItem("Terrain"))
        {
            ImGui::Text("pack root");
            ImGui::TextWrapped("%s", pack_root.string().c_str());
            ImGui::Text("cache root");
            ImGui::TextWrapped("%s", options.cache_root.string().c_str());
            ImGui::Separator();
            ImGui::Text(
                "zoom range %d..%d selected %d", options.min_z, options.max_z, selected_zoom);
            ImGui::Text("center %d/%d height scale %.6f",
                        options.center_x,
                        options.center_y,
                        options.height_scale);
            ImGui::Text("bathymetry %s", options.use_bathymetry ? "enabled" : "disabled");
            ImGui::Text("elevation GeoTIFF %s",
                        options.elevation_geotiff.empty()
                            ? "not set"
                            : (std::filesystem::exists(options.elevation_geotiff) ? "available"
                                                                                  : "missing"));
            ImGui::Text("bathymetry GeoTIFF %s",
                        options.bathymetry_geotiff.empty()
                            ? "not set"
                            : (std::filesystem::exists(options.bathymetry_geotiff) ? "available"
                                                                                   : "missing"));
            ImGui::Separator();
            ImGui::Checkbox("State colors", &state_colors);
            ImGui::SameLine();
            ImGui::Checkbox("Fallback highlight", &highlight_fallback);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Render"))
        {
            ImGui::Text("camera target %.2f %.2f %.2f distance %.2f zoom %d",
                        camera.target.x,
                        camera.target.y,
                        camera.target.z,
                        camera.distance,
                        selected_zoom);
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
            ImGui::Separator();
            ImGui::Text("GL vendor %s", gl_info.vendor.c_str());
            ImGui::Text("GL renderer %s", gl_info.renderer.c_str());
            ImGui::Text("GL version %s", gl_info.version.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cache"))
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
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Tiles"))
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
            ImGui::EndTabItem();
        }

        if (playback.loaded && ImGui::BeginTabItem("Telemetry"))
        {
            ImGui::Text("file");
            ImGui::TextWrapped("%s", options.telemetry_tlog.string().c_str());
            ImGui::Text("entities %zu samples %zu events %zu",
                        playback.timeline.entities.size(),
                        playback.timeline.samples.size(),
                        playback.timeline.events.size());
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
            ImGui::Text("terrain height %s",
                        playback.terrain_height_unavailable ? "unavailable for some samples"
                                                            : "available");
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
            ImGui::EndTabItem();
        }

        if (playback.loaded && ImGui::BeginTabItem("Timeline"))
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
            ImGui::EndTabItem();
        }

        if (playback.loaded && ImGui::BeginTabItem("Entity"))
        {
            const auto sample =
                playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
            if (!playback.timeline.entities.empty())
            {
                int selected_index = 0;
                for (std::size_t index = 0U; index < playback.timeline.entities.size(); ++index)
                {
                    if (playback.timeline.entities[index].id == playback.selected_entity)
                    {
                        selected_index = static_cast<int>(index);
                    }
                }
                if (ImGui::BeginCombo("Entity",
                                      (std::to_string(playback.selected_entity.system_id) + ":" +
                                       std::to_string(playback.selected_entity.component_id))
                                          .c_str()))
                {
                    for (std::size_t index = 0U; index < playback.timeline.entities.size(); ++index)
                    {
                        const auto id = playback.timeline.entities[index].id;
                        const std::string label =
                            std::to_string(id.system_id) + ":" + std::to_string(id.component_id);
                        const bool selected = static_cast<int>(index) == selected_index;
                        if (ImGui::Selectable(label.c_str(), selected))
                        {
                            playback.selected_entity = id;
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            if (sample)
            {
                ImGui::Text("lat/lon %.7f %.7f", sample->lat_deg, sample->lon_deg);
                ImGui::Text("alt msl %s rel %s",
                            sample->altitude_msl_m ? std::to_string(*sample->altitude_msl_m).c_str()
                                                   : "n/a",
                            sample->altitude_relative_m
                                ? std::to_string(*sample->altitude_relative_m).c_str()
                                : "n/a");
                ImGui::Text("att roll %s pitch %s yaw %s",
                            sample->roll_rad ? std::to_string(*sample->roll_rad).c_str() : "n/a",
                            sample->pitch_rad ? std::to_string(*sample->pitch_rad).c_str() : "n/a",
                            sample->yaw_rad ? std::to_string(*sample->yaw_rad).c_str() : "n/a");
                ImGui::Text(
                    "speed %s heading %s",
                    sample->ground_speed_mps ? std::to_string(*sample->ground_speed_mps).c_str()
                                             : "n/a",
                    sample->heading_deg ? std::to_string(*sample->heading_deg).c_str() : "n/a");
                ImGui::Text("fields pos %s alt %s att %s vel %s hdg %s",
                            sample->fields.position ? "yes" : "no",
                            sample->fields.altitude_msl || sample->fields.altitude_relative ? "yes"
                                                                                            : "no",
                            sample->fields.attitude ? "yes" : "no",
                            sample->fields.velocity ? "yes" : "no",
                            sample->fields.heading ? "yes" : "no");
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

int run(const Options &options)
{
    const std::filesystem::path pack_root = resolve_pack_root(options.pack_root);
    TelemetryPlaybackState telemetry;
    if (!options.telemetry_tlog.empty())
    {
        telemetry.timeline = animus::telemetry_core::load_tlog(options.telemetry_tlog);
        telemetry.loaded = true;
        telemetry.clock.set_range(telemetry.timeline.start_time_s, telemetry.timeline.end_time_s);
        telemetry.clock.set_rate(options.playback_rate);
        telemetry.clock.set_paused(options.playback_start_paused);
        if (!telemetry.timeline.entities.empty())
        {
            telemetry.selected_entity = telemetry.timeline.entities.front().id;
        }
        std::cout << "Loaded telemetry tlog: " << options.telemetry_tlog << " entities "
                  << telemetry.timeline.entities.size() << " samples "
                  << telemetry.timeline.samples.size() << '\n';
    }

    animus::render_core::GlfwWindow window({
        options.width,
        options.height,
        "Animus",
        !options.smoke,
    });
    window.make_current();
    animus::render_core::initialize_glew();
    animus::render_core::enable_debug_callback();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    const auto info = animus::render_core::query_gl_info();
    std::cout << animus::render_core::format_gl_info(info) << '\n';

    InputState input;
    glfwSetWindowUserPointer(window.native_handle(), &input);
    glfwSetScrollCallback(window.native_handle(), scroll_callback);

    const animus::render_core::ShaderProgram program(vertex_shader, fragment_shader);
    TerrainStreamer streamer({
        options.min_z,
        options.max_z,
        options.tile_budget,
        options.resident_tile_cap,
        options.max_outstanding_jobs,
        options.worker_count,
        options.max_texture_uploads,
        options.max_mesh_uploads,
        options.max_upload_bytes,
        true,
    });
    std::unordered_map<TileCoord, TerrainTileGpu> tiles;
    constexpr std::uint64_t stream_generation = 1;
    std::vector<TileCoord> desired_tiles;
    std::vector<animus::terrain_core::TileRenderDecision> visible_tiles;
    bool state_colors = false;
    bool highlight_fallback = false;
    auto debug_layer =
        options.debug_overlay
            ? std::make_unique<animus::render_core::ImGuiLayer>(window.native_handle())
            : nullptr;
    animus::render_core::RenderStats stats;
    bool captured = false;

    while (!window.should_close())
    {
        window.poll_events();
        stats.frame_started();
        streamer.begin_frame();
        if (debug_layer != nullptr)
        {
            debug_layer->begin_frame();
        }
        telemetry.clock.advance(stats.last_frame_seconds());
        const bool camera_mouse_enabled =
            debug_layer == nullptr || !debug_layer->wants_mouse_capture();
        const bool camera_keyboard_enabled =
            debug_layer == nullptr || !debug_layer->wants_keyboard_capture();
        update_camera(window.native_handle(), input, camera_mouse_enabled, camera_keyboard_enabled);

        const int selected_zoom = animus::terrain_core::select_zoom_for_distance(
            input.camera.distance, options.min_z, options.max_z);
        const auto view = terrain_viewpoint(options, input.camera, selected_zoom);
        desired_tiles =
            animus::terrain_core::build_tile_wishlist(view, selected_zoom, options.tile_budget);
        const std::vector<TileCoord> requested_tiles =
            add_parent_fallback_requests(desired_tiles, options.min_z);
        streamer.request_tiles(build_load_requests(
            options, pack_root, requested_tiles, input.camera, stream_generation));

        const std::size_t max_tiles_by_textures =
            std::max<std::size_t>(1U, static_cast<std::size_t>(options.max_texture_uploads) / 2U);
        const std::size_t max_upload_tiles =
            std::min(static_cast<std::size_t>(options.max_mesh_uploads), max_tiles_by_textures);
        std::size_t upload_bytes_used = 0;
        int texture_uploads_used = 0;
        int mesh_uploads_used = 0;
        for (auto &prepared : streamer.drain_ready_cpu(max_upload_tiles, options.max_upload_bytes))
        {
            upload_bytes_used += prepared.estimated_cpu_bytes;
            streamer.mark_upload_queued(prepared.coord);
            TerrainTileGpu gpu_tile = upload_tile(std::move(prepared));
            texture_uploads_used += 2;
            mesh_uploads_used += 1;
            const TileCoord coord = gpu_tile.coord;
            tiles.insert_or_assign(coord, std::move(gpu_tile));
            streamer.mark_ready_gpu(coord);
        }
        for (const auto &failed : streamer.drain_failed())
        {
            std::cerr << "terrain tile failed " << animus::geo_core::tile_key(failed.coord) << ": "
                      << failed.error << '\n';
        }

        visible_tiles = streamer.choose_visible_tiles(desired_tiles);
        for (const auto &decision : visible_tiles)
        {
            streamer.mark_visible(decision.coord, decision.using_fallback);
        }

        for (auto it = tiles.begin();
             tiles.size() > options.resident_tile_cap && it != tiles.end();)
        {
            if (contains_visible_tile(visible_tiles, it->first))
            {
                ++it;
                continue;
            }
            streamer.mark_retiring(it->first);
            it = tiles.erase(it);
        }

        std::size_t resident_gpu_bytes = 0;
        for (const auto &[coord, tile] : tiles)
        {
            (void)coord;
            resident_gpu_bytes += tile.estimated_gpu_bytes;
        }
        streamer.update_l0_stats(tiles.size(), resident_gpu_bytes);

        render_frame(window,
                     program,
                     tiles,
                     visible_tiles,
                     input.camera,
                     options.height_scale,
                     state_colors,
                     debug_layer != nullptr && highlight_fallback);
        if (debug_layer != nullptr)
        {
            draw_telemetry_overlay(options,
                                   tiles,
                                   telemetry,
                                   input.camera,
                                   window.framebuffer_width(),
                                   window.framebuffer_height());
            draw_developer_workspace(options,
                                     pack_root,
                                     info,
                                     stats,
                                     streamer.snapshot(),
                                     input.camera,
                                     selected_zoom,
                                     visible_tiles,
                                     upload_bytes_used,
                                     texture_uploads_used,
                                     mesh_uploads_used,
                                     resident_gpu_bytes,
                                     telemetry,
                                     state_colors,
                                     highlight_fallback);
            debug_layer->end_frame();
        }
        const bool should_capture =
            !captured && !options.capture_ppm.empty() &&
            (options.frames == 0 || stats.frame_count() + 1 >= options.frames);
        if (should_capture)
        {
            write_ppm_capture(
                options.capture_ppm, window.framebuffer_width(), window.framebuffer_height());
            captured = true;
        }
        window.swap_buffers();
        stats.frame_finished();

        if (window.escape_pressed())
        {
            window.request_close();
        }
        if (options.frames > 0 && stats.frame_count() >= options.frames)
        {
            window.request_close();
        }
    }

    std::cout << "Rendered frames: " << stats.frame_count()
              << "\nLast frame seconds: " << stats.last_frame_seconds()
              << "\nTotal render seconds: " << stats.total_seconds() << '\n';
    return 0;
}

} // namespace

int animus_app_main(int argc, char **argv)
{
    return run(parse_options(argc, argv));
}
