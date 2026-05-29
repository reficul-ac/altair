#include "capture.hpp"
#include "forward_clearance.hpp"
#include "map_tools.hpp"
#include "options.hpp"
#include "ui.hpp"
#include "vehicle_visual_style.hpp"

#include "animus/render_core/gl_info.hpp"
#include "animus/render_core/imgui_layer.hpp"
#include "animus/render_core/mesh.hpp"
#include "animus/render_core/render_stats.hpp"
#include "animus/render_core/shader_program.hpp"
#include "animus/render_core/texture.hpp"
#include "animus/render_core/window.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_live/live_telemetry_buffer.hpp"
#include "animus/telemetry_live/trail_decimation.hpp"
#include "animus/telemetry_live/udp_mavlink_receiver.hpp"
#include "animus/terrain_core/datum.hpp"
#include "animus/terrain_core/terrain_data.hpp"
#include "animus/terrain_core/terrain_stream.hpp"
#include "animus/vehicle_core/vehicle_definition.hpp"
#include "animus/vehicle_core/vehicle_model.hpp"

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
using animus::app::Map2DCamera;
using animus::app::MapOrientationMode;
using animus::app::MapToolPoint;
using animus::app::MapToolState;
using animus::app::Mp4RecorderState;
using animus::app::Options;
using animus::app::PlanGeoPoint;
using animus::app::PlanVisualizationData;
using animus::app::PlanVisualizationLoadResult;
using animus::app::PlanVisualizationState;
using animus::app::ScreenshotToolState;
using animus::app::TelemetryPlaybackState;
using animus::app::ToolMode;
using animus::app::UiNavigationMode;
using animus::app::UiState;
using animus::app::Vec3;
using animus::app::VehicleRuntimeStatus;
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
    Map2DCamera map_camera;
    bool left_drag = false;
    bool middle_drag = false;
    bool was_reset_pressed = false;
    bool was_follow_pressed = false;
    bool was_space_pressed = false;
    bool was_escape_pressed = false;
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
    bool synthetic = false;
    std::size_t estimated_gpu_bytes = 0;

    TerrainTileGpu(TileCoord tile_coord,
                   animus::render_core::IndexedMesh tile_mesh,
                   animus::render_core::Texture2D imagery_texture,
                   animus::render_core::Texture2D height_values,
                   std::vector<OverlayTexture> overlay_values,
                   Raster height_raster,
                   float min_height,
                   float max_height,
                   bool synthetic_tile,
                   std::size_t estimated_bytes)
        : coord(tile_coord), mesh(std::move(tile_mesh)), imagery(std::move(imagery_texture)),
          height_texture(std::move(height_values)), overlay_textures(std::move(overlay_values)),
          heights(std::move(height_raster)), min_height_m(min_height), max_height_m(max_height),
          synthetic(synthetic_tile), estimated_gpu_bytes(estimated_bytes)
    {
    }
};

constexpr std::string_view vertex_shader = R"glsl(
#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 texcoord;

uniform mat4 mvp;
uniform float terrain_height_factor;

out vec2 uv;
out vec3 world_position;

void main()
{
    uv = texcoord;
    vec3 rendered_position = vec3(position.x, position.y * terrain_height_factor, position.z);
    world_position = rendered_position;
    gl_Position = mvp * vec4(rendered_position, 1.0);
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

constexpr std::string_view model_vertex_shader = R"glsl(
#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 view_projection;
uniform mat4 model;

out vec3 world_normal;

void main()
{
    vec4 world_position = model * vec4(position, 1.0);
    world_normal = mat3(model) * normal;
    gl_Position = view_projection * world_position;
}
)glsl";

constexpr std::string_view model_fragment_shader = R"glsl(
#version 330 core
in vec3 world_normal;

uniform vec4 base_color;

out vec4 color;

