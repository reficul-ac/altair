#include "capture.hpp"
#include "options.hpp"
#include "ui.hpp"

#include "animus/render_core/gl_info.hpp"
#include "animus/render_core/imgui_layer.hpp"
#include "animus/render_core/mesh.hpp"
#include "animus/render_core/render_stats.hpp"
#include "animus/render_core/shader_program.hpp"
#include "animus/render_core/texture.hpp"
#include "animus/render_core/window.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/telemetry_live/live_telemetry_buffer.hpp"
#include "animus/telemetry_live/trail_decimation.hpp"
#include "animus/telemetry_live/udp_mavlink_receiver.hpp"
#include "animus/terrain_core/datum.hpp"
#include "animus/terrain_core/terrain_data.hpp"
#include "animus/terrain_core/terrain_stream.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

using animus::app::Camera;
using animus::app::Mp4RecorderState;
using animus::app::Options;
using animus::app::ScreenshotToolState;
using animus::app::TelemetryPlaybackState;
using animus::app::UiState;
using animus::app::Vec3;
using animus::geo_core::TileCoord;
using animus::terrain_core::Raster;
using animus::terrain_core::TerrainStreamer;

struct Mat4
{
    std::array<float, 16> data{};
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
    struct OverlayTexture
    {
        int draw_order = 0;
        float opacity = 1.0F;
        std::string cache_identity;
        animus::render_core::Texture2D texture;
    };
    std::vector<OverlayTexture> overlay_textures;
    Raster heights;
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    std::size_t estimated_gpu_bytes = 0;

    TerrainTileGpu(TileCoord tile_coord,
                   animus::render_core::IndexedMesh tile_mesh,
                   animus::render_core::Texture2D imagery_texture,
                   animus::render_core::Texture2D height_values,
                   std::vector<OverlayTexture> overlay_values,
                   Raster height_raster,
                   float min_height,
                   float max_height,
                   std::size_t estimated_bytes)
        : coord(tile_coord), mesh(std::move(tile_mesh)), imagery(std::move(imagery_texture)),
          height_texture(std::move(height_values)), overlay_textures(std::move(overlay_values)),
          heights(std::move(height_raster)), min_height_m(min_height), max_height_m(max_height),
          estimated_gpu_bytes(estimated_bytes)
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

constexpr std::string_view overlay_fragment_shader = R"glsl(
#version 330 core
in vec2 uv;

uniform sampler2D overlay_tex;
uniform float opacity;

out vec4 color;

void main()
{
    vec4 sample = texture(overlay_tex, uv);
    color = vec4(sample.rgb, sample.a * opacity);
}
)glsl";

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
        request.imagery_mbtiles = options.imagery_mbtiles;
        request.remote_imagery_url_template = options.remote_imagery_url_template;
        request.remote_imagery_cache_identity = options.remote_imagery_cache_identity;
        request.remote_imagery_user_agent = options.remote_imagery_user_agent;
        request.remote_imagery_timeout_ms = options.remote_imagery_timeout_ms;
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

