#include "animus/render_core/gl_info.hpp"
#include "animus/render_core/mesh.hpp"
#include "animus/render_core/render_stats.hpp"
#include "animus/render_core/shader_program.hpp"
#include "animus/render_core/texture.hpp"
#include "animus/render_core/window.hpp"
#include "animus/terrain_lab/terrain_data.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using animus::terrain_lab::HeightGrid;
using animus::terrain_lab::RgbaImage;
using animus::terrain_lab::XyzTile;

struct Options {
    bool smoke = false;
    int frames = 0;
    int width = 1280;
    int height = 720;
    std::filesystem::path pack_root = "animus/data/tiles/lake_tahoe";
    std::filesystem::path capture_ppm;
    int z = 12;
    int center_x = 682;
    int center_y = 1563;
    int patch_radius = 1;
    float height_scale = 0.0015F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Mat4 {
    std::array<float, 16> data{};
};

struct Camera {
    Vec3 target{0.0F, 3.45F, 0.0F};
    float distance = 4.2F;
    float yaw = -0.72F;
    float pitch = 0.72F;
};

struct InputState {
    Camera camera;
    bool left_drag = false;
    bool middle_drag = false;
    bool was_reset_pressed = false;
    double last_x = 0.0;
    double last_y = 0.0;
};

struct TerrainTileGpu {
    XyzTile coord;
    HeightGrid heights;
    animus::render_core::IndexedMesh mesh;
    animus::render_core::Texture2D imagery;
    animus::render_core::Texture2D height_texture;

    TerrainTileGpu(
        XyzTile tile_coord,
        HeightGrid tile_heights,
        animus::render_core::IndexedMesh tile_mesh,
        animus::render_core::Texture2D imagery_texture,
        animus::render_core::Texture2D height_values)
        : coord(tile_coord),
          heights(std::move(tile_heights)),
          mesh(std::move(tile_mesh)),
          imagery(std::move(imagery_texture)),
          height_texture(std::move(height_values))
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
    color = vec4(mix(lit, lit * 0.82, contour * 0.08), 1.0);
}
)glsl";

int parse_positive_int(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const int parsed = std::stoi(std::string(value), &consumed);
    if (consumed != value.size() || parsed <= 0) {
        throw std::invalid_argument(
            std::string("Expected positive integer for ") + std::string(name));
    }
    return parsed;
}

int parse_int(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const int parsed = std::stoi(std::string(value), &consumed);
    if (consumed != value.size()) {
        throw std::invalid_argument("Expected integer for " + std::string(name));
    }
    return parsed;
}

float parse_positive_float(std::string_view name, std::string_view value)
{
    std::size_t consumed = 0;
    const float parsed = std::stof(std::string(value), &consumed);
    if (consumed != value.size() || parsed <= 0.0F) {
        throw std::invalid_argument(
            std::string("Expected positive float for ") + std::string(name));
    }
    return parsed;
}

std::string_view next_arg(int argc, char** argv, int& index, std::string_view option)
{
    if (++index >= argc) {
        throw std::invalid_argument("Missing value for " + std::string(option));
    }
    return argv[index];
}