void main()
{
    vec3 normal = normalize(world_normal);
    vec3 light_dir = normalize(vec3(-0.42, 0.72, -0.32));
    float diffuse = clamp(dot(normal, light_dir), 0.0, 1.0);
    vec3 lit = base_color.rgb * (0.36 + 0.64 * diffuse);
    color = vec4(lit, base_color.a);
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

UiNavigationMode panel_mode_from_config_value(const std::string &value)
{
    if (value == "layers")
    {
        return UiNavigationMode::Layers;
    }
    if (value == "telemetry")
    {
        return UiNavigationMode::Telemetry;
    }
    if (value == "signals")
    {
        return UiNavigationMode::Signals;
    }
    if (value == "capture")
    {
        return UiNavigationMode::Capture;
    }
    if (value == "settings")
    {
        return UiNavigationMode::Settings;
    }
    if (value == "developer")
    {
        return UiNavigationMode::Developer;
    }
    return UiNavigationMode::View;
}

std::string panel_config_value(const UiNavigationMode mode)
{
    switch (mode)
    {
    case UiNavigationMode::View:
        return "view";
    case UiNavigationMode::Layers:
        return "layers";
    case UiNavigationMode::Telemetry:
        return "telemetry";
    case UiNavigationMode::Signals:
        return "signals";
    case UiNavigationMode::Capture:
        return "capture";
    case UiNavigationMode::Settings:
        return "settings";
    case UiNavigationMode::Developer:
        return "developer";
    }
    return "view";
}

std::string workspace_config_value(const animus::app::WorkspaceMode mode)
{
    switch (mode)
    {
    case animus::app::WorkspaceMode::FlyTest:
        return "fly_test";
    case animus::app::WorkspaceMode::Plan:
        return "plan";
    case animus::app::WorkspaceMode::Analyze:
        return "analyze";
    case animus::app::WorkspaceMode::Terrain:
        return "terrain";
    case animus::app::WorkspaceMode::Developer:
        return "developer";
    }
    return "fly_test";
}

MapOrientationMode map_orientation_from_config_value(const std::string &value)
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

std::string map_orientation_config_value(const MapOrientationMode mode)
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

void apply_options_to_ui(const Options &options, UiState &ui_state, Map2DCamera &map_camera)
{
    ui_state.workspace_mode = options.workspace_mode;
    ui_state.view_mode = options.view_mode;
    ui_state.active_mode = panel_mode_from_config_value(options.active_panel);
    ui_state.follow_selected_entity = options.follow_selected_entity;
    ui_state.telemetry_tracks_visible = options.telemetry_tracks_visible;
    ui_state.telemetry_labels_visible = options.telemetry_labels_visible;
    ui_state.bathymetry_enabled = options.use_bathymetry;
    ui_state.layers = options.layers;
    ui_state.developer_diagnostics_visible = options.developer_diagnostics_visible;
    ui_state.telemetry_diagnostics_visible = options.telemetry_diagnostics_visible;
    ui_state.mavlink_inspector_visible = options.mavlink_inspector_visible;
    ui_state.workspace_layouts = options.workspace_layouts;
    ui_state.workspace_layout_applied = false;
    map_camera.orientation = map_orientation_from_config_value(options.map_orientation);
}

void sync_options_from_ui(Options &options,
                          const UiState &ui_state,
                          const Map2DCamera &map_camera,
                          bool overlay_enabled,
                          float overlay_opacity)
{
    options.workspace_mode = ui_state.workspace_mode;
    options.view_mode = ui_state.view_mode;
    options.active_panel = panel_config_value(ui_state.active_mode);
    options.follow_selected_entity = ui_state.follow_selected_entity;
    options.map_orientation = map_orientation_config_value(map_camera.orientation);
    options.telemetry_tracks_visible = ui_state.telemetry_tracks_visible;
    options.telemetry_labels_visible = ui_state.telemetry_labels_visible;
    options.use_bathymetry = ui_state.bathymetry_enabled;
    options.layers = ui_state.layers;
    options.layers.track_tail_visible = ui_state.telemetry_tracks_visible;
    options.layers.vehicle_labels_visible = ui_state.telemetry_labels_visible;
    options.layers.bathymetry_visible = ui_state.bathymetry_enabled;
    options.layers.geotiff_overlay_visible = overlay_enabled;
    options.layers.geotiff_overlay_opacity = overlay_opacity;
    options.layers.tile_state_debug_visible = ui_state.layers.tile_state_debug_visible;
    options.layers.fallback_highlight_visible = ui_state.layers.fallback_highlight_visible;
    options.developer_diagnostics_visible = ui_state.developer_diagnostics_visible;
    options.telemetry_diagnostics_visible = ui_state.telemetry_diagnostics_visible;
    options.mavlink_inspector_visible = ui_state.mavlink_inspector_visible;
    options.workspace_layouts = ui_state.workspace_layouts;
    options.overlay_enabled = overlay_enabled;
    options.overlay_opacity = overlay_opacity;
    for (animus::app::OverlayLayerConfig &layer : options.overlays)
    {
        if (layer.path == options.overlay_geotiff)
        {
            layer.enabled = overlay_enabled;
            layer.opacity = overlay_opacity;
        }
    }
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

Mat4 translation(const Vec3 value)
{
    Mat4 matrix = identity();
    matrix.data[12] = value.x;
    matrix.data[13] = value.y;
    matrix.data[14] = value.z;
    return matrix;
}

Mat4 uniform_scale(const float scale)
{
    Mat4 matrix = identity();
    matrix.data[0] = scale;
    matrix.data[5] = scale;
    matrix.data[10] = scale;
    return matrix;
}

Mat4 rotation_x(const float radians)
{
    Mat4 matrix = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix.data[5] = c;
    matrix.data[6] = s;
    matrix.data[9] = -s;
    matrix.data[10] = c;
    return matrix;
}

Mat4 rotation_y(const float radians)
{
    Mat4 matrix = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix.data[0] = c;
    matrix.data[2] = -s;
    matrix.data[8] = s;
    matrix.data[10] = c;
    return matrix;
}

Mat4 rotation_z(const float radians)
{
    Mat4 matrix = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix.data[0] = c;
    matrix.data[1] = s;
    matrix.data[4] = -s;
    matrix.data[5] = c;
    return matrix;
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

Mat4 orthographic(const float left,
                  const float right,
                  const float bottom,
                  const float top,
                  const float near_plane,
                  const float far_plane)
{
    Mat4 matrix = identity();
    matrix.data[0] = 2.0F / (right - left);
    matrix.data[5] = 2.0F / (top - bottom);
    matrix.data[10] = -2.0F / (far_plane - near_plane);
    matrix.data[12] = -(right + left) / (right - left);
    matrix.data[13] = -(top + bottom) / (top - bottom);
    matrix.data[14] = -(far_plane + near_plane) / (far_plane - near_plane);
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

Vec3 map_up_vector(const Map2DCamera &camera)
{
    return {std::sin(camera.rotation_rad), 0.0F, -std::cos(camera.rotation_rad)};
}

Vec3 map_right_vector(const Map2DCamera &camera)
{
    return {std::cos(camera.rotation_rad), 0.0F, std::sin(camera.rotation_rad)};
}

Mat4 map2d_mvp(const Map2DCamera &camera, const int width, const int height)
{
    const float aspect =
        static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
    const float half_height = std::clamp(camera.distance, 0.25F, 80.0F);
    const Vec3 target{camera.target_x, 0.0F, camera.target_z};
    const Vec3 eye{camera.target_x, 32.0F, camera.target_z};
    return multiply(orthographic(-half_height * aspect,
                                 half_height * aspect,
                                 -half_height,
                                 half_height,
                                 -100.0F,
                                 100.0F),
                    look_at(eye, target, map_up_vector(camera)));
}

Mat4 active_view_projection(const Camera &camera,
                            const Map2DCamera &map_camera,
                            const animus::app::ViewMode view_mode,
                            const int width,
                            const int height)
{
    return view_mode == animus::app::ViewMode::Map2D ? map2d_mvp(map_camera, width, height)
                                                     : camera_mvp(camera, width, height);
}

bool update_camera(GLFWwindow *window,
                   InputState &input,
                   const animus::app::ViewMode view_mode,
                   const bool camera_mouse_enabled,
                   const bool camera_keyboard_enabled)
{
    bool target_panned = false;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    const bool left = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool middle = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const double dx = x - input.last_x;
    const double dy = y - input.last_y;

    if (view_mode == animus::app::ViewMode::Map2D)
    {
        if (camera_mouse_enabled && left && input.left_drag)
        {
            int framebuffer_width = 1;
            int framebuffer_height = 1;
            glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
            const float world_per_pixel = (2.0F * input.map_camera.distance) /
                                          static_cast<float>(std::max(1, framebuffer_height));
            (void)framebuffer_width;
            const Vec3 right = map_right_vector(input.map_camera);
            const Vec3 up = map_up_vector(input.map_camera);
            input.map_camera.target_x += right.x * static_cast<float>(-dx) * world_per_pixel +
                                         up.x * static_cast<float>(dy) * world_per_pixel;
            input.map_camera.target_z += right.z * static_cast<float>(-dx) * world_per_pixel +
                                         up.z * static_cast<float>(dy) * world_per_pixel;
            target_panned = true;
        }
        if (camera_mouse_enabled && middle && input.middle_drag &&
            input.map_camera.orientation == MapOrientationMode::FreeRotate)
        {
            input.map_camera.rotation_rad -= static_cast<float>(dx) * 0.006F;
        }
        if (camera_mouse_enabled && input.pending_scroll_y != 0.0)
        {
            const float zoom = std::pow(0.88F, static_cast<float>(input.pending_scroll_y));
            input.map_camera.distance = std::clamp(input.map_camera.distance * zoom, 0.35F, 80.0F);
        }
        input.pending_scroll_y = 0.0;
        input.left_drag = camera_mouse_enabled && left;
        input.middle_drag = camera_mouse_enabled && middle;
        input.last_x = x;
        input.last_y = y;

        const bool reset_pressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        if (camera_keyboard_enabled && reset_pressed && !input.was_reset_pressed)
        {
            input.map_camera = Map2DCamera{};
            target_panned = true;
        }
        input.was_reset_pressed = camera_keyboard_enabled && reset_pressed;
        return target_panned;
    }

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
        target_panned = true;
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
        target_panned = true;
    }
    input.was_reset_pressed = camera_keyboard_enabled && reset_pressed;
    return target_panned;
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

animus::terrain_core::TerrainViewpoint
terrain_viewpoint(const Options &options, const Map2DCamera &camera, const int zoom)
{
    const double scale = static_cast<double>(zoom_scale_from_base(zoom, options.z));
    return {
        (static_cast<double>(options.center_x) + 0.5 + static_cast<double>(camera.target_x)) *
            scale,
        (static_cast<double>(options.center_y) + 0.5 + static_cast<double>(camera.target_z)) *
            scale,
        camera.distance,
    };
}

animus::terrain_core::TerrainViewpoint
active_terrain_viewpoint(const Options &options,
                         const Camera &camera,
                         const Map2DCamera &map_camera,
                         const animus::app::ViewMode view_mode,
                         const int zoom)
{
    return view_mode == animus::app::ViewMode::Map2D ? terrain_viewpoint(options, map_camera, zoom)
                                                     : terrain_viewpoint(options, camera, zoom);
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
                    const animus::terrain_core::TerrainViewpoint &view,
                    std::uint64_t generation,
                    const bool use_bathymetry)
{
    std::vector<animus::terrain_core::TileLoadRequest> requests;
    requests.reserve(coords.size());
    for (const TileCoord coord : coords)
    {
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
        request.use_bathymetry = use_bathymetry;
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
                          prepared.synthetic,
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

struct VehicleRenderState
{
    struct LoadedModel
    {
        const animus::vehicle_core::VehicleDefinition *definition = nullptr;
        std::unique_ptr<animus::render_core::ModelMesh> mesh;
        std::string status = "not loaded";
        bool loaded = false;
    };

    animus::vehicle_core::VehicleRegistry registry;
    const animus::vehicle_core::VehicleDefinition *default_definition = nullptr;
    std::unordered_map<std::string, LoadedModel> models;
    VehicleRuntimeStatus status;
};

bool telemetry_sample_placeable(const animus::telemetry_core::TelemetrySample &sample)
{
    return sample.fields.position;
}

bool telemetry_sample_stale(const TelemetryPlaybackState &playback,
                            const animus::telemetry_core::TelemetrySample &sample)
{
    if (!playback.live)
    {
        return false;
    }
    return playback.receiver_stats.stale || (playback.timeline.end_time_s > sample.time_s &&
                                             playback.timeline.end_time_s - sample.time_s > 2.0);
}

std::optional<float> telemetry_heading_rad(const animus::telemetry_core::TelemetrySample &sample)
{
    if (sample.heading_deg)
    {
        return static_cast<float>(*sample.heading_deg * 3.1415926535 / 180.0);
    }
    if (sample.yaw_rad)
    {
        return static_cast<float>(*sample.yaw_rad);
    }
    return std::nullopt;
}

ImU32 im_color(const animus::app::VehicleVisualColor color)
{
    return IM_COL32(color.r, color.g, color.b, color.a);
}

ImVec2 rotated_icon_point(const ImVec2 center,
                          const float heading,
                          const float scale,
                          const float local_x,
                          const float local_y)
{
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return ImVec2(center.x + (local_x * c - local_y * s) * scale,
                  center.y + (local_x * s + local_y * c) * scale);
}

void draw_fixed_wing_icon(ImDrawList *draw,
                          const ImVec2 center,
                          const float heading,
                          const float scale,
                          const ImU32 fill,
                          const ImU32 stroke,
                          const float stroke_thickness)
{
    const std::array<ImVec2, 3> wing = {
        rotated_icon_point(center, heading, scale, 0.0F, -9.5F),
        rotated_icon_point(center, heading, scale, -9.0F, 2.0F),
        rotated_icon_point(center, heading, scale, 9.0F, 2.0F),
    };
    const std::array<ImVec2, 3> tail = {
        rotated_icon_point(center, heading, scale, 0.0F, 2.0F),
        rotated_icon_point(center, heading, scale, -4.5F, 7.5F),
        rotated_icon_point(center, heading, scale, 4.5F, 7.5F),
    };
    draw->AddTriangleFilled(wing[0], wing[1], wing[2], fill);
    draw->AddTriangleFilled(tail[0], tail[1], tail[2], fill);
    draw->AddPolyline(
        wing.data(), static_cast<int>(wing.size()), stroke, ImDrawFlags_Closed, stroke_thickness);
    draw->AddPolyline(
        tail.data(), static_cast<int>(tail.size()), stroke, ImDrawFlags_Closed, stroke_thickness);
    draw->AddLine(rotated_icon_point(center, heading, scale, 0.0F, -8.0F),
                  rotated_icon_point(center, heading, scale, 0.0F, 7.0F),
                  stroke,
                  stroke_thickness);
}

void draw_quadcopter_icon(ImDrawList *draw,
                          const ImVec2 center,
                          const float heading,
                          const float scale,
                          const ImU32 fill,
                          const ImU32 stroke,
                          const float stroke_thickness)
{
    constexpr std::array<std::pair<float, float>, 4> rotors = {
        std::pair{-6.5F, -6.5F},
        std::pair{6.5F, -6.5F},
        std::pair{6.5F, 6.5F},
        std::pair{-6.5F, 6.5F},
    };
    for (const auto &[x, y] : rotors)
    {
        draw->AddLine(
            center, rotated_icon_point(center, heading, scale, x, y), stroke, stroke_thickness);
    }
    for (const auto &[x, y] : rotors)
    {
        const ImVec2 rotor = rotated_icon_point(center, heading, scale, x, y);
        draw->AddCircleFilled(rotor, 2.9F * scale, fill, 14);
        draw->AddCircle(rotor, 3.3F * scale, stroke, 14, stroke_thickness);
    }
    draw->AddCircleFilled(center, 3.8F * scale, fill, 16);
    draw->AddCircle(center, 4.3F * scale, stroke, 16, stroke_thickness);
}

void draw_rover_icon(ImDrawList *draw,
                     const ImVec2 center,
                     const float heading,
                     const float scale,
                     const ImU32 fill,
                     const ImU32 stroke,
                     const float stroke_thickness)
{
    const std::array<ImVec2, 4> body = {
        rotated_icon_point(center, heading, scale, -6.5F, -7.5F),
        rotated_icon_point(center, heading, scale, 6.5F, -7.5F),
        rotated_icon_point(center, heading, scale, 6.5F, 7.5F),
        rotated_icon_point(center, heading, scale, -6.5F, 7.5F),
    };
    draw->AddConvexPolyFilled(body.data(), static_cast<int>(body.size()), fill);
    draw->AddPolyline(
        body.data(), static_cast<int>(body.size()), stroke, ImDrawFlags_Closed, stroke_thickness);
    constexpr std::array<std::pair<float, float>, 4> wheels = {
        std::pair{-8.0F, -4.5F},
        std::pair{8.0F, -4.5F},
        std::pair{-8.0F, 4.5F},
        std::pair{8.0F, 4.5F},
    };
    for (const auto &[x, y] : wheels)
    {
        draw->AddCircleFilled(
            rotated_icon_point(center, heading, scale, x, y), 2.1F * scale, stroke, 10);
    }
}

void draw_surface_boat_icon(ImDrawList *draw,
                            const ImVec2 center,
                            const float heading,
                            const float scale,
                            const ImU32 fill,
                            const ImU32 stroke,
                            const float stroke_thickness)
{
    const std::array<ImVec2, 4> hull = {
        rotated_icon_point(center, heading, scale, 0.0F, -10.0F),
        rotated_icon_point(center, heading, scale, 6.5F, 0.0F),
        rotated_icon_point(center, heading, scale, 0.0F, 8.0F),
        rotated_icon_point(center, heading, scale, -6.5F, 0.0F),
    };
    draw->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), fill);
    draw->AddPolyline(
        hull.data(), static_cast<int>(hull.size()), stroke, ImDrawFlags_Closed, stroke_thickness);
}

void draw_vehicle_visual_icon(ImDrawList *draw,
                              const animus::app::VehicleVisualStyle &style,
                              const animus::app::VehicleVisualVariant &variant,
                              const ImVec2 screen,
                              const std::optional<float> heading)
{
    const float heading_rad = heading.value_or(0.0F);
    const float scale = variant.scale;
    const ImU32 fill = im_color(variant.fill);
    const ImU32 stroke = im_color(variant.stroke);
    draw->AddCircleFilled(screen, 9.5F * scale, im_color(variant.shadow), 20);
    switch (style.icon_shape)
    {
    case animus::app::VehicleVisualIconShape::FixedWing:
        draw_fixed_wing_icon(
            draw, screen, heading_rad, scale, fill, stroke, variant.stroke_thickness);
        break;
    case animus::app::VehicleVisualIconShape::Quadcopter:
        draw_quadcopter_icon(
            draw, screen, heading_rad, scale, fill, stroke, variant.stroke_thickness);
        break;
    case animus::app::VehicleVisualIconShape::Rover:
        draw_rover_icon(draw, screen, heading_rad, scale, fill, stroke, variant.stroke_thickness);
        break;
    case animus::app::VehicleVisualIconShape::SurfaceBoat:
        draw_surface_boat_icon(
            draw, screen, heading_rad, scale, fill, stroke, variant.stroke_thickness);
        break;
    case animus::app::VehicleVisualIconShape::Circle:
        draw->AddCircleFilled(screen, 5.5F * scale, fill, 20);
        draw->AddCircle(screen, 6.0F * scale, stroke, 20, variant.stroke_thickness);
        break;
    }
}

std::vector<animus::render_core::ModelPrimitive>
to_render_model_primitives(const animus::vehicle_core::VehicleModelCpu &model)
{
    std::vector<animus::render_core::ModelPrimitive> primitives;
    primitives.reserve(model.primitives.size());
    for (const auto &primitive : model.primitives)
    {
        animus::render_core::ModelPrimitive out;
        out.base_color = primitive.base_color;
        out.vertices.reserve(primitive.vertices.size());
        for (const auto &vertex : primitive.vertices)
        {
            out.vertices.push_back({vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz});
        }
        out.indices = primitive.indices;
        primitives.push_back(std::move(out));
    }
    return primitives;
}

std::string
vehicle_diagnostic_text(const animus::vehicle_core::VehicleRegistryDiagnostic &diagnostic)
{
    return std::string(animus::vehicle_core::to_string(diagnostic.severity)) + ": " +
           diagnostic.package_path.string() + ": " + diagnostic.message;
}

VehicleRenderState load_vehicle_render_state()
{
    VehicleRenderState state;
    const std::filesystem::path vehicles_root =
        std::filesystem::path(ANIMUS_SOURCE_DIR) / "assets" / "vehicles";
    state.registry = animus::vehicle_core::VehicleRegistry::load_from_directory(vehicles_root);
    state.default_definition = state.registry.default_definition();
    state.status.registry_package_count = state.registry.package_count();
    for (const auto &diagnostic : state.registry.diagnostics())
    {
        state.status.diagnostics.push_back(vehicle_diagnostic_text(diagnostic));
    }

    if (state.default_definition == nullptr)
    {
        state.status.default_vehicle_id = animus::vehicle_core::VehicleRegistry::default_vehicle_id;
        state.status.model_status = "default descriptor missing";
        state.status.diagnostics.push_back("error: default vehicle descriptor missing");
        return state;
    }

    state.status.default_vehicle_id = state.default_definition->id;
    state.status.default_vehicle_name = state.default_definition->display_name;
    state.status.default_vehicle_type =
        std::string(animus::vehicle_core::to_string(state.default_definition->type));
    for (const auto &definition : state.registry.definitions())
    {
        state.status.definitions.push_back(
            {definition.id,
             definition.display_name,
             std::string(animus::vehicle_core::to_string(definition.type)),
             "not loaded",
             false});
    }
    return state;
}

VehicleRenderState::LoadedModel &ensure_vehicle_model_loaded(VehicleRenderState &state,
                                                             const std::string &vehicle_id)
{
    VehicleRenderState::LoadedModel &model = state.models[vehicle_id];
    if (model.definition != nullptr || model.status != "not loaded")
    {
        return model;
    }
    model.definition = state.registry.find(vehicle_id);
    if (model.definition == nullptr)
    {
        model.status = "descriptor missing";
        state.status.diagnostics.push_back("error: vehicle descriptor missing: " + vehicle_id);
        return model;
    }
    try
    {
        const animus::vehicle_core::VehicleModelCpu cpu_model =
            animus::vehicle_core::load_glb_model(model.definition->model_path);
        const auto render_primitives = to_render_model_primitives(cpu_model);
        model.mesh = std::make_unique<animus::render_core::ModelMesh>(render_primitives);
        model.loaded = true;
        model.status = "loaded";
    }
    catch (const std::exception &error)
    {
        model.loaded = false;
        model.status = "fallback icon";
        state.status.diagnostics.push_back(std::string("error: model load failed: ") +
                                           error.what());
    }
    return model;
}

void refresh_vehicle_runtime_status(VehicleRenderState &state,
                                    const animus::app::VehicleResolvedVisual &selected_visual)
{
    state.status.model_loaded = false;
    state.status.model_status = "fallback icon";
    if (state.default_definition != nullptr)
    {
        if (const auto model = state.models.find(state.default_definition->id);
            model != state.models.end())
        {
            state.status.model_loaded = model->second.loaded;
            state.status.model_status = model->second.status;
        }
    }
    for (auto &definition : state.status.definitions)
    {
        if (const auto model = state.models.find(definition.id); model != state.models.end())
        {
            definition.model_loaded = model->second.loaded;
            definition.model_status = model->second.status;
        }
    }
    state.status.selected_detected_type = selected_visual.detected_type;
    state.status.selected_vehicle_id = selected_visual.vehicle_id;
    state.status.selected_vehicle_name = selected_visual.vehicle_name;
    state.status.selected_vehicle_type = selected_visual.vehicle_type;
    state.status.selected_model_status = selected_visual.model_status;
    state.status.selected_fallback_reason = selected_visual.fallback_reason;
    state.status.selected_heading_source = selected_visual.heading_source;
    state.status.selected_altitude_placement = selected_visual.altitude_placement;
    state.status.selected_model_loaded = selected_visual.model_loaded;
    state.status.selected_force_icon_only = selected_visual.force_icon_only;
    state.status.selected_scale = selected_visual.scale;
}

std::pair<bool, std::string> vehicle_model_status(const VehicleRenderState &state,
                                                  const std::string &vehicle_id)
{
    if (const auto model = state.models.find(vehicle_id); model != state.models.end())
    {
        return {model->second.loaded, model->second.status};
    }
    return {false, "not loaded"};
}

Mat4 selected_vehicle_model_matrix(const animus::vehicle_core::VehicleDefinition &definition,
                                   const Vec3 world,
                                   const std::optional<float> heading)
{
    constexpr float deg_to_rad = 3.1415926535F / 180.0F;
    const Mat4 pose_rotation = rotation_y(heading.value_or(0.0F));
    const Mat4 descriptor_rotation =
        multiply(rotation_y(definition.orientation.yaw_deg * deg_to_rad),
                 multiply(rotation_x(definition.orientation.pitch_deg * deg_to_rad),
                          rotation_z(definition.orientation.roll_deg * deg_to_rad)));
    return multiply(translation(world),
                    multiply(pose_rotation,
                             multiply(descriptor_rotation, uniform_scale(definition.model_scale))));
}

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

struct ResidentTerrainSample
{
    float height_m = 0.0F;
    TelemetryPlaybackState::TerrainConfidence confidence =
        TelemetryPlaybackState::TerrainConfidence::Unavailable;
};

std::optional<ResidentTerrainSample>
sample_resident_terrain(const Options &options,
                        const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                        const double lat_deg,
                        const double lon_deg)
{
    const auto try_zoom = [&](const int zoom) -> std::optional<ResidentTerrainSample>
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
        ResidentTerrainSample result;
        result.height_m = sample.value;
        if (it->second.synthetic)
        {
            result.confidence = TelemetryPlaybackState::TerrainConfidence::SyntheticResidentTile;
        }
        else if (zoom == options.z)
        {
            result.confidence = TelemetryPlaybackState::TerrainConfidence::ExactResidentTile;
        }
        else
        {
            result.confidence = TelemetryPlaybackState::TerrainConfidence::FallbackResidentTile;
        }
        return result;
    };

    if (const auto base = try_zoom(options.z))
    {
        return base;
    }
    std::vector<int> resident_zooms;
    resident_zooms.reserve(tiles.size());
    for (const auto &[coord, tile] : tiles)
    {
        (void)tile;
        if (coord.z != options.z)
        {
            resident_zooms.push_back(coord.z);
        }
    }
    std::sort(resident_zooms.begin(), resident_zooms.end(), std::greater<int>{});
    resident_zooms.erase(std::unique(resident_zooms.begin(), resident_zooms.end()),
                         resident_zooms.end());
    for (const int zoom : resident_zooms)
    {
        if (const auto fallback = try_zoom(zoom))
        {
            return fallback;
        }
    }
    return std::nullopt;
}

Vec3 terrain_world_position(const Options &options,
                            const double lat_deg,
                            const double lon_deg,
                            const std::optional<float> terrain_m)
{
    const TileCoord coord = animus::geo_core::lat_lon_to_tile(lat_deg, lon_deg, options.z);
    const auto uv = animus::geo_core::lat_lon_to_tile_uv(lat_deg, lon_deg, coord);
    const float x = static_cast<float>(static_cast<double>(coord.x - options.center_x) + uv.u);
    const float z = static_cast<float>(static_cast<double>(coord.y - options.center_y) + uv.v);
    const float y = terrain_m ? *terrain_m * options.height_scale : 0.0F;
    return {x, y, z};
}

MapToolPoint make_map_tool_point(const Options &options,
                                 const double lat_deg,
                                 const double lon_deg,
                                 const std::optional<float> terrain_m,
                                 const std::string &label)
{
    const TileCoord coord = animus::geo_core::lat_lon_to_tile(lat_deg, lon_deg, options.z);
    const Vec3 world = terrain_world_position(options, lat_deg, lon_deg, terrain_m);
    MapToolPoint point;
    point.lat_deg = lat_deg;
    point.lon_deg = lon_deg;
    if (terrain_m)
    {
        point.terrain_elevation_m = *terrain_m;
    }
    point.tile_z = coord.z;
    point.tile_x = coord.x;
    point.tile_y = coord.y;
    point.world_x = world.x;
    point.world_y = world.y;
    point.world_z = world.z;
    point.label = label;
    return point;
}

std::optional<MapToolPoint>
map2d_point_from_screen(const Options &options,
                        const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                        const Map2DCamera &camera,
                        const ImVec2 screen,
                        const int framebuffer_width,
                        const int framebuffer_height)
{
    const float width = static_cast<float>(std::max(1, framebuffer_width));
    const float height = static_cast<float>(std::max(1, framebuffer_height));
    if (screen.x < 0.0F || screen.y < 0.0F || screen.x >= width || screen.y >= height)
    {
        return std::nullopt;
    }
    const float aspect = width / height;
    const float half_height = std::clamp(camera.distance, 0.25F, 80.0F);
    const float half_width = half_height * aspect;
    const Vec3 right = map_right_vector(camera);
    const Vec3 up = map_up_vector(camera);
    const float sx = (screen.x / width - 0.5F) * 2.0F * half_width;
    const float sy = (0.5F - screen.y / height) * 2.0F * half_height;
    const float world_x = camera.target_x + right.x * sx + up.x * sy;
    const float world_z = camera.target_z + right.z * sx + up.z * sy;
    const double global_x = static_cast<double>(options.center_x) + 0.5 + world_x;
    const double global_y = static_cast<double>(options.center_y) + 0.5 + world_z;
    const auto lat_lon = animus::geo_core::tile_space_to_lat_lon(global_x, global_y, options.z);
    return make_map_tool_point(
        options,
        lat_lon.u,
        lat_lon.v,
        sample_resident_terrain_height_m(options, tiles, lat_lon.u, lat_lon.v),
        "map");
}

std::optional<MapToolPoint>
terrain3d_point_from_screen(const Options &options,
                            const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                            const Camera &camera,
                            const ImVec2 screen,
                            const int framebuffer_width,
                            const int framebuffer_height)
{
    const float width = static_cast<float>(std::max(1, framebuffer_width));
    const float height = static_cast<float>(std::max(1, framebuffer_height));
    if (screen.x < 0.0F || screen.y < 0.0F || screen.x >= width || screen.y >= height)
    {
        return std::nullopt;
    }

    const float ndc_x = (screen.x / width) * 2.0F - 1.0F;
    const float ndc_y = 1.0F - (screen.y / height) * 2.0F;
    const Vec3 eye = camera_eye(camera);
    const Vec3 forward = normalize(camera.target - eye);
    const Vec3 right = normalize(cross(forward, {0.0F, 1.0F, 0.0F}));
    const Vec3 up = normalize(cross(right, forward));
    const float aspect = width / height;
    constexpr float tan_half_fov = 0.41421356237F; // tan(22.5 deg), matching camera_mvp().
    const Vec3 ray =
        normalize(forward + right * (ndc_x * aspect * tan_half_fov) + up * (ndc_y * tan_half_fov));

    auto terrain_delta = [&](const Vec3 world) -> std::optional<float>
    {
        const double global_x = static_cast<double>(options.center_x) + 0.5 + world.x;
        const double global_y = static_cast<double>(options.center_y) + 0.5 + world.z;
        const auto lat_lon = animus::geo_core::tile_space_to_lat_lon(global_x, global_y, options.z);
        const auto terrain_m =
            sample_resident_terrain_height_m(options, tiles, lat_lon.u, lat_lon.v);
        if (!terrain_m)
        {
            return std::nullopt;
        }
        return world.y - *terrain_m * options.height_scale;
    };

    std::optional<Vec3> previous_world;
    std::optional<float> previous_delta;
    constexpr int step_count = 192;
    const float max_distance = std::max(8.0F, camera.distance + 80.0F);
    for (int step = 0; step <= step_count; ++step)
    {
        const float t = max_distance * static_cast<float>(step) / static_cast<float>(step_count);
        const Vec3 world = eye + ray * t;
        const auto delta = terrain_delta(world);
        if (!delta)
        {
            previous_world.reset();
            previous_delta.reset();
            continue;
        }
        if (std::abs(*delta) < 0.002F)
        {
            const double global_x = static_cast<double>(options.center_x) + 0.5 + world.x;
            const double global_y = static_cast<double>(options.center_y) + 0.5 + world.z;
            const auto lat_lon =
                animus::geo_core::tile_space_to_lat_lon(global_x, global_y, options.z);
            return make_map_tool_point(
                options,
                lat_lon.u,
                lat_lon.v,
                sample_resident_terrain_height_m(options, tiles, lat_lon.u, lat_lon.v),
                "terrain");
        }
        if (previous_world && previous_delta &&
            ((*previous_delta > 0.0F && *delta <= 0.0F) ||
             (*previous_delta < 0.0F && *delta >= 0.0F)))
        {
            float lo = t - max_distance / static_cast<float>(step_count);
            float hi = t;
            for (int refine = 0; refine < 10; ++refine)
            {
                const float mid = (lo + hi) * 0.5F;
                const Vec3 mid_world = eye + ray * mid;
                const auto mid_delta = terrain_delta(mid_world);
                if (!mid_delta)
                {
                    break;
                }
                if ((*previous_delta > 0.0F && *mid_delta > 0.0F) ||
                    (*previous_delta < 0.0F && *mid_delta < 0.0F))
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
            const Vec3 hit = eye + ray * ((lo + hi) * 0.5F);
            const double global_x = static_cast<double>(options.center_x) + 0.5 + hit.x;
            const double global_y = static_cast<double>(options.center_y) + 0.5 + hit.z;
            const auto lat_lon =
                animus::geo_core::tile_space_to_lat_lon(global_x, global_y, options.z);
            return make_map_tool_point(
                options,
                lat_lon.u,
                lat_lon.v,
                sample_resident_terrain_height_m(options, tiles, lat_lon.u, lat_lon.v),
                "terrain");
        }
        previous_world = world;
        previous_delta = *delta;
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

std::optional<double>
selected_entity_clearance_m(const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                            const animus::telemetry_core::TelemetrySample &sample,
                            const double terrain_m,
                            bool &datum_uncertain)
{
    if (sample.altitude_relative_m)
    {
        if (sample.altitude_datum == animus::telemetry_core::AltitudeDatum::Unknown)
        {
            datum_uncertain = true;
        }
        return sample.altitude_relative_m;
    }
    if (!sample.altitude_msl_m)
    {
        return std::nullopt;
    }
    try
    {
        const auto clearance =
            animus::terrain_core::height_above_terrain_m(altitude_reference(sample.altitude_datum),
                                                         *sample.altitude_msl_m,
                                                         terrain_m,
                                                         sample.lat_deg,
                                                         sample.lon_deg,
                                                         geoid_grid);
        if (!clearance)
        {
            datum_uncertain = true;
        }
        return clearance;
    }
    catch (const std::exception &)
    {
        datum_uncertain = true;
        return std::nullopt;
    }
}

void update_selected_entity_terrain_state(
    const Options &options,
    const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
    const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
    const animus::app::AppConfigStatusThresholds &thresholds,
    TelemetryPlaybackState &playback,
    const UiState &ui_state)
{
    playback.selected_entity_terrain = {};
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return;
    }
    const auto sample =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!sample || !telemetry_sample_placeable(*sample))
    {
        return;
    }
    const auto terrain_sampler =
        [&](const double lat_deg,
            const double lon_deg) -> std::optional<animus::app::ForwardClearanceTerrainSample>
    {
        const auto resident_sample = sample_resident_terrain(options, tiles, lat_deg, lon_deg);
        if (!resident_sample)
        {
            return std::nullopt;
        }
        return animus::app::ForwardClearanceTerrainSample{
            resident_sample->height_m,
            resident_sample->confidence,
        };
    };
    const auto clearance_calculator =
        [&](const animus::telemetry_core::TelemetrySample &predicted_sample,
            const double terrain_elevation_m,
            bool &forward_datum_uncertain) -> std::optional<double>
    {
        return selected_entity_clearance_m(
            geoid_grid, predicted_sample, terrain_elevation_m, forward_datum_uncertain);
    };
    playback.selected_entity_terrain.forward_clearance =
        animus::app::build_forward_clearance_samples(
            *sample, terrain_sampler, clearance_calculator, thresholds);

    const auto terrain = sample_resident_terrain(options, tiles, sample->lat_deg, sample->lon_deg);
    if (!terrain)
    {
        playback.selected_entity_terrain.confidence =
            TelemetryPlaybackState::TerrainConfidence::Unavailable;
        return;
    }

    bool datum_uncertain = false;
    playback.selected_entity_terrain.terrain_elevation_m = terrain->height_m;
    playback.selected_entity_terrain.terrain_clearance_m =
        selected_entity_clearance_m(geoid_grid, *sample, terrain->height_m, datum_uncertain);
    playback.selected_entity_terrain.confidence =
        datum_uncertain ? TelemetryPlaybackState::TerrainConfidence::DatumUncertain
                        : terrain->confidence;
}

std::vector<animus::app::TerrainClearanceSample>
build_review_clearance_samples(const Options &options,
                               const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                               const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                               const animus::telemetry_core::Timeline &timeline,
                               const animus::telemetry_core::EntityId selected_entity)
{
    std::vector<animus::app::TerrainClearanceSample> samples;
    const auto *track = timeline.track_for(selected_entity);
    if (track == nullptr || track->samples.empty())
    {
        return samples;
    }
    constexpr std::size_t max_samples = animus::app::default_timeline_review_sample_cap;
    samples.reserve(std::min(track->samples.size(), max_samples));
    const std::size_t input_last = track->samples.size() - 1U;
    const std::size_t output_count = std::min(track->samples.size(), max_samples);
    const std::size_t output_last = output_count - 1U;
    for (std::size_t index = 0U; index < output_count; ++index)
    {
        const std::size_t source =
            output_last == 0U ? 0U : (index * input_last + output_last / 2U) / output_last;
        const auto &sample = track->samples[source];
        if (!telemetry_sample_placeable(sample))
        {
            continue;
        }
        const auto terrain =
            sample_resident_terrain(options, tiles, sample.lat_deg, sample.lon_deg);
        if (!terrain)
        {
            continue;
        }
        bool datum_uncertain = false;
        const auto clearance =
            selected_entity_clearance_m(geoid_grid, sample, terrain->height_m, datum_uncertain);
        if (clearance)
        {
            samples.push_back({sample.time_s, *clearance});
        }
    }
    return samples;
}

std::vector<animus::app::TerrainClearanceSample>
build_planned_path_clearance_samples(const Options &options,
                                     const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                                     const PlanVisualizationState &plan_state,
                                     const animus::telemetry_core::Timeline &timeline)
{
    std::vector<animus::app::TerrainClearanceSample> samples;
    if (!plan_state.data || plan_state.data->mission_waypoints.empty())
    {
        return samples;
    }

    const auto &waypoints = plan_state.data->mission_waypoints;
    samples.reserve(waypoints.size());
    const double duration_s = std::max(0.0, timeline.end_time_s - timeline.start_time_s);
    const std::size_t last_index = waypoints.size() - 1U;
    for (std::size_t index = 0U; index < waypoints.size(); ++index)
    {
        const PlanGeoPoint &point = waypoints[index].point;
        if (!point.alt_m)
        {
            continue;
        }
        const auto terrain = sample_resident_terrain(options, tiles, point.lat_deg, point.lon_deg);
        if (!terrain)
        {
            continue;
        }
        const double fraction =
            last_index == 0U ? 0.0 : static_cast<double>(index) / static_cast<double>(last_index);
        samples.push_back({timeline.start_time_s + duration_s * fraction,
                           *point.alt_m - terrain->height_m,
                           true});
    }
    return samples;
}

std::optional<Vec3>
selected_entity_world_position(const Options &options,
                               const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                               const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                               const TelemetryPlaybackState &playback,
                               const UiState &ui_state)
{
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return std::nullopt;
    }
    const auto sample =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!sample || !telemetry_sample_placeable(*sample))
    {
        return std::nullopt;
    }
    bool terrain_unavailable = false;
    bool unknown_datum = false;
    bool geoid_unavailable = false;
    return telemetry_world_position(
        options, tiles, geoid_grid, *sample, terrain_unavailable, unknown_datum, geoid_unavailable);
}

void center_selected_entity(const Options &options,
                            const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                            const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                            const TelemetryPlaybackState &playback,
                            const UiState &ui_state,
                            InputState &input)
{
    const auto world =
        selected_entity_world_position(options, tiles, geoid_grid, playback, ui_state);
    if (!world)
    {
        return;
    }
    if (ui_state.view_mode == animus::app::ViewMode::Map2D)
    {
        input.map_camera.target_x = world->x;
        input.map_camera.target_z = world->z;
    }
    else
    {
        input.camera.target = *world;
    }
}

void fit_selected_entity(const Options &options,
                         const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                         const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                         const TelemetryPlaybackState &playback,
                         const UiState &ui_state,
                         InputState &input)
{
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return;
    }
    const auto current =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!current || !telemetry_sample_placeable(*current))
    {
        return;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();
    bool any = false;
    const auto add_sample = [&](const animus::telemetry_core::TelemetrySample &sample)
    {
        if (!telemetry_sample_placeable(sample))
        {
            return;
        }
        bool terrain_unavailable = false;
        bool unknown_datum = false;
        bool geoid_unavailable = false;
        const Vec3 world = telemetry_world_position(options,
                                                    tiles,
                                                    geoid_grid,
                                                    sample,
                                                    terrain_unavailable,
                                                    unknown_datum,
                                                    geoid_unavailable);
        min_x = std::min(min_x, world.x);
        min_y = std::min(min_y, world.y);
        min_z = std::min(min_z, world.z);
        max_x = std::max(max_x, world.x);
        max_y = std::max(max_y, world.y);
        max_z = std::max(max_z, world.z);
        any = true;
    };

    const auto *track = playback.timeline.track_for(playback.selected_entity);
    const double start_time_s = current->time_s - 20.0;
    if (track != nullptr)
    {
        std::size_t accepted = 0U;
        for (auto it = track->samples.rbegin(); it != track->samples.rend() && accepted < 80U; ++it)
        {
            if (it->time_s > current->time_s)
            {
                continue;
            }
            if (it->time_s < start_time_s)
            {
                break;
            }
            add_sample(*it);
            ++accepted;
        }
    }
    add_sample(*current);
    if (!any)
    {
        return;
    }

    const Vec3 target{(min_x + max_x) * 0.5F, (min_y + max_y) * 0.5F, (min_z + max_z) * 0.5F};
    const float span_x = max_x - min_x;
    const float span_y = max_y - min_y;
    const float span_z = max_z - min_z;
    if (ui_state.view_mode == animus::app::ViewMode::Map2D)
    {
        input.map_camera.target_x = target.x;
        input.map_camera.target_z = target.z;
        input.map_camera.distance =
            std::clamp(std::max(span_x, span_z) * 0.9F + 0.7F, 0.35F, 80.0F);
    }
    else
    {
        input.camera.target = target;
        input.camera.distance =
            std::clamp(std::max({span_x, span_y, span_z}) * 1.4F + 1.2F, 0.45F, 40.0F);
    }
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

const animus::terrain_core::TileRuntimeState *
runtime_tile_for_point(const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                       const MapToolPoint &point)
{
    const TileCoord coord{point.tile_z, point.tile_x, point.tile_y};
    auto exact = std::find_if(snapshot.tiles.begin(),
                              snapshot.tiles.end(),
                              [coord](const auto &tile) { return tile.coord == coord; });
    if (exact != snapshot.tiles.end())
    {
        return &*exact;
    }
    return nullptr;
}

bool visible_tile_uses_fallback(
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
    const MapToolPoint &point)
{
    const TileCoord coord{point.tile_z, point.tile_x, point.tile_y};
    const auto it = std::find_if(visible_tiles.begin(),
                                 visible_tiles.end(),
                                 [coord](const auto &decision) { return decision.coord == coord; });
    return it != visible_tiles.end() && it->using_fallback;
}

std::string lat_lon_clipboard_text(const MapToolPoint &point, const bool include_elevation)
{
    char text[160] = {};
    if (include_elevation && point.terrain_elevation_m)
    {
        std::snprintf(text,
                      sizeof(text),
                      "%.7f, %.7f, %.2f m",
                      point.lat_deg,
                      point.lon_deg,
                      *point.terrain_elevation_m);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%.7f, %.7f", point.lat_deg, point.lon_deg);
    }
    return text;
}

std::optional<MapToolPoint>
selected_entity_tool_point(const Options &options,
                           const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                           const TelemetryPlaybackState &playback,
                           const UiState &ui_state)
{
    if (!playback.loaded || !ui_state.telemetry_entity_selected)
    {
        return std::nullopt;
    }
    const auto sample =
        playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
    if (!sample || !telemetry_sample_placeable(*sample))
    {
        return std::nullopt;
    }
    return make_map_tool_point(
        options,
        sample->lat_deg,
        sample->lon_deg,
        sample_resident_terrain_height_m(options, tiles, sample->lat_deg, sample->lon_deg),
        "selected");
}

void draw_tool_label(ImDrawList *draw, const ImVec2 at, const std::string &label, const ImU32 color)
{
    const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    const ImVec2 min(at.x + 10.0F, at.y - text_size.y - 10.0F);
    const ImVec2 max(min.x + text_size.x + 10.0F, min.y + text_size.y + 7.0F);
    draw->AddRectFilled(min, max, IM_COL32(12, 17, 20, 210), 5.0F);
    draw->AddRect(min, max, IM_COL32(230, 238, 242, 58), 5.0F);
    draw->AddText(ImVec2(min.x + 5.0F, min.y + 3.0F), color, label.c_str());
}

void draw_tool_point(ImDrawList *draw,
                     const Mat4 &mvp,
                     const MapToolPoint &point,
                     const int framebuffer_width,
                     const int framebuffer_height,
                     const ImU32 fill,
                     const ImU32 stroke)
{
    const ProjectedPoint projected =
        project_to_screen(mvp,
                          {point.world_x, point.world_y + 0.012F, point.world_z},
                          framebuffer_width,
                          framebuffer_height);
    if (!projected.visible)
    {
        return;
    }
    draw->AddCircleFilled(projected.screen, 5.5F, fill, 18);
    draw->AddCircle(projected.screen, 7.5F, stroke, 20, 1.4F);
    if (!point.label.empty())
    {
        draw_tool_label(draw, projected.screen, point.label, IM_COL32(232, 240, 244, 238));
    }
}

void draw_map_tools_overlay(const MapToolState &tools,
                            const Camera &camera,
                            const Map2DCamera &map_camera,
                            const animus::app::ViewMode view_mode,
                            const int framebuffer_width,
                            const int framebuffer_height)
{
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const Mat4 mvp = active_view_projection(
        camera, map_camera, view_mode, framebuffer_width, framebuffer_height);
    for (const auto &marker : tools.markers)
    {
        draw_tool_point(draw,
                        mvp,
                        marker,
                        framebuffer_width,
                        framebuffer_height,
                        IM_COL32(246, 194, 76, 236),
                        IM_COL32(255, 246, 194, 216));
    }
    for (const auto &bookmark : tools.bookmarks)
    {
        draw_tool_point(draw,
                        mvp,
                        bookmark,
                        framebuffer_width,
                        framebuffer_height,
                        IM_COL32(105, 192, 235, 230),
                        IM_COL32(206, 238, 252, 218));
    }
    if (tools.terrain_probe)
    {
        draw_tool_point(draw,
                        mvp,
                        *tools.terrain_probe,
                        framebuffer_width,
                        framebuffer_height,
                        IM_COL32(73, 211, 146, 238),
                        IM_COL32(198, 248, 224, 224));
    }
    if (tools.range_anchor && tools.range_endpoint)
    {
        const ProjectedPoint a = project_to_screen(mvp,
                                                   {tools.range_anchor->world_x,
                                                    tools.range_anchor->world_y + 0.018F,
                                                    tools.range_anchor->world_z},
                                                   framebuffer_width,
                                                   framebuffer_height);
        const ProjectedPoint b = project_to_screen(mvp,
                                                   {tools.range_endpoint->world_x,
                                                    tools.range_endpoint->world_y + 0.018F,
                                                    tools.range_endpoint->world_z},
                                                   framebuffer_width,
                                                   framebuffer_height);
        if (a.visible && b.visible)
        {
            draw->AddLine(a.screen, b.screen, IM_COL32(253, 225, 128, 238), 2.4F);
            draw->AddCircleFilled(a.screen, 5.0F, IM_COL32(253, 225, 128, 244), 18);
            draw->AddCircleFilled(b.screen, 5.0F, IM_COL32(253, 225, 128, 244), 18);
            const auto rb =
                animus::app::range_bearing_between(*tools.range_anchor, *tools.range_endpoint);
            char label[96] = {};
            std::snprintf(label,
                          sizeof(label),
                          rb.distance_m >= 1000.0 ? "%.2f km  %.0f deg" : "%.0f m  %.0f deg",
                          rb.distance_m >= 1000.0 ? rb.distance_m / 1000.0 : rb.distance_m,
                          rb.initial_bearing_deg);
            draw_tool_label(
                draw,
                ImVec2((a.screen.x + b.screen.x) * 0.5F, (a.screen.y + b.screen.y) * 0.5F),
                label,
                IM_COL32(253, 237, 171, 244));
        }
    }
    else if (tools.range_anchor)
    {
        draw_tool_point(draw,
                        mvp,
                        *tools.range_anchor,
                        framebuffer_width,
                        framebuffer_height,
                        IM_COL32(253, 225, 128, 244),
                        IM_COL32(255, 247, 194, 224));
    }
}

const char *imagery_source_label(const Options &options)
{
    if (!options.remote_imagery_url_template.empty())
    {
        return "remote_http";
    }
    if (!options.imagery_mbtiles.empty())
    {
        return "mbtiles";
    }
    return "local_xyz";
}

void draw_tile_source_inspector(
    const animus::terrain_core::TerrainStreamSnapshot &snapshot,
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
    const Options &options,
    const MapToolPoint &point)
{
    const auto *runtime = runtime_tile_for_point(snapshot, point);
    ImGui::SeparatorText("Tile / Source");
    ImGui::Text("tile %d/%d/%d", point.tile_z, point.tile_x, point.tile_y);
    ImGui::Text("visible fallback %s",
                visible_tile_uses_fallback(visible_tiles, point) ? "yes" : "no");
    if (runtime == nullptr)
    {
        ImGui::TextUnformatted("runtime state not resident");
        return;
    }
    ImGui::Text("imagery source %s", imagery_source_label(options));
    ImGui::Text("state %s", std::string(animus::terrain_core::to_string(runtime->state)).c_str());
    ImGui::Text("cache %s",
                std::string(animus::terrain_core::to_string(runtime->cache_tier)).c_str());
    ImGui::Text("elevation source %s",
                std::string(animus::terrain_core::to_string(runtime->source_type)).c_str());
    ImGui::Text(
        "synthetic %s depth %d", runtime->synthetic ? "yes" : "no", runtime->synthesis_depth);
    ImGui::Text("height %.1f..%.1f m", runtime->min_height_m, runtime->max_height_m);
    if (runtime->parent)
    {
        ImGui::Text("parent %d/%d/%d", runtime->parent->z, runtime->parent->x, runtime->parent->y);
    }
    if (!runtime->error.empty())
    {
        ImGui::TextWrapped("error %s", runtime->error.c_str());
    }
}

void draw_map_tool_popup(MapToolState &tools,
                         const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                         const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                         const Options &options,
                         const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                         const TelemetryPlaybackState &playback,
                         const UiState &ui_state,
                         InputState &input,
                         const MapToolPoint &clicked)
{
    if (!ImGui::BeginPopup("map_tool_context"))
    {
        return;
    }
    ImGui::Text("%.7f, %.7f", clicked.lat_deg, clicked.lon_deg);
    if (clicked.terrain_elevation_m)
    {
        ImGui::Text("elev %.2f m", *clicked.terrain_elevation_m);
    }
    else
    {
        ImGui::TextUnformatted("elev n/a");
    }
    if (ImGui::MenuItem("Copy lat/lon"))
    {
        ImGui::SetClipboardText(lat_lon_clipboard_text(clicked, false).c_str());
    }
    if (ImGui::MenuItem(
            "Copy lat/lon/elevation", nullptr, false, clicked.terrain_elevation_m.has_value()))
    {
        ImGui::SetClipboardText(lat_lon_clipboard_text(clicked, true).c_str());
    }
    if (ImGui::MenuItem("Place temporary marker"))
    {
        MapToolPoint point = clicked;
        point.label = "M" + std::to_string(tools.next_order);
        animus::app::push_bounded_point(tools.markers, point, tools.next_order);
    }
    if (ImGui::MenuItem("Add session bookmark"))
    {
        MapToolPoint point = clicked;
        point.label = "B" + std::to_string(tools.next_order);
        animus::app::push_bounded_point(tools.bookmarks, point, tools.next_order);
    }
    if (ImGui::MenuItem("Center camera here"))
    {
        input.map_camera.target_x = clicked.world_x;
        input.map_camera.target_z = clicked.world_z;
        input.camera.target = {clicked.world_x, clicked.world_y, clicked.world_z};
    }
    if (ImGui::MenuItem("Look at point",
                        nullptr,
                        false,
                        ui_state.view_mode == animus::app::ViewMode::Terrain3D))
    {
        input.camera.target = {clicked.world_x, clicked.world_y, clicked.world_z};
    }
    const auto selected_point = selected_entity_tool_point(options, tiles, playback, ui_state);
    if (ImGui::MenuItem("Measure from selected entity", nullptr, false, selected_point.has_value()))
    {
        tools.mode = ToolMode::RangeBearing;
        tools.range_anchor = *selected_point;
        tools.range_endpoint = clicked;
    }
    if (ImGui::MenuItem("Start range/bearing here"))
    {
        tools.mode = ToolMode::RangeBearing;
        tools.range_anchor = clicked;
        tools.range_endpoint.reset();
    }
    if (ImGui::MenuItem(
            "Terrain probe here", nullptr, false, clicked.terrain_elevation_m.has_value()))
    {
        tools.mode = ToolMode::TerrainProbe;
        tools.terrain_probe = clicked;
    }
    ImGui::MenuItem("Elevation profile", "deferred", false, false);
    ImGui::MenuItem("Clearance profile", "deferred", false, false);
    if (ImGui::MenuItem("Clear markers/bookmarks"))
    {
        tools.markers.clear();
        tools.bookmarks.clear();
    }
    draw_tile_source_inspector(snapshot, visible_tiles, options, clicked);
    ImGui::EndPopup();
}

ImU32 forward_clearance_color(const TelemetryPlaybackState::TerrainClearanceStatus status)
{
    switch (status)
    {
    case TelemetryPlaybackState::TerrainClearanceStatus::Ok:
        return IM_COL32(92, 221, 156, 232);
    case TelemetryPlaybackState::TerrainClearanceStatus::Caution:
        return IM_COL32(248, 181, 83, 238);
    case TelemetryPlaybackState::TerrainClearanceStatus::Warning:
        return IM_COL32(248, 91, 91, 244);
    case TelemetryPlaybackState::TerrainClearanceStatus::Unknown:
        return IM_COL32(150, 160, 166, 178);
    }
    return IM_COL32(150, 160, 166, 178);
}

void draw_segmented_circle(ImDrawList *draw,
                           const ImVec2 center,
                           const float radius,
                           const ImU32 color,
                           const int segment_count,
                           const bool dots)
{
    constexpr float two_pi = 6.28318530717958647692F;
    for (int segment = 0; segment < segment_count; ++segment)
    {
        const float a0 = two_pi * static_cast<float>(segment) / static_cast<float>(segment_count);
        if (dots)
        {
            draw->AddCircleFilled(
                ImVec2(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius),
                1.6F,
                color,
                8);
            continue;
        }
        const float a1 = a0 + two_pi * 0.55F / static_cast<float>(segment_count);
        draw->AddLine(ImVec2(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius),
                      ImVec2(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius),
                      color,
                      2.0F);
    }
}

void draw_selected_terrain_confidence_ring(
    ImDrawList *draw,
    const ImVec2 center,
    const TelemetryPlaybackState::TerrainConfidence confidence)
{
    constexpr float radius = 22.0F;
    switch (confidence)
    {
    case TelemetryPlaybackState::TerrainConfidence::ExactResidentTile:
        draw->AddCircle(center, radius, IM_COL32(92, 221, 156, 230), 42, 2.2F);
        break;
    case TelemetryPlaybackState::TerrainConfidence::FallbackResidentTile:
        draw_segmented_circle(draw, center, radius, IM_COL32(248, 181, 83, 230), 18, false);
        break;
    case TelemetryPlaybackState::TerrainConfidence::SyntheticResidentTile:
    case TelemetryPlaybackState::TerrainConfidence::DatumUncertain:
        draw_segmented_circle(draw, center, radius, IM_COL32(248, 181, 83, 230), 26, true);
        break;
    case TelemetryPlaybackState::TerrainConfidence::Unavailable:
        draw->AddCircle(center, radius, IM_COL32(150, 160, 166, 150), 42, 1.8F);
        break;
    }
}

void draw_forward_clearance_overlay(
    const Options &options,
    ImDrawList *draw,
    const Mat4 &mvp,
    const ImVec2 selected_screen,
    const std::vector<TelemetryPlaybackState::ForwardClearanceSample> &samples,
    const int framebuffer_width,
    const int framebuffer_height)
{
    if (samples.empty())
    {
        return;
    }
    std::vector<ImVec2> points;
    points.reserve(samples.size() + 1U);
    points.push_back(selected_screen);
    TelemetryPlaybackState::TerrainClearanceStatus worst_status =
        TelemetryPlaybackState::TerrainClearanceStatus::Ok;
    for (const auto &sample : samples)
    {
        const std::optional<float> terrain_m =
            sample.terrain_elevation_m
                ? std::optional<float>(static_cast<float>(*sample.terrain_elevation_m))
                : std::nullopt;
        const ProjectedPoint projected = project_to_screen(
            mvp,
            terrain_world_position(options, sample.lat_deg, sample.lon_deg, terrain_m),
            framebuffer_width,
            framebuffer_height);
        if (!projected.visible)
        {
            points.push_back(ImVec2(-100000.0F, -100000.0F));
            continue;
        }
        points.push_back(projected.screen);
        worst_status = animus::app::worst_terrain_clearance_status(worst_status, sample.status);
    }
    const ImU32 line_color = forward_clearance_color(worst_status);
    for (std::size_t index = 1U; index < points.size(); ++index)
    {
        if (points[index - 1U].x < -99999.0F || points[index].x < -99999.0F)
        {
            continue;
        }
        draw->AddLine(points[index - 1U], points[index], line_color, 2.0F);
    }
    for (std::size_t index = 0U; index < samples.size(); ++index)
    {
        const ImVec2 point = points[index + 1U];
        if (point.x < -99999.0F)
        {
            continue;
        }
        draw->AddCircleFilled(point, 4.0F, forward_clearance_color(samples[index].status), 16);
        draw->AddCircle(point, 6.0F, IM_COL32(12, 18, 22, 216), 18, 1.5F);
        char label[16]{};
        std::snprintf(label, sizeof(label), "%.0fs", samples[index].horizon_s);
        draw->AddText(ImVec2(point.x + 7.0F, point.y - 7.0F), IM_COL32(226, 232, 236, 224), label);
    }
}

TelemetryOverlayDrawStats
draw_telemetry_overlay(const Options &options,
                       const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                       const animus::terrain_core::GeoidCorrectionGrid &geoid_grid,
                       TelemetryPlaybackState &playback,
                       const VehicleRenderState &vehicle_render,
                       const UiState &ui_state,
                       const Camera &camera,
                       const Map2DCamera &map_camera,
                       const int framebuffer_width,
                       const int framebuffer_height,
                       const bool selected_model_visible)
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
        ui_state.telemetry_entity_selected
            ? playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s())
            : std::optional<animus::telemetry_core::TelemetrySample>{};
    const animus::app::VehicleVisualRegistry &visual_registry =
        animus::app::VehicleVisualRegistry::defaults();

    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const Mat4 mvp = active_view_projection(
        camera, map_camera, ui_state.view_mode, framebuffer_width, framebuffer_height);
    const auto *track = ui_state.telemetry_entity_selected
                            ? playback.timeline.track_for(playback.selected_entity)
                            : nullptr;
    if (ui_state.layers.track_tail_visible && current && telemetry_sample_placeable(*current) &&
        track != nullptr && track->samples.size() >= 2U)
    {
        const auto selected_entity =
            std::find_if(playback.timeline.entities.begin(),
                         playback.timeline.entities.end(),
                         [&](const animus::telemetry_core::Entity &entity)
                         { return entity.id == playback.selected_entity; });
        animus::app::VehicleResolvedVisual trail_visual;
        if (selected_entity != playback.timeline.entities.end())
        {
            trail_visual = animus::app::resolve_vehicle_visual(vehicle_render.registry,
                                                               options.vehicle_visuals,
                                                               *selected_entity,
                                                               false,
                                                               "not loaded");
        }
        const animus::app::VehicleVisualStyle &trail_style =
            selected_entity == playback.timeline.entities.end()
                ? visual_registry.default_style()
                : animus::app::resolve_entity_visual_style(visual_registry, trail_visual);
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
                draw->AddLine(*previous,
                              point.screen,
                              im_color(trail_style.trail_color),
                              trail_style.trail_thickness);
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
            if (!event_sample || !telemetry_sample_placeable(*event_sample))
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
                    ImVec2(event_point.screen.x, event_point.screen.y - 8.0F),
                    ImVec2(event_point.screen.x - 5.0F, event_point.screen.y + 2.0F),
                    ImVec2(event_point.screen.x + 5.0F, event_point.screen.y + 2.0F),
                    IM_COL32(116, 206, 232, 196));
            }
        }
    }

    const bool compact_labels = playback.timeline.entities.size() > 5U;
    std::size_t visible_labels = 0U;
    for (const auto &entity : playback.timeline.entities)
    {
        const auto sample = playback.timeline.sample_at(entity.id, playback.clock.time_s());
        if (!sample || !telemetry_sample_placeable(*sample))
        {
            continue;
        }
        bool terrain_unavailable = false;
        bool unknown_datum = false;
        bool geoid_unavailable = false;
        const Vec3 world = telemetry_world_position(options,
                                                    tiles,
                                                    geoid_grid,
                                                    *sample,
                                                    terrain_unavailable,
                                                    unknown_datum,
                                                    geoid_unavailable);
        playback.terrain_height_unavailable =
            playback.terrain_height_unavailable || terrain_unavailable;
        playback.unknown_datum_relative_fallback =
            playback.unknown_datum_relative_fallback || unknown_datum;
        playback.geoid_correction_unavailable =
            playback.geoid_correction_unavailable || geoid_unavailable;
        const ProjectedPoint point =
            project_to_screen(mvp, world, framebuffer_width, framebuffer_height);
        if (!point.visible)
        {
            continue;
        }

        const bool selected =
            ui_state.telemetry_entity_selected && entity.id == playback.selected_entity;
        const bool stale = telemetry_sample_stale(playback, *sample);
        const bool degraded = terrain_unavailable || geoid_unavailable;
        animus::app::VehicleResolvedVisual resolved_visual = animus::app::resolve_vehicle_visual(
            vehicle_render.registry, options.vehicle_visuals, entity, false, "not loaded");
        const auto model_status = vehicle_model_status(vehicle_render, resolved_visual.vehicle_id);
        resolved_visual = animus::app::resolve_vehicle_visual(vehicle_render.registry,
                                                              options.vehicle_visuals,
                                                              entity,
                                                              model_status.first,
                                                              model_status.second);
        const animus::app::VehicleVisualStyle &style =
            animus::app::resolve_entity_visual_style(visual_registry, resolved_visual);
        animus::app::VehicleVisualVariant variant =
            visual_registry.variant(style, {selected, stale, degraded});
        variant.scale *= resolved_visual.scale;
        const std::optional<float> heading = resolved_visual.heading_source == "none"
                                                 ? std::nullopt
                                                 : telemetry_heading_rad(*sample);
        const bool suppress_selected_fallback_marker = selected && selected_model_visible;
        if (ui_state.layers.vehicle_icons_visible && !suppress_selected_fallback_marker)
        {
            draw_vehicle_visual_icon(draw, style, variant, point.screen, heading);
            if (selected)
            {
                draw->AddCircle(point.screen, 13.5F, IM_COL32(118, 210, 255, 216), 28, 2.2F);
                draw->AddCircle(point.screen, 17.0F, IM_COL32(118, 210, 255, 68), 32, 3.0F);
                if (ui_state.view_mode == animus::app::ViewMode::Map2D &&
                    ui_state.layers.terrain_confidence_visible)
                {
                    draw_selected_terrain_confidence_ring(
                        draw, point.screen, playback.selected_entity_terrain.confidence);
                    draw_forward_clearance_overlay(
                        options,
                        draw,
                        mvp,
                        point.screen,
                        playback.selected_entity_terrain.forward_clearance,
                        framebuffer_width,
                        framebuffer_height);
                }
            }

            if (ui_state.layers.heading_vectors_visible && heading &&
                style.heading_indicator == animus::app::VehicleVisualHeadingIndicator::NoseLine)
            {
                const float length = selected ? 30.0F : 16.0F;
                const ImVec2 end(point.screen.x + std::sin(*heading) * length,
                                 point.screen.y - std::cos(*heading) * length);
                draw->AddLine(point.screen, end, im_color(variant.heading), selected ? 2.2F : 1.2F);
                if (selected)
                {
                    draw->AddCircleFilled(end, 2.6F, im_color(variant.heading), 10);
                }
            }
        }

        const bool draw_label = ui_state.layers.vehicle_labels_visible &&
                                (selected || !compact_labels || visible_labels < 3U || stale);
        if (!draw_label)
        {
            continue;
        }
        ++visible_labels;
        const std::string label = animus::app::vehicle_visual_label(style, entity.id, stale);
        const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        ImVec2 label_min(point.screen.x + 12.0F, point.screen.y - text_size.y - 9.0F);
        label_min.x = std::clamp(
            label_min.x, 8.0F, static_cast<float>(framebuffer_width) - text_size.x - 22.0F);
        label_min.y = std::clamp(
            label_min.y, 46.0F, static_cast<float>(framebuffer_height) - text_size.y - 12.0F);
        const ImVec2 label_max(label_min.x + text_size.x + 12.0F, label_min.y + text_size.y + 7.0F);
        draw->AddRectFilled(label_min, label_max, im_color(variant.label_background), 6.0F);
        draw->AddRect(label_min, label_max, im_color(variant.label_stroke), 6.0F, 0, 1.0F);
        draw->AddText(ImVec2(label_min.x + 6.0F, label_min.y + 3.0F),
                      im_color(variant.label_text),
                      label.c_str());
    }
    stats.draw_ms = (steady_time_s() - draw_start_s) * 1000.0;
    return stats;
}

