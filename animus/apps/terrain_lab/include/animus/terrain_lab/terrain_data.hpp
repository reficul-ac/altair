#pragma once

#include "animus/render_core/mesh.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace animus::terrain_lab {

struct XyzTile {
    int z = 0;
    int x = 0;
    int y = 0;
};

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

struct HeightGrid {
    int width = 0;
    int height = 0;
    std::vector<float> meters;
    float min_meters = std::numeric_limits<float>::infinity();
    float max_meters = -std::numeric_limits<float>::infinity();
};

struct TerrainMeshCpu {
    std::vector<render_core::TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
};

[[nodiscard]] std::filesystem::path tile_png_path(
    const std::filesystem::path& pack_root,
    const std::string& layer,
    const XyzTile& tile);

[[nodiscard]] float terrain_rgb_to_meters(std::uint8_t red, std::uint8_t green, std::uint8_t blue);

[[nodiscard]] RgbaImage load_png_rgba(const std::filesystem::path& path);
[[nodiscard]] HeightGrid decode_terrain_rgb(const RgbaImage& image);

[[nodiscard]] TerrainMeshCpu build_tile_mesh(
    const HeightGrid& heights,
    float origin_x,
    float origin_z,
    float tile_size,
    float height_scale,
    float skirt_depth);

} // namespace animus::terrain_lab