TerrainTileGpu upload_tile(const std::vector<animus::app::OverlayLayerConfig> &overlays,
                           animus::terrain_core::PreparedTile prepared)
{
    const auto render_vertices = to_render_vertices(prepared.mesh);
    animus::render_core::IndexedMesh gpu_mesh(render_vertices, prepared.mesh.indices);
    animus::render_core::Texture2D imagery_texture;
    imagery_texture.upload_rgba8(
        prepared.imagery.width, prepared.imagery.height, prepared.imagery.byte_data);
    animus::render_core::Texture2D height_texture;
    height_texture.upload_r32f(
        prepared.heights.width, prepared.heights.height, prepared.heights.float_data);
    std::vector<TerrainTileGpu::OverlayTexture> overlay_textures;
    for (const auto &layer : overlays)
    {
        if (!layer.enabled || layer.path.empty())
        {
            continue;
        }
        try
        {
            const Raster overlay = animus::terrain_core::GdalGeoTiffTileSource(layer.path)
                                       .load_tile_rgba(prepared.coord, prepared.imagery.width);
            animus::render_core::Texture2D texture;
            texture.upload_rgba8(overlay.width, overlay.height, overlay.byte_data);
            overlay_textures.push_back(TerrainTileGpu::OverlayTexture{
                layer.draw_order,
                layer.opacity,
                layer.cache_identity,
                std::move(texture),
            });
        }
        catch (const std::exception &error)
        {
            std::cerr << "overlay tile failed " << animus::geo_core::tile_key(prepared.coord) << " "
                      << layer.path << ": " << error.what() << '\n';
        }
    }
    std::stable_sort(
        overlay_textures.begin(),
        overlay_textures.end(),
        [](const TerrainTileGpu::OverlayTexture &a, const TerrainTileGpu::OverlayTexture &b)
        { return a.draw_order < b.draw_order; });
    const std::size_t gpu_bytes =
        prepared.imagery.byte_data.size() + prepared.heights.float_data.size() * sizeof(float) +
        prepared.mesh.vertices.size() * sizeof(animus::render_core::TerrainVertex) +
        prepared.mesh.indices.size() * sizeof(std::uint32_t);
    return TerrainTileGpu(prepared.coord,
                          std::move(gpu_mesh),
                          std::move(imagery_texture),
                          std::move(height_texture),
                          std::move(overlay_textures),
                          std::move(prepared.heights),
                          prepared.min_height_m,
                          prepared.max_height_m,
                          gpu_bytes);
}

struct ProjectedPoint
{
    bool visible = false;
    ImVec2 screen;
};

struct TelemetryOverlayDrawStats
{
    double draw_ms = 0.0;
    std::size_t rendered_trail_points = 0;
};

struct LiveDebugCsv
{
    std::ofstream output;

    explicit LiveDebugCsv(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return;
        }
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        output.open(path);
        if (!output)
        {
            throw std::runtime_error("failed to open live telemetry debug CSV: " + path.string());
        }
        output << "frame,wall_time_s,drained_datagrams,receiver_queue_before_drain,"
                  "receiver_dropped_datagrams,ingest_ms,prune_finalize_ms,snapshot_copy_ms,"
                  "overlay_draw_ms,retained_samples,rendered_trail_points,current_sample_time_s,"
                  "timeline_end_time_s,last_packet_age_s,receiver_queue_high_water,parsed_messages,"
                  "batch_messages,batch_samples\n";
    }

    [[nodiscard]] bool enabled() const
    {
        return output.is_open();
    }
};

double steady_time_s()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

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

animus::terrain_core::AltitudeReference
altitude_reference(animus::telemetry_core::AltitudeDatum datum)
{
    switch (datum)
    {
    case animus::telemetry_core::AltitudeDatum::MslOrthometric:
        return animus::terrain_core::AltitudeReference::MslOrthometric;
    case animus::telemetry_core::AltitudeDatum::Ellipsoid:
        return animus::terrain_core::AltitudeReference::Ellipsoid;
    case animus::telemetry_core::AltitudeDatum::TerrainRelative:
        return animus::terrain_core::AltitudeReference::TerrainRelative;
    case animus::telemetry_core::AltitudeDatum::Unknown:
        return animus::terrain_core::AltitudeReference::Unknown;
    }
    return animus::terrain_core::AltitudeReference::Unknown;
}