Vec3 plan_world_position(const Options &options,
                         const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                         const PlanGeoPoint &point)
{
    const std::optional<float> terrain_m =
        sample_resident_terrain_height_m(options, tiles, point.lat_deg, point.lon_deg);
    Vec3 world = terrain_world_position(options, point.lat_deg, point.lon_deg, terrain_m);
    if (!terrain_m && point.alt_m)
    {
        world.y = static_cast<float>(*point.alt_m) * options.height_scale;
    }
    world.y += 0.025F;
    return world;
}

std::vector<PlanGeoPoint> circle_points(const PlanGeoPoint &center, const double radius_m)
{
    std::vector<PlanGeoPoint> points;
    points.reserve(65U);
    constexpr double pi = 3.14159265358979323846;
    constexpr double earth_radius_m = 6371008.8;
    const double lat_rad = center.lat_deg * pi / 180.0;
    const double dlat_deg = (radius_m / earth_radius_m) * 180.0 / pi;
    const double cos_lat = std::max(0.05, std::abs(std::cos(lat_rad)));
    const double dlon_deg = dlat_deg / cos_lat;
    for (int index = 0; index <= 64; ++index)
    {
        const double theta = 2.0 * pi * static_cast<double>(index) / 64.0;
        points.push_back({center.lat_deg + std::sin(theta) * dlat_deg,
                          center.lon_deg + std::cos(theta) * dlon_deg,
                          center.alt_m});
    }
    return points;
}

