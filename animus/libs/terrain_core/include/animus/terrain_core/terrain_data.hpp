#pragma once

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace animus::terrain_core
{

struct RasterStats
{
    float min_value = 0.0F;
    float max_value = 0.0F;
};

struct RasterSample
{
    bool available = false;
    float value = 0.0F;
};

struct GeoidGrid
{
    double south_deg = 0.0;
    double west_deg = 0.0;
    double north_deg = 0.0;
    double east_deg = 0.0;
    Raster offsets_m;
};

struct TerrainMeshVertex
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

struct TerrainMeshCpu
{
    std::vector<TerrainMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct TerrainMeshOptions
{
    float origin_x = 0.0F;
    float origin_z = 0.0F;
    float tile_size = 1.0F;
    float height_scale = 1.0F;
    float skirt_depth = 0.0F;
};

[[nodiscard]] std::filesystem::path local_xyz_tile_path(const std::filesystem::path &pack_root,
                                                        const std::string &layer_name,
                                                        geo_core::TileCoord tile,
                                                        const std::string &extension);

[[nodiscard]] Raster load_png_rgba(const std::filesystem::path &path);
[[nodiscard]] Raster decode_png_rgba(std::span<const std::uint8_t> bytes);
[[nodiscard]] Raster decode_jpeg_rgba(std::span<const std::uint8_t> bytes);
[[nodiscard]] Raster decode_image_rgba(std::span<const std::uint8_t> bytes);
[[nodiscard]] float terrain_rgb_to_meters(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
[[nodiscard]] Raster decode_terrain_rgb(const Raster &image);
[[nodiscard]] RasterStats float_raster_min_max(const Raster &raster);
[[nodiscard]] RasterSample sample_float_raster_bilinear(const Raster &raster, double u, double v);
[[nodiscard]] RasterSample sample_geoid_grid(const GeoidGrid &grid, double lat_deg, double lon_deg);
[[nodiscard]] TerrainMeshCpu build_terrain_mesh(const Raster &heights,
                                                const TerrainMeshOptions &options);

} // namespace animus::terrain_core