Vec3 telemetry_world_position(const Options &options,
                              const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                              const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                              const animus::telemetry_core::TelemetrySample &sample,
                              bool &terrain_height_unavailable,
                              bool &unknown_datum_relative_fallback,
                              bool &geoid_correction_unavailable)
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
            if (sample.altitude_datum == animus::telemetry_core::AltitudeDatum::Unknown)
            {
                unknown_datum_relative_fallback = true;
            }
            y += static_cast<float>(*sample.altitude_relative_m) * options.height_scale;
        }
        else if (sample.altitude_msl_m)
        {
            try
            {
                const auto above_terrain = animus::terrain_core::height_above_terrain_m(
                    altitude_reference(sample.altitude_datum),
                    *sample.altitude_msl_m,
                    *terrain_m,
                    sample.lat_deg,
                    sample.lon_deg,
                    geoid_grid);
                if (above_terrain)
                {
                    y += static_cast<float>(*above_terrain) * options.height_scale;
                }
                else if (sample.altitude_datum == animus::telemetry_core::AltitudeDatum::Ellipsoid)
                {
                    geoid_correction_unavailable = true;
                }
            }
            catch (const std::exception &)
            {
                geoid_correction_unavailable = true;
            }
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

TelemetryOverlayDrawStats
draw_telemetry_overlay(const Options &options,
                       const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                       const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                       TelemetryPlaybackState &playback,
                       const UiState &ui_state,
                       const Camera &camera,
                       const int framebuffer_width,
                       const int framebuffer_height)
{
    const double draw_start_s = steady_time_s();
    TelemetryOverlayDrawStats stats;
    playback.terrain_height_unavailable = false;
    playback.unknown_datum_relative_fallback = false;
    playback.geoid_correction_unavailable = false;
    if (!playback.loaded || playback.timeline.entities.empty())
    {
        return stats;
    }
    const auto current =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!current)
    {
        return stats;
    }

    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const Mat4 mvp = camera_mvp(camera, framebuffer_width, framebuffer_height);
    const auto *track = playback.timeline.track_for(playback.selected_entity);
    if (track != nullptr && track->samples.size() >= 2U)
    {
        const std::size_t max_points =
            playback.live ? options.telemetry_live_render_max_points : track->samples.size();
        const std::vector<std::size_t> trail_indices =
            animus::telemetry_live::decimated_trail_indices(track->samples.size(), max_points);
        stats.rendered_trail_points = trail_indices.size();
        std::optional<ImVec2> previous;
        for (const std::size_t sample_index : trail_indices)
        {
            const auto &sample = track->samples[sample_index];
            bool unavailable = false;
            bool unknown_datum = false;
            const Vec3 world = telemetry_world_position(options,
                                                        tiles,
                                                        geoid_grid,
                                                        sample,
                                                        unavailable,
                                                        unknown_datum,
                                                        playback.geoid_correction_unavailable);
            const ProjectedPoint point =
                project_to_screen(mvp, world, framebuffer_width, framebuffer_height);
            if (point.visible && previous)
            {
                draw->AddLine(*previous, point.screen, IM_COL32(255, 214, 82, 210), 2.0F);
            }
            previous = point.visible ? std::optional<ImVec2>(point.screen) : std::nullopt;
        }
    }

    if (!playback.live)
    {
        for (const auto &event : playback.timeline.events)
        {
            if (!animus::app::telemetry_event_visible(event, ui_state.telemetry_event_filters))
            {
                continue;
            }
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
            bool unknown_datum = false;
            const Vec3 event_world =
                telemetry_world_position(options,
                                         tiles,
                                         geoid_grid,
                                         *event_sample,
                                         unavailable,
                                         unknown_datum,
                                         playback.geoid_correction_unavailable);
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
    }

    const Vec3 world = telemetry_world_position(options,
                                                tiles,
                                                geoid_grid,
                                                *current,
                                                playback.terrain_height_unavailable,
                                                playback.unknown_datum_relative_fallback,
                                                playback.geoid_correction_unavailable);
    const ProjectedPoint point =
        project_to_screen(mvp, world, framebuffer_width, framebuffer_height);
    if (point.visible)
    {
        draw->AddCircleFilled(point.screen, 7.0F, IM_COL32(255, 80, 64, 255), 18);
        draw->AddCircle(point.screen, 11.0F, IM_COL32(255, 255, 255, 230), 18, 2.0F);
    }
    stats.draw_ms = (steady_time_s() - draw_start_s) * 1000.0;
    return stats;
}

void render_frame(const animus::render_core::GlfwWindow &window,
                  const animus::render_core::ShaderProgram &program,
                  const animus::render_core::ShaderProgram &overlay_program,
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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    overlay_program.use();
    glUniformMatrix4fv(
        glGetUniformLocation(overlay_program.id(), "mvp"), 1, GL_FALSE, mvp.data.data());
    glUniform1i(glGetUniformLocation(overlay_program.id(), "overlay_tex"), 0);
    std::vector<int> overlay_orders;
    for (const auto &[coord, tile] : tiles)
    {
        (void)coord;
        for (const auto &overlay : tile.overlay_textures)
        {
            overlay_orders.push_back(overlay.draw_order);
        }
    }
    std::sort(overlay_orders.begin(), overlay_orders.end());
    overlay_orders.erase(std::unique(overlay_orders.begin(), overlay_orders.end()),
                         overlay_orders.end());
    for (const int order : overlay_orders)
    {
        for (const auto &decision : visible_tiles)
        {
            const auto it = tiles.find(decision.coord);
            if (it == tiles.end())
            {
                continue;
            }
            for (const auto &overlay : it->second.overlay_textures)
            {
                if (overlay.draw_order != order)
                {
                    continue;
                }
                glUniform1f(glGetUniformLocation(overlay_program.id(), "opacity"), overlay.opacity);
                overlay.texture.bind_to_unit(0);
                it->second.mesh.draw();
            }
        }
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}

int run(const Options &options)
{
    const std::filesystem::path pack_root = resolve_pack_root(options.pack_root);
    const animus::terrain_core::GeoidCorrectionGrid geoid_grid(options.geoid_grid);
    TelemetryPlaybackState telemetry;
    if (!options.telemetry.empty())
    {
        telemetry.timeline =
            animus::telemetry_core::load_telemetry(options.telemetry, options.telemetry_format);
        telemetry.loaded = true;
        telemetry.clock.set_range(telemetry.timeline.start_time_s, telemetry.timeline.end_time_s);
        telemetry.clock.set_rate(options.playback_rate);
        telemetry.clock.set_paused(options.playback_start_paused);
        if (!telemetry.timeline.entities.empty())
        {
            telemetry.selected_entity = telemetry.timeline.entities.front().id;
        }
        std::cout << "Loaded telemetry "
                  << animus::telemetry_core::to_string(telemetry.timeline.source_format) << ": "
                  << options.telemetry << " entities " << telemetry.timeline.entities.size()
                  << " samples " << telemetry.timeline.samples.size() << " events "
                  << telemetry.timeline.events.size() << " skipped "
                  << telemetry.timeline.diagnostics.skipped_records << '\n';
    }
    std::unique_ptr<animus::telemetry_live::UdpMavlinkReceiver> live_receiver;
    std::unique_ptr<animus::telemetry_live::LiveTelemetryBuffer> live_buffer;
    if (options.telemetry_live_udp_enabled)
    {
        animus::telemetry_live::UdpMavlinkReceiverConfig receiver_config;
        receiver_config.bind_host = options.telemetry_live_udp_host;
        receiver_config.bind_port = options.telemetry_live_udp_port;
        live_receiver =
            std::make_unique<animus::telemetry_live::UdpMavlinkReceiver>(receiver_config);
        live_receiver->start();
        animus::telemetry_live::LiveTelemetryBufferConfig buffer_config;
        buffer_config.history_seconds = options.telemetry_live_buffer_s;
        buffer_config.max_samples = options.telemetry_live_max_samples;
        live_buffer = std::make_unique<animus::telemetry_live::LiveTelemetryBuffer>(buffer_config);
        telemetry.loaded = true;
        telemetry.live = true;
        telemetry.clock.set_paused(true);
        telemetry.live_endpoint = live_receiver->local_endpoint();
        std::cout << "Listening for live MAVLink UDP on " << telemetry.live_endpoint << '\n';
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
    const animus::render_core::ShaderProgram overlay_program(vertex_shader,
                                                             overlay_fragment_shader);
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
    bool overlay_enabled = options.overlay_enabled;
    float overlay_opacity = options.overlay_opacity;
    std::vector<animus::app::OverlayLayerConfig> active_overlays = options.overlays;
    for (auto &layer : active_overlays)
    {
        if (layer.path == options.overlay_geotiff)
        {
            layer.enabled = overlay_enabled;
            layer.opacity = overlay_opacity;
        }
    }
    auto debug_layer =
        options.debug_overlay
            ? std::make_unique<animus::render_core::ImGuiLayer>(window.native_handle())
            : nullptr;
    animus::render_core::RenderStats stats;
    bool captured = false;
    ScreenshotToolState screenshot_tool;
    Mp4RecorderState mp4_recorder;
    UiState ui_state;
    std::snprintf(screenshot_tool.png_path.data(),
                  screenshot_tool.png_path.size(),
                  "%s",
                  "artifacts/animus/screenshots/manual_screenshot.png");
    std::snprintf(mp4_recorder.mp4_path.data(),
                  mp4_recorder.mp4_path.size(),
                  "%s",
                  "artifacts/animus/videos/manual_recording.mp4");
    LiveDebugCsv live_debug_csv(options.telemetry_live_debug_csv);
    const double live_debug_start_s = steady_time_s();

    while (!window.should_close())
    {
        window.poll_events();
        stats.frame_started();
        active_overlays = options.overlays;
        for (auto &layer : active_overlays)
        {
            if (layer.path == options.overlay_geotiff)
            {
                layer.enabled = overlay_enabled;
                layer.opacity = overlay_opacity;
            }
        }
        streamer.begin_frame();
        if (debug_layer != nullptr)
        {
            debug_layer->begin_frame();
        }
        if (live_receiver && live_buffer)
        {
            const auto datagrams = live_receiver->drain();
            std::size_t batch_samples = 0U;
            telemetry.live_ingest_ms = 0.0;
            telemetry.live_prune_finalize_ms = 0.0;
            telemetry.live_snapshot_copy_ms = 0.0;
            telemetry.live_frame_batch_messages = 0U;
            telemetry.live_frame_batch_samples = 0U;
            if (!datagrams.empty())
            {
                live_buffer->ingest(datagrams);
                batch_samples = live_buffer->stats().last_batch_samples;
            }
            telemetry.receiver_stats = live_receiver->stats();
            telemetry.live_stats = live_buffer->stats();
            if (!datagrams.empty())
            {
                telemetry.live_ingest_ms = telemetry.live_stats.last_batch_ingest_ms;
                telemetry.live_prune_finalize_ms =
                    telemetry.live_stats.last_batch_prune_finalize_ms;
                telemetry.live_frame_batch_messages = telemetry.live_stats.last_batch_messages;
                telemetry.live_frame_batch_samples = telemetry.live_stats.last_batch_samples;
            }
            telemetry.live_snapshot_elapsed_s += stats.last_frame_seconds();
            const bool should_snapshot =
                batch_samples > 0U &&
                (telemetry.timeline.samples.empty() || telemetry.live_snapshot_elapsed_s >= 0.05);
            if (should_snapshot)
            {
                telemetry.live_snapshot_elapsed_s = 0.0;
                const double snapshot_start_s = steady_time_s();
                telemetry.timeline = live_buffer->timeline();
                telemetry.live_snapshot_copy_ms = (steady_time_s() - snapshot_start_s) * 1000.0;
                telemetry.clock.set_range(telemetry.timeline.start_time_s,
                                          telemetry.timeline.end_time_s);
                telemetry.clock.seek(telemetry.timeline.end_time_s);
                if (!telemetry.timeline.entities.empty())
                {
                    const bool selected_present =
                        std::any_of(telemetry.timeline.entities.begin(),
                                    telemetry.timeline.entities.end(),
                                    [&telemetry](const animus::telemetry_core::Entity &entity)
                                    { return entity.id == telemetry.selected_entity; });
                    if (!selected_present)
                    {
                        telemetry.selected_entity = telemetry.timeline.entities.front().id;
                    }
                }
            }
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
            TerrainTileGpu gpu_tile = upload_tile(active_overlays, std::move(prepared));
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
                     overlay_program,
                     tiles,
                     visible_tiles,
                     input.camera,
                     options.height_scale,
                     state_colors,
                     debug_layer != nullptr && highlight_fallback);
        if (debug_layer != nullptr)
        {
            const TelemetryOverlayDrawStats overlay_stats =
                draw_telemetry_overlay(options,
                                       tiles,
                                       geoid_grid,
                                       telemetry,
                                       ui_state,
                                       input.camera,
                                       window.framebuffer_width(),
                                       window.framebuffer_height());
            telemetry.live_overlay_draw_ms = overlay_stats.draw_ms;
            telemetry.live_rendered_trail_points = overlay_stats.rendered_trail_points;
            draw_app_workspace(options,
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
                               screenshot_tool,
                               mp4_recorder,
                               ui_state,
                               state_colors,
                               highlight_fallback,
                               overlay_enabled,
                               overlay_opacity);
            debug_layer->end_frame();
        }
        if (live_debug_csv.enabled() && telemetry.live)
        {
            const auto current =
                telemetry.timeline.sample_at(telemetry.selected_entity, telemetry.clock.time_s());
            live_debug_csv.output << stats.frame_count() << ','
                                  << (steady_time_s() - live_debug_start_s) << ','
                                  << telemetry.receiver_stats.last_drain_datagrams << ','
                                  << telemetry.receiver_stats.last_drain_queue_before << ','
                                  << telemetry.receiver_stats.dropped_datagrams << ','
                                  << telemetry.live_ingest_ms << ','
                                  << telemetry.live_prune_finalize_ms << ','
                                  << telemetry.live_snapshot_copy_ms << ','
                                  << telemetry.live_overlay_draw_ms << ','
                                  << telemetry.live_stats.retained_samples << ','
                                  << telemetry.live_rendered_trail_points << ','
                                  << (current ? current->time_s : 0.0) << ','
                                  << telemetry.timeline.end_time_s << ','
                                  << telemetry.receiver_stats.last_packet_age_s << ','
                                  << telemetry.receiver_stats.queue_high_water << ','
                                  << telemetry.live_stats.parsed_messages << ','
                                  << telemetry.live_frame_batch_messages << ','
                                  << telemetry.live_frame_batch_samples << '\n';
        }
        if (screenshot_tool.pending_png)
        {
            const std::filesystem::path path = screenshot_path(screenshot_tool);
            try
            {
                animus::app::write_png_capture(
                    path, window.framebuffer_width(), window.framebuffer_height());
                screenshot_tool.status = "saved " + path.string();
            }
            catch (const std::exception &error)
            {
                screenshot_tool.status = std::string("save failed: ") + error.what();
            }
            screenshot_tool.pending_png = false;
        }
        if (mp4_recorder.recording)
        {
            char name[64] = {};
            std::snprintf(name, sizeof(name), "frame_%06d.png", mp4_recorder.frame_count);
            try
            {
                animus::app::write_png_capture(mp4_recorder.sequence_dir / name,
                                               window.framebuffer_width(),
                                               window.framebuffer_height());
                ++mp4_recorder.frame_count;
            }
            catch (const std::exception &error)
            {
                mp4_recorder.recording = false;
                mp4_recorder.pending_stop = false;
                mp4_recorder.status = std::string("recording failed: ") + error.what();
            }
        }
        if (mp4_recorder.pending_stop)
        {
            try
            {
                finish_mp4_recording(mp4_recorder);
            }
            catch (const std::exception &error)
            {
                mp4_recorder.recording = false;
                mp4_recorder.pending_stop = false;
                mp4_recorder.status = std::string("encode failed: ") + error.what();
            }
        }
        const bool should_capture =
            !captured && (!options.capture_ppm.empty() || !options.capture_png.empty()) &&
            (options.frames == 0 || stats.frame_count() + 1 >= options.frames);
        if (should_capture)
        {
            if (!options.capture_ppm.empty())
            {
                animus::app::write_ppm_capture(
                    options.capture_ppm, window.framebuffer_width(), window.framebuffer_height());
            }
            if (!options.capture_png.empty())
            {
                animus::app::write_png_capture(
                    options.capture_png, window.framebuffer_width(), window.framebuffer_height());
            }
            captured = true;
        }
        if (!options.capture_sequence_dir.empty())
        {
            const int frame_number = stats.frame_count();
            char name[64] = {};
            std::snprintf(name, sizeof(name), "frame_%06d.png", frame_number);
            animus::app::write_png_capture(options.capture_sequence_dir / name,
                                           window.framebuffer_width(),
                                           window.framebuffer_height());
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

    if (mp4_recorder.recording && !mp4_recorder.pending_stop)
    {
        try
        {
            finish_mp4_recording(mp4_recorder);
            std::cout << "MP4 recording " << mp4_recorder.status << '\n';
        }
        catch (const std::exception &error)
        {
            std::cerr << "MP4 recording encode failed: " << error.what() << '\n';
        }
    }

    std::cout << "Rendered frames: " << stats.frame_count()
              << "\nLast frame seconds: " << stats.last_frame_seconds()
              << "\nTotal render seconds: " << stats.total_seconds() << '\n';
    animus::app::save_app_config(options);
    return 0;
}

} // namespace

int animus_app_main(int argc, char **argv)
{
    return run(animus::app::parse_options(argc, argv));
}