void draw_plan_polyline(ImDrawList *draw,
                        const Options &options,
                        const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                        const Mat4 &mvp,
                        const std::vector<PlanGeoPoint> &points,
                        const int framebuffer_width,
                        const int framebuffer_height,
                        const ImU32 color,
                        const float thickness,
                        const bool closed,
                        const bool arrows)
{
    if (points.size() < 2U)
    {
        return;
    }
    std::vector<ImVec2> projected;
    projected.reserve(points.size());
    for (const PlanGeoPoint &point : points)
    {
        const ProjectedPoint screen = project_to_screen(
            mvp, plan_world_position(options, tiles, point), framebuffer_width, framebuffer_height);
        if (!screen.visible)
        {
            projected.push_back(ImVec2(-100000.0F, -100000.0F));
            continue;
        }
        projected.push_back(screen.screen);
    }

    const auto draw_segment = [&](const ImVec2 a, const ImVec2 b, const bool arrow)
    {
        if (a.x < -99999.0F || b.x < -99999.0F)
        {
            return;
        }
        draw->AddLine(a, b, color, thickness);
        if (!arrow)
        {
            return;
        }
        const ImVec2 delta(b.x - a.x, b.y - a.y);
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (length < 22.0F)
        {
            return;
        }
        const ImVec2 dir(delta.x / length, delta.y / length);
        const ImVec2 normal(-dir.y, dir.x);
        const ImVec2 tip(a.x + delta.x * 0.62F, a.y + delta.y * 0.62F);
        draw->AddTriangleFilled(
            tip,
            ImVec2(tip.x - dir.x * 8.0F + normal.x * 4.0F, tip.y - dir.y * 8.0F + normal.y * 4.0F),
            ImVec2(tip.x - dir.x * 8.0F - normal.x * 4.0F, tip.y - dir.y * 8.0F - normal.y * 4.0F),
            color);
    };

    for (std::size_t index = 1U; index < projected.size(); ++index)
    {
        draw_segment(projected[index - 1U], projected[index], arrows);
    }
    if (closed)
    {
        draw_segment(projected.back(), projected.front(), false);
    }
}