Options parse_options(int argc, char** argv)
{
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--smoke") {
            options.smoke = true;
        } else if (arg == "--frames") {
            options.frames = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--width") {
            options.width = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--height") {
            options.height = parse_positive_int(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--pack-root") {
            options.pack_root = std::string(next_arg(argc, argv, index, arg));
        } else if (arg == "--z") {
            options.z = parse_int(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--center-x") {
            options.center_x = parse_int(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--center-y") {
            options.center_y = parse_int(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--height-scale") {
            options.height_scale = parse_positive_float(arg, next_arg(argc, argv, index, arg));
        } else if (arg == "--capture-ppm") {
            options.capture_ppm = std::string(next_arg(argc, argv, index, arg));
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "usage: terrain_lab [--pack-root PATH] [--z N] [--center-x N]\n"
                << "                   [--center-y N] [--frames N] [--width PX]\n"
                << "                   [--height PX] [--height-scale F] [--smoke]\n"
                << "                   [--capture-ppm PATH]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + std::string(arg));
        }
    }

    if (options.smoke && options.frames == 0) {
        options.frames = 1;
    }
    return options;
}

std::filesystem::path resolve_pack_root(const std::filesystem::path& requested)
{
    if (std::filesystem::exists(requested)) {
        return requested;
    }
    if (requested.is_relative()) {
        const std::filesystem::path source_relative =
            std::filesystem::path(ANIMUS_SOURCE_DIR).parent_path() / requested;
        if (std::filesystem::exists(source_relative)) {
            return source_relative;
        }
    }
    throw std::runtime_error("Terrain pack root does not exist: " + requested.string());
}

Mat4 identity()
{
    Mat4 matrix;
    matrix.data = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
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
    if (length <= 0.0F) {
        return {};
    }
    return value * (1.0F / length);
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
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

Vec3 camera_eye(const Camera& camera)
{
    const float cos_pitch = std::cos(camera.pitch);
    const Vec3 offset{
        camera.distance * cos_pitch * std::sin(camera.yaw),
        camera.distance * std::sin(camera.pitch),
        camera.distance * cos_pitch * std::cos(camera.yaw),
    };
    return camera.target + offset;
}

Mat4 camera_mvp(const Camera& camera, int width, int height)
{
    const float aspect =
        static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
    return multiply(
        perspective(45.0F * 3.1415926535F / 180.0F, aspect, 0.01F, 100.0F),
        look_at(camera_eye(camera), camera.target, {0.0F, 1.0F, 0.0F}));
}

void update_camera(GLFWwindow* window, InputState& input)
{
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    const bool left = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool middle = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const double dx = x - input.last_x;
    const double dy = y - input.last_y;

    if (middle && input.middle_drag) {
        input.camera.yaw -= static_cast<float>(dx) * 0.006F;
        input.camera.pitch =
            std::clamp(input.camera.pitch - static_cast<float>(dy) * 0.006F, 0.12F, 1.45F);
    }
    if (left && input.left_drag) {
        const Vec3 eye = camera_eye(input.camera);
        const Vec3 forward = normalize(input.camera.target - eye);
        const Vec3 right = normalize(cross(forward, {0.0F, 1.0F, 0.0F}));
        const Vec3 up_plane = normalize(cross(right, forward));
        const float scale = input.camera.distance * 0.0015F;
        input.camera.target = input.camera.target + right * static_cast<float>(-dx * scale) +
                              up_plane * static_cast<float>(dy * scale);
    }

    input.left_drag = left;
    input.middle_drag = middle;
    input.last_x = x;
    input.last_y = y;

    const bool reset_pressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (reset_pressed && !input.was_reset_pressed) {
        input.camera = Camera{};
    }
    input.was_reset_pressed = reset_pressed;
}

void scroll_callback(GLFWwindow* window, double, double yoffset)
{
    auto* input = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (input == nullptr) {
        return;
    }
    const float zoom = std::pow(0.88F, static_cast<float>(yoffset));
    input->camera.distance = std::clamp(input->camera.distance * zoom, 0.45F, 40.0F);
}

std::vector<TerrainTileGpu> load_tiles(const Options& options, const std::filesystem::path& pack_root)
{
    std::vector<TerrainTileGpu> tiles;
    tiles.reserve(9);

    float min_height = std::numeric_limits<float>::infinity();
    float max_height = -std::numeric_limits<float>::infinity();

    for (int dy = -options.patch_radius; dy <= options.patch_radius; ++dy) {
        for (int dx = -options.patch_radius; dx <= options.patch_radius; ++dx) {
            const XyzTile coord{options.z, options.center_x + dx, options.center_y + dy};
            const RgbaImage imagery =
                animus::terrain_lab::load_png_rgba(tile_png_path(pack_root, "imagery", coord));
            HeightGrid heights = animus::terrain_lab::decode_terrain_rgb(
                animus::terrain_lab::load_png_rgba(tile_png_path(pack_root, "elevation", coord)));

            min_height = std::min(min_height, heights.min_meters);
            max_height = std::max(max_height, heights.max_meters);

            const float origin_x = static_cast<float>(dx);
            const float origin_z = static_cast<float>(dy);
            const auto cpu_mesh = animus::terrain_lab::build_tile_mesh(
                heights,
                origin_x,
                origin_z,
                1.0F,
                options.height_scale,
                180.0F * options.height_scale);

            animus::render_core::IndexedMesh gpu_mesh(cpu_mesh.vertices, cpu_mesh.indices);
            animus::render_core::Texture2D imagery_texture;
            imagery_texture.upload_rgba8(imagery.width, imagery.height, imagery.pixels);
            animus::render_core::Texture2D height_texture;
            height_texture.upload_r32f(heights.width, heights.height, heights.meters);

            tiles.emplace_back(
                coord,
                std::move(heights),
                std::move(gpu_mesh),
                std::move(imagery_texture),
                std::move(height_texture));
        }
    }

    std::cout << "Loaded terrain tiles: " << tiles.size() << " from " << pack_root
              << "\nElevation meters: min=" << min_height << " max=" << max_height << '\n';
    return tiles;
}

void write_ppm_capture(const std::filesystem::path& path, int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to open capture path: " + path.string());
    }
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = height - 1; row >= 0; --row) {
        const auto offset = static_cast<std::size_t>(row * width * 3);
        out.write(reinterpret_cast<const char*>(pixels.data() + offset), width * 3);
    }
}

void render_frame(
    const animus::render_core::GlfwWindow& window,
    const animus::render_core::ShaderProgram& program,
    const std::vector<TerrainTileGpu>& tiles,
    const Camera& camera,
    float height_scale)
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

    for (const auto& tile : tiles) {
        tile.imagery.bind_to_unit(0);
        tile.height_texture.bind_to_unit(1);
        tile.mesh.draw();
    }

    window.swap_buffers();
}

int run(const Options& options)
{
    const std::filesystem::path pack_root = resolve_pack_root(options.pack_root);

    animus::render_core::GlfwWindow window({
        options.width,
        options.height,
        "terrain_lab",
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
    const std::vector<TerrainTileGpu> tiles = load_tiles(options, pack_root);
    animus::render_core::RenderStats stats;
    bool captured = false;

    while (!window.should_close()) {
        stats.frame_started();
        update_camera(window.native_handle(), input);
        render_frame(window, program, tiles, input.camera, options.height_scale);
        if (!captured && !options.capture_ppm.empty()) {
            write_ppm_capture(
                options.capture_ppm,
                window.framebuffer_width(),
                window.framebuffer_height());
            captured = true;
        }
        stats.frame_finished();

        window.poll_events();
        if (window.escape_pressed()) {
            window.request_close();
        }
        if (options.frames > 0 && stats.frame_count() >= options.frames) {
            window.request_close();
        }
    }

    std::cout << "Rendered frames: " << stats.frame_count()
              << "\nLast frame seconds: " << stats.last_frame_seconds()
              << "\nTotal render seconds: " << stats.total_seconds() << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "terrain_lab: " << error.what() << '\n';
        return 1;
    }
}