void draw_plan_overlay(const Options &options,
                       const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                       const PlanVisualizationState &plan_state,
                       const animus::app::AppLayerSettings &layers,
                       const Camera &camera,
                       const Map2DCamera &map_camera,
                       const animus::app::ViewMode view_mode,
                       const int framebuffer_width,
                       const int framebuffer_height)
{
    if (!plan_state.overlay_visible || !plan_state.data ||
        (!layers.planned_route_visible && !layers.geofence_rally_visible))
    {
        return;
    }
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const Mat4 mvp = active_view_projection(
        camera, map_camera, view_mode, framebuffer_width, framebuffer_height);
    const PlanVisualizationData &plan = *plan_state.data;

    if (layers.planned_route_visible)
    {
        std::vector<PlanGeoPoint> route;
        route.reserve(plan.mission_waypoints.size());
        for (const auto &waypoint : plan.mission_waypoints)
        {
            route.push_back(waypoint.point);
        }
        draw_plan_polyline(draw,
                           options,
                           tiles,
                           mvp,
                           route,
                           framebuffer_width,
                           framebuffer_height,
                           IM_COL32(255, 213, 94, 232),
                           2.2F,
                           false,
                           true);
        for (const auto &outline : plan.complex_outlines)
        {
            draw_plan_polyline(draw,
                               options,
                               tiles,
                               mvp,
                               outline.points,
                               framebuffer_width,
                               framebuffer_height,
                               IM_COL32(137, 209, 255, 184),
                               1.8F,
                               false,
                               false);
        }
    }
    if (layers.geofence_rally_visible)
    {
        for (const auto &polygon : plan.geofence_polygons)
        {
            draw_plan_polyline(draw,
                               options,
                               tiles,
                               mvp,
                               polygon.points,
                               framebuffer_width,
                               framebuffer_height,
                               IM_COL32(248, 114, 114, 202),
                               2.0F,
                               true,
                               false);
        }
        for (const auto &circle : plan.geofence_circles)
        {
            draw_plan_polyline(draw,
                               options,
                               tiles,
                               mvp,
                               circle_points(circle.center, circle.radius_m),
                               framebuffer_width,
                               framebuffer_height,
                               IM_COL32(248, 114, 114, 178),
                               1.8F,
                               false,
                               false);
        }
    }

    const auto draw_point = [&](const PlanGeoPoint &point,
                                const std::string &label,
                                const ImU32 fill,
                                const ImU32 stroke)
    {
        const ProjectedPoint screen = project_to_screen(
            mvp, plan_world_position(options, tiles, point), framebuffer_width, framebuffer_height);
        if (!screen.visible)
        {
            return;
        }
        draw->AddCircleFilled(screen.screen, 5.8F, fill, 20);
        draw->AddCircle(screen.screen, 7.4F, stroke, 24, 1.4F);
        if (!label.empty())
        {
            draw_tool_label(draw, screen.screen, label, IM_COL32(242, 246, 248, 236));
        }
    };
    if (layers.planned_route_visible)
    {
        for (const auto &waypoint : plan.mission_waypoints)
        {
            draw_point(waypoint.point,
                       waypoint.label,
                       IM_COL32(255, 213, 94, 242),
                       IM_COL32(255, 246, 184, 220));
        }
    }
    if (layers.geofence_rally_visible)
    {
        for (const auto &rally : plan.rally_points)
        {
            draw_point(rally.point,
                       rally.label,
                       IM_COL32(88, 221, 155, 232),
                       IM_COL32(203, 250, 226, 214));
        }
    }
}

void draw_map2d_overlay(const Options &options,
                        const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                        const Map2DCamera &camera,
                        const int framebuffer_width,
                        const int framebuffer_height,
                        const int selected_zoom)
{
    const float width = static_cast<float>(std::max(1, framebuffer_width));
    const float height = static_cast<float>(std::max(1, framebuffer_height));
    const float aspect = width / height;
    const float half_height = std::clamp(camera.distance, 0.25F, 80.0F);
    const float half_width = half_height * aspect;
    const Vec3 right = map_right_vector(camera);
    const Vec3 up = map_up_vector(camera);

    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool cursor_inside =
        mouse.x >= 0.0F && mouse.y >= 0.0F && mouse.x < width && mouse.y < height;
    const float center_global_x = static_cast<float>(options.center_x) + 0.5F + camera.target_x;
    const float center_global_y = static_cast<float>(options.center_y) + 0.5F + camera.target_z;
    float cursor_global_x = center_global_x;
    float cursor_global_y = center_global_y;
    if (cursor_inside)
    {
        const float sx = (mouse.x / width - 0.5F) * 2.0F * half_width;
        const float sy = (0.5F - mouse.y / height) * 2.0F * half_height;
        const float world_x = camera.target_x + right.x * sx + up.x * sy;
        const float world_z = camera.target_z + right.z * sx + up.z * sy;
        cursor_global_x = static_cast<float>(options.center_x) + 0.5F + world_x;
        cursor_global_y = static_cast<float>(options.center_y) + 0.5F + world_z;
    }
    const auto lat_lon =
        animus::geo_core::tile_space_to_lat_lon(cursor_global_x, cursor_global_y, options.z);
    const int axis = animus::geo_core::tiles_per_axis(options.z);
    const TileCoord cursor_tile{
        options.z,
        std::clamp(static_cast<int>(std::floor(cursor_global_x)), 0, axis - 1),
        std::clamp(static_cast<int>(std::floor(cursor_global_y)), 0, axis - 1)};
    const auto terrain_height =
        sample_resident_terrain_height_m(options, tiles, lat_lon.u, lat_lon.v);

    const float bar_pixels = 120.0F;
    const double tile_fraction = (static_cast<double>(bar_pixels) / static_cast<double>(height)) *
                                 static_cast<double>(half_height) * 2.0;
    const auto bar_start =
        animus::geo_core::tile_space_to_lat_lon(center_global_x, center_global_y, options.z);
    const auto bar_end = animus::geo_core::tile_space_to_lat_lon(
        center_global_x + tile_fraction, center_global_y, options.z);
    const double meters_per_deg_lon =
        std::cos(bar_start.u * 3.14159265358979323846 / 180.0) * 111320.0;
    const double bar_meters = std::abs(bar_end.v - bar_start.v) * meters_per_deg_lon;

    const ImVec2 panel_min(14.0F, height - 112.0F);
    const ImVec2 panel_max(330.0F, height - 14.0F);
    draw->AddRectFilled(panel_min, panel_max, IM_COL32(11, 16, 19, 188), 7.0F);
    draw->AddRect(panel_min, panel_max, IM_COL32(122, 145, 156, 86), 7.0F);
    draw->AddText(
        ImVec2(panel_min.x + 10.0F, panel_min.y + 8.0F), IM_COL32(229, 236, 240, 238), "Map2D");
    char text[192] = {};
    std::snprintf(text, sizeof(text), "z %d  %.6f %.6f", selected_zoom, lat_lon.u, lat_lon.v);
    draw->AddText(
        ImVec2(panel_min.x + 10.0F, panel_min.y + 30.0F), IM_COL32(190, 201, 207, 232), text);
    std::snprintf(text,
                  sizeof(text),
                  "tile %d/%d/%d  elev %s",
                  cursor_tile.z,
                  cursor_tile.x,
                  cursor_tile.y,
                  terrain_height ? std::to_string(static_cast<int>(*terrain_height)).c_str()
                                 : "n/a");
    draw->AddText(
        ImVec2(panel_min.x + 10.0F, panel_min.y + 52.0F), IM_COL32(190, 201, 207, 232), text);
    const ImVec2 bar_a(panel_min.x + 10.0F, panel_min.y + 82.0F);
    const ImVec2 bar_b(panel_min.x + 10.0F + bar_pixels, panel_min.y + 82.0F);
    draw->AddLine(bar_a, bar_b, IM_COL32(232, 238, 241, 238), 3.0F);
    draw->AddLine(bar_a, ImVec2(bar_a.x, bar_a.y - 8.0F), IM_COL32(232, 238, 241, 238), 2.0F);
    draw->AddLine(bar_b, ImVec2(bar_b.x, bar_b.y - 8.0F), IM_COL32(232, 238, 241, 238), 2.0F);
    std::snprintf(text,
                  sizeof(text),
                  "%.0f %s",
                  bar_meters >= 1000.0 ? bar_meters / 1000.0 : bar_meters,
                  bar_meters >= 1000.0 ? "km" : "m");
    draw->AddText(ImVec2(bar_b.x + 10.0F, bar_b.y - 11.0F), IM_COL32(232, 238, 241, 238), text);

    const ImVec2 arrow_base(width - 54.0F, 72.0F);
    const float north_screen_rad = -camera.rotation_rad;
    const ImVec2 arrow_tip(arrow_base.x + std::sin(north_screen_rad) * 28.0F,
                           arrow_base.y - std::cos(north_screen_rad) * 28.0F);
    draw->AddCircleFilled(arrow_base, 22.0F, IM_COL32(11, 16, 19, 176), 32);
    draw->AddLine(arrow_base, arrow_tip, IM_COL32(235, 241, 244, 244), 3.0F);
    draw->AddText(
        ImVec2(arrow_tip.x - 4.0F, arrow_tip.y - 18.0F), IM_COL32(235, 241, 244, 244), "N");
}

void render_frame(const animus::render_core::GlfwWindow &window,
                  const animus::render_core::ShaderProgram &program,
                  const animus::render_core::ShaderProgram &overlay_program,
                  const animus::render_core::ShaderProgram &model_program,
                  const std::unordered_map<TileCoord, TerrainTileGpu> &tiles,
                  const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                  const animus::render_core::ModelMesh *selected_model,
                  const Mat4 &selected_model_matrix,
                  const Camera &camera,
                  const Map2DCamera &map_camera,
                  animus::app::ViewMode view_mode,
                  float height_scale,
                  bool state_colors,
                  bool highlight_fallback)
{
    const int framebuffer_width = window.framebuffer_width();
    const int framebuffer_height = window.framebuffer_height();
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(0.11F, 0.15F, 0.17F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Mat4 mvp = active_view_projection(
        camera, map_camera, view_mode, framebuffer_width, framebuffer_height);
    const bool map_mode = view_mode == animus::app::ViewMode::Map2D;
    if (map_mode)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
    }
    program.use();
    glUniformMatrix4fv(glGetUniformLocation(program.id(), "mvp"), 1, GL_FALSE, mvp.data.data());
    glUniform1f(glGetUniformLocation(program.id(), "terrain_height_factor"),
                map_mode ? 0.0F : 1.0F);
    glUniform1i(glGetUniformLocation(program.id(), "imagery_tex"), 0);
    glUniform1i(glGetUniformLocation(program.id(), "height_tex"), 1);
    glUniform1f(glGetUniformLocation(program.id(), "height_scale"), map_mode ? 0.0F : height_scale);

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

    if (selected_model != nullptr && !map_mode)
    {
        model_program.use();
        glUniformMatrix4fv(glGetUniformLocation(model_program.id(), "view_projection"),
                           1,
                           GL_FALSE,
                           mvp.data.data());
        glUniformMatrix4fv(glGetUniformLocation(model_program.id(), "model"),
                           1,
                           GL_FALSE,
                           selected_model_matrix.data.data());
        for (const auto &primitive : selected_model->primitives())
        {
            glUniform4fv(glGetUniformLocation(model_program.id(), "base_color"),
                         1,
                         primitive.base_color().data());
            primitive.draw();
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    overlay_program.use();
    glUniformMatrix4fv(
        glGetUniformLocation(overlay_program.id(), "mvp"), 1, GL_FALSE, mvp.data.data());
    glUniform1f(glGetUniformLocation(overlay_program.id(), "terrain_height_factor"),
                map_mode ? 0.0F : 1.0F);
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

int run(Options options)
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
        if (options.telemetry_format == animus::telemetry_core::TelemetryImportFormat::Tlog)
        {
            std::ifstream input(options.telemetry, std::ios::binary);
            if (input)
            {
                const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                                      std::istreambuf_iterator<char>());
                const animus::telemetry_core::MavlinkParseResult parsed =
                    animus::telemetry_core::parse_mavlink_stream(bytes);
                telemetry.mavlink_values.ingest_messages(parsed.messages);
            }
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
    const animus::render_core::ShaderProgram selected_model_program(model_vertex_shader,
                                                                    model_fragment_shader);
    VehicleRenderState vehicle_render = load_vehicle_render_state();
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
    bool state_colors = options.layers.tile_state_debug_visible;
    bool highlight_fallback = options.layers.fallback_highlight_visible;
    bool overlay_enabled = options.layers.geotiff_overlay_visible;
    float overlay_opacity = options.layers.geotiff_overlay_opacity;
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
    PlanVisualizationState plan_state;
    MapToolState map_tools;
    std::optional<MapToolPoint> map_context_point;
    apply_options_to_ui(options, ui_state, input.map_camera);
    if (options.developer_workspace && options.debug_overlay)
    {
        ui_state.workspace_mode = animus::app::WorkspaceMode::Developer;
        ui_state.developer_diagnostics_visible = true;
        ui_state.workspace_layout_applied = false;
    }
    animus::app::AppConfig saved_config_baseline = animus::app::app_config_from_options(options);
    ui_state.telemetry_entity_selected = !telemetry.timeline.entities.empty();
    if (!options.plan.empty())
    {
        std::snprintf(
            plan_state.path.data(), plan_state.path.size(), "%s", options.plan.string().c_str());
        const PlanVisualizationLoadResult plan_result =
            animus::app::load_plan_visualization(options.plan);
        plan_state.data = plan_result.data;
        plan_state.diagnostics = plan_result.diagnostics;
        plan_state.error = plan_result.error;
        plan_state.loaded_path = plan_result.data ? options.plan : std::filesystem::path{};
    }
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
                std::vector<animus::telemetry_live::ParsedUdpMavlinkDatagram> parsed_datagrams;
                parsed_datagrams.reserve(datagrams.size());
                for (const animus::telemetry_live::UdpMavlinkDatagram &datagram : datagrams)
                {
                    animus::telemetry_live::ParsedUdpMavlinkDatagram parsed;
                    parsed.receive_time_s = datagram.receive_time_s;
                    parsed.byte_count = datagram.bytes.size();
                    parsed.parsed = animus::telemetry_core::parse_mavlink_stream(datagram.bytes);
                    parsed_datagrams.push_back(std::move(parsed));
                }
                telemetry.mavlink_values.ingest(parsed_datagrams);
                live_buffer->ingest_parsed(parsed_datagrams);
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
                        ui_state.telemetry_entity_selected = true;
                    }
                }
                else
                {
                    ui_state.telemetry_entity_selected = false;
                    ui_state.follow_selected_entity = false;
                }
            }
        }
        telemetry.clock.advance(stats.last_frame_seconds());
        const bool camera_mouse_enabled =
            debug_layer == nullptr || !debug_layer->wants_mouse_capture();
        const bool camera_keyboard_enabled =
            debug_layer == nullptr || !debug_layer->wants_keyboard_capture();
        const bool camera_target_panned = update_camera(window.native_handle(),
                                                        input,
                                                        ui_state.view_mode,
                                                        camera_mouse_enabled,
                                                        camera_keyboard_enabled);
        if (camera_target_panned)
        {
            ui_state.follow_selected_entity = false;
        }
        const bool follow_pressed = glfwGetKey(window.native_handle(), GLFW_KEY_F) == GLFW_PRESS;
        if (camera_keyboard_enabled && follow_pressed && !input.was_follow_pressed)
        {
            ui_state.follow_selected_entity =
                ui_state.telemetry_entity_selected && !ui_state.follow_selected_entity;
        }
        input.was_follow_pressed = camera_keyboard_enabled && follow_pressed;

        const bool space_pressed = glfwGetKey(window.native_handle(), GLFW_KEY_SPACE) == GLFW_PRESS;
        if (camera_keyboard_enabled && space_pressed && !input.was_space_pressed &&
            telemetry.loaded && !telemetry.live)
        {
            telemetry.clock.set_paused(!telemetry.clock.paused());
        }
        input.was_space_pressed = camera_keyboard_enabled && space_pressed;

        const bool escape_pressed =
            glfwGetKey(window.native_handle(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (camera_keyboard_enabled && escape_pressed && !input.was_escape_pressed)
        {
            if (ui_state.active_mode == animus::app::UiNavigationMode::Telemetry)
            {
                ui_state.follow_selected_entity = false;
                ui_state.telemetry_entity_selected = false;
                ui_state.inspector_target = animus::app::InspectorTarget::TelemetrySource;
            }
            else
            {
                window.request_close();
            }
        }
        input.was_escape_pressed = camera_keyboard_enabled && escape_pressed;

        if (ui_state.follow_selected_entity && ui_state.telemetry_entity_selected)
        {
            const auto followed =
                telemetry.timeline.sample_at(telemetry.selected_entity, telemetry.clock.time_s());
            if (followed && telemetry_sample_placeable(*followed))
            {
                bool terrain_unavailable = false;
                bool unknown_datum = false;
                bool geoid_unavailable = false;
                const Vec3 world = telemetry_world_position(options,
                                                            tiles,
                                                            geoid_grid,
                                                            *followed,
                                                            terrain_unavailable,
                                                            unknown_datum,
                                                            geoid_unavailable);
                if (ui_state.view_mode == animus::app::ViewMode::Map2D)
                {
                    input.map_camera.target_x = world.x;
                    input.map_camera.target_z = world.z;
                }
                else
                {
                    input.camera.target = world;
                }
            }
        }

        if (ui_state.request_center_selected_entity)
        {
            center_selected_entity(options, tiles, geoid_grid, telemetry, ui_state, input);
        }
        ui_state.request_center_selected_entity = false;

        if (ui_state.request_fit_selected_entity)
        {
            fit_selected_entity(options, tiles, geoid_grid, telemetry, ui_state, input);
        }
        ui_state.request_fit_selected_entity = false;

        if (ui_state.request_jump_latest_sample && telemetry.loaded && !telemetry.live)
        {
            telemetry.clock.seek(telemetry.timeline.end_time_s);
        }
        ui_state.request_jump_latest_sample = false;

        if (ui_state.request_review_jump_time_s && telemetry.loaded && !telemetry.live)
        {
            telemetry.clock.seek(*ui_state.request_review_jump_time_s);
        }
        ui_state.request_review_jump_time_s.reset();

        if (ui_state.request_home_view)
        {
            if (ui_state.view_mode == animus::app::ViewMode::Map2D)
            {
                input.map_camera = Map2DCamera{};
            }
            else
            {
                input.camera = Camera{};
            }
        }
        ui_state.request_home_view = false;

        if (ui_state.zoom_steps != 0)
        {
            const float zoom = std::pow(0.82F, static_cast<float>(ui_state.zoom_steps));
            if (ui_state.view_mode == animus::app::ViewMode::Map2D)
            {
                input.map_camera.distance =
                    std::clamp(input.map_camera.distance * zoom, 0.35F, 80.0F);
            }
            else
            {
                input.camera.distance = std::clamp(input.camera.distance * zoom, 0.45F, 40.0F);
            }
            ui_state.zoom_steps = 0;
        }

        if (ui_state.request_fit_all_entities && telemetry.loaded)
        {
            bool any = false;
            float min_x = std::numeric_limits<float>::max();
            float min_z = std::numeric_limits<float>::max();
            float max_x = std::numeric_limits<float>::lowest();
            float max_z = std::numeric_limits<float>::lowest();
            for (const auto &entity : telemetry.timeline.entities)
            {
                const auto sample =
                    telemetry.timeline.sample_at(entity.id, telemetry.clock.time_s());
                if (!sample || !telemetry_sample_placeable(*sample))
                {
                    continue;
                }
                bool terrain_unavailable = false;
                bool unknown_datum = false;
                bool geoid_unavailable = false;
                const Vec3 world = telemetry_world_position(options,
                                                            tiles,
                                                            geoid_grid,
                                                            *sample,
                                                            terrain_unavailable,
                                                            unknown_datum,
                                                            geoid_unavailable);
                min_x = std::min(min_x, world.x);
                min_z = std::min(min_z, world.z);
                max_x = std::max(max_x, world.x);
                max_z = std::max(max_z, world.z);
                any = true;
            }
            if (any)
            {
                input.map_camera.target_x = (min_x + max_x) * 0.5F;
                input.map_camera.target_z = (min_z + max_z) * 0.5F;
                input.map_camera.distance =
                    std::clamp(std::max(max_x - min_x, max_z - min_z) * 0.8F + 0.8F, 0.35F, 80.0F);
                ui_state.view_mode = animus::app::ViewMode::Map2D;
            }
        }
        ui_state.request_fit_all_entities = false;

        if (ui_state.view_mode == animus::app::ViewMode::Map2D)
        {
            if (input.map_camera.orientation == MapOrientationMode::NorthUp)
            {
                input.map_camera.rotation_rad = 0.0F;
            }
            else if (input.map_camera.orientation == MapOrientationMode::TrackUp)
            {
                const auto sample = ui_state.telemetry_entity_selected
                                        ? telemetry.timeline.sample_at(telemetry.selected_entity,
                                                                       telemetry.clock.time_s())
                                        : std::optional<animus::telemetry_core::TelemetrySample>{};
                if (sample)
                {
                    if (const auto heading = telemetry_heading_rad(*sample))
                    {
                        input.map_camera.rotation_rad = *heading;
                    }
                    else
                    {
                        input.map_camera.rotation_rad = 0.0F;
                    }
                }
                else
                {
                    input.map_camera.rotation_rad = 0.0F;
                }
            }
        }

        const float active_distance = ui_state.view_mode == animus::app::ViewMode::Map2D
                                          ? input.map_camera.distance
                                          : input.camera.distance;
        const int selected_zoom = animus::terrain_core::select_zoom_for_distance(
            active_distance, options.min_z, options.max_z);
        const auto view = active_terrain_viewpoint(
            options, input.camera, input.map_camera, ui_state.view_mode, selected_zoom);
        desired_tiles =
            animus::terrain_core::build_tile_wishlist(view, selected_zoom, options.tile_budget);
        const std::vector<TileCoord> requested_tiles =
            add_parent_fallback_requests(desired_tiles, options.min_z);
        streamer.request_tiles(build_load_requests(options,
                                                   pack_root,
                                                   requested_tiles,
                                                   view,
                                                   stream_generation,
                                                   ui_state.bathymetry_enabled));

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
        update_selected_entity_terrain_state(
            options, tiles, geoid_grid, options.status_thresholds, telemetry, ui_state);
        if (telemetry.loaded && !telemetry.live && ui_state.telemetry_entity_selected)
        {
            const animus::app::TimelineReviewThresholds review_thresholds{
                options.status_thresholds.terrain_clearance_warning_m,
                options.status_thresholds.terrain_clearance_critical_m,
                options.status_thresholds.roll_warning_deg,
                options.status_thresholds.pitch_warning_deg,
                options.status_thresholds.frame_time_warning_ms,
                options.status_thresholds.telemetry_gap_warning_s,
                options.status_thresholds.telemetry_gap_critical_s,
                options.status_thresholds.plan_deviation_warning_m,
                options.status_thresholds.plan_altitude_error_warning_m};
            std::vector<animus::app::TerrainClearanceSample> review_clearance =
                build_review_clearance_samples(
                    options, tiles, geoid_grid, telemetry.timeline, telemetry.selected_entity);
            std::vector<animus::app::TerrainClearanceSample> planned_clearance =
                build_planned_path_clearance_samples(
                    options, tiles, plan_state, telemetry.timeline);
            review_clearance.insert(
                review_clearance.end(), planned_clearance.begin(), planned_clearance.end());
            std::stable_sort(review_clearance.begin(),
                             review_clearance.end(),
                             [](const auto &lhs, const auto &rhs)
                             { return lhs.time_s < rhs.time_s; });
            telemetry.review =
                animus::app::build_timeline_review(telemetry.timeline,
                                                   telemetry.selected_entity,
                                                   ui_state.timeline_bookmarks,
                                                   review_clearance,
                                                   ui_state.timeline_frame_time_markers,
                                                   review_thresholds,
                                                   plan_state.data ? &*plan_state.data : nullptr,
                                                   animus::app::default_timeline_review_sample_cap);
        }
        else
        {
            telemetry.review = {};
        }

        if (debug_layer != nullptr)
        {
            const ImGuiIO &io = ImGui::GetIO();
            const auto resolve_screen_point =
                [&](const ImVec2 screen) -> std::optional<MapToolPoint>
            {
                if (ui_state.view_mode == animus::app::ViewMode::Map2D)
                {
                    return map2d_point_from_screen(options,
                                                   tiles,
                                                   input.map_camera,
                                                   screen,
                                                   window.framebuffer_width(),
                                                   window.framebuffer_height());
                }
                return terrain3d_point_from_screen(options,
                                                   tiles,
                                                   input.camera,
                                                   screen,
                                                   window.framebuffer_width(),
                                                   window.framebuffer_height());
            };
            if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                map_context_point = resolve_screen_point(io.MousePos);
                if (map_context_point)
                {
                    ImGui::OpenPopup("map_tool_context");
                }
            }
            if (!io.WantCaptureMouse && map_tools.mode == ToolMode::RangeBearing &&
                map_tools.range_anchor && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (const auto endpoint = resolve_screen_point(io.MousePos))
                {
                    map_tools.range_endpoint = endpoint;
                }
            }
        }

        const animus::render_core::ModelMesh *selected_model = nullptr;
        Mat4 selected_model_matrix_value = identity();
        animus::app::VehicleResolvedVisual selected_visual;
        const auto selected_entity =
            ui_state.telemetry_entity_selected
                ? std::find_if(telemetry.timeline.entities.begin(),
                               telemetry.timeline.entities.end(),
                               [&](const animus::telemetry_core::Entity &entity)
                               { return entity.id == telemetry.selected_entity; })
                : telemetry.timeline.entities.end();
        if (selected_entity != telemetry.timeline.entities.end())
        {
            selected_visual = animus::app::resolve_vehicle_visual(vehicle_render.registry,
                                                                  options.vehicle_visuals,
                                                                  *selected_entity,
                                                                  false,
                                                                  "not loaded");
            VehicleRenderState::LoadedModel &model =
                ensure_vehicle_model_loaded(vehicle_render, selected_visual.vehicle_id);
            selected_visual = animus::app::resolve_vehicle_visual(vehicle_render.registry,
                                                                  options.vehicle_visuals,
                                                                  *selected_entity,
                                                                  model.loaded,
                                                                  model.status);
            const auto sample =
                telemetry.timeline.sample_at(telemetry.selected_entity, telemetry.clock.time_s());
            if (ui_state.view_mode == animus::app::ViewMode::Terrain3D && sample &&
                telemetry_sample_placeable(*sample) && !selected_visual.force_icon_only &&
                model.definition != nullptr && model.mesh)
            {
                bool terrain_unavailable = false;
                bool unknown_datum = false;
                bool geoid_unavailable = false;
                const Vec3 world = telemetry_world_position(options,
                                                            tiles,
                                                            geoid_grid,
                                                            *sample,
                                                            terrain_unavailable,
                                                            unknown_datum,
                                                            geoid_unavailable);
                selected_model_matrix_value = selected_vehicle_model_matrix(
                    *model.definition,
                    world,
                    selected_visual.heading_source == "none" ? std::nullopt
                                                             : telemetry_heading_rad(*sample));
                selected_model = model.mesh.get();
            }
        }
        refresh_vehicle_runtime_status(vehicle_render, selected_visual);

        render_frame(window,
                     program,
                     overlay_program,
                     selected_model_program,
                     tiles,
                     visible_tiles,
                     selected_model,
                     selected_model_matrix_value,
                     input.camera,
                     input.map_camera,
                     ui_state.view_mode,
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
                                       vehicle_render,
                                       ui_state,
                                       input.camera,
                                       input.map_camera,
                                       window.framebuffer_width(),
                                       window.framebuffer_height(),
                                       selected_model != nullptr);
            telemetry.live_overlay_draw_ms = overlay_stats.draw_ms;
            telemetry.live_rendered_trail_points = overlay_stats.rendered_trail_points;
            draw_plan_overlay(options,
                              tiles,
                              plan_state,
                              ui_state.layers,
                              input.camera,
                              input.map_camera,
                              ui_state.view_mode,
                              window.framebuffer_width(),
                              window.framebuffer_height());
            if (ui_state.view_mode == animus::app::ViewMode::Map2D)
            {
                draw_map2d_overlay(options,
                                   tiles,
                                   input.map_camera,
                                   window.framebuffer_width(),
                                   window.framebuffer_height(),
                                   selected_zoom);
            }
            draw_map_tools_overlay(map_tools,
                                   input.camera,
                                   input.map_camera,
                                   ui_state.view_mode,
                                   window.framebuffer_width(),
                                   window.framebuffer_height());
            if (map_context_point)
            {
                draw_map_tool_popup(map_tools,
                                    streamer.snapshot(),
                                    visible_tiles,
                                    options,
                                    tiles,
                                    telemetry,
                                    ui_state,
                                    input,
                                    *map_context_point);
            }
            draw_app_workspace(options,
                               pack_root,
                               info,
                               stats,
                               streamer.snapshot(),
                               input.camera,
                               input.map_camera,
                               selected_zoom,
                               visible_tiles,
                               upload_bytes_used,
                               texture_uploads_used,
                               mesh_uploads_used,
                               resident_gpu_bytes,
                               telemetry,
                               plan_state,
                               vehicle_render.status,
                               screenshot_tool,
                               mp4_recorder,
                               ui_state,
                               state_colors,
                               highlight_fallback,
                               overlay_enabled,
                               overlay_opacity);
            debug_layer->end_frame();
        }
        sync_options_from_ui(options, ui_state, input.map_camera, overlay_enabled, overlay_opacity);
        options.config_dirty =
            !(animus::app::app_config_from_options(options) == saved_config_baseline);
        if (ui_state.request_config_reset)
        {
            animus::app::apply_app_config_to_options(options, animus::app::default_app_config());
            options.config_load_status = "reset to defaults";
            options.config_save_status = "not saved";
            options.config_dirty = true;
            options.config_diagnostics.push_back("reset preferences to defaults");
            apply_options_to_ui(options, ui_state, input.map_camera);
            state_colors = ui_state.layers.tile_state_debug_visible;
            highlight_fallback = ui_state.layers.fallback_highlight_visible;
            overlay_enabled = ui_state.layers.geotiff_overlay_visible;
            overlay_opacity = ui_state.layers.geotiff_overlay_opacity;
            ui_state.request_config_reset = false;
        }
        if (ui_state.request_workspace_layout_reset)
        {
            const std::string workspace_id = workspace_config_value(ui_state.workspace_mode);
            ui_state.workspace_layouts[workspace_id] =
                animus::app::default_workspace_layout(workspace_id);
            ui_state.workspace_layout_applied = false;
            options.config_dirty = true;
            options.config_save_status = "not saved";
            options.config_diagnostics.push_back("reset current workspace layout to defaults");
            ui_state.request_workspace_layout_reset = false;
        }
        if (ui_state.request_config_reload)
        {
            const animus::app::AppConfigLoadResult result =
                animus::app::load_app_config_file(options.config_path);
            options.config_load_status = animus::app::app_config_load_status_label(result.status);
            options.config_diagnostics.insert(options.config_diagnostics.end(),
                                              result.diagnostics.begin(),
                                              result.diagnostics.end());
            if (result.status == animus::app::AppConfigLoadStatus::Loaded ||
                result.status == animus::app::AppConfigLoadStatus::LoadedLegacy)
            {
                animus::app::apply_app_config_to_options(options, result.config);
                apply_options_to_ui(options, ui_state, input.map_camera);
                state_colors = ui_state.layers.tile_state_debug_visible;
                highlight_fallback = ui_state.layers.fallback_highlight_visible;
                overlay_enabled = ui_state.layers.geotiff_overlay_visible;
                overlay_opacity = ui_state.layers.geotiff_overlay_opacity;
                options.config_dirty = false;
                saved_config_baseline = animus::app::app_config_from_options(options);
            }
            ui_state.request_config_reload = false;
        }
        if (ui_state.request_config_save || ui_state.request_config_save_default)
        {
            const std::filesystem::path original_path = options.config_path;
            if (ui_state.request_config_save_default)
            {
                options.config_path = animus::app::default_app_config_path();
            }
            const animus::app::AppConfigSaveResult save_result =
                animus::app::save_app_config(options);
            if (save_result.saved)
            {
                saved_config_baseline = animus::app::app_config_from_options(options);
            }
            if (ui_state.request_config_save_default)
            {
                options.config_path = original_path;
            }
            ui_state.request_config_save = false;
            ui_state.request_config_save_default = false;
        }
        if (live_debug_csv.enabled() && telemetry.live)
        {
            const auto current = ui_state.telemetry_entity_selected
                                     ? telemetry.timeline.sample_at(telemetry.selected_entity,
                                                                    telemetry.clock.time_s())
                                     : std::optional<animus::telemetry_core::TelemetrySample>{};
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
        if (telemetry.loaded && !telemetry.live)
        {
            const animus::app::TimelineReviewThresholds review_thresholds{
                options.status_thresholds.terrain_clearance_warning_m,
                options.status_thresholds.terrain_clearance_critical_m,
                options.status_thresholds.roll_warning_deg,
                options.status_thresholds.pitch_warning_deg,
                options.status_thresholds.frame_time_warning_ms,
                options.status_thresholds.telemetry_gap_warning_s,
                options.status_thresholds.telemetry_gap_critical_s};
            animus::app::observe_timeline_frame_time(ui_state.timeline_frame_time_state,
                                                     ui_state.timeline_frame_time_markers,
                                                     telemetry.clock.time_s(),
                                                     stats.last_frame_seconds() * 1000.0,
                                                     review_thresholds);
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
    return 0;
}

} // namespace

int animus_app_main(int argc, char **argv)
{
    return run(animus::app::parse_options(argc, argv));
}
