#include "animus/terrain_core/terrain_data.hpp"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace animus::terrain_core
{
namespace
{

struct FileCloser
{
    void operator()(std::FILE *file) const
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
};

void add_quad(std::vector<std::uint32_t> &indices,
              const std::uint32_t a,
              const std::uint32_t b,
              const std::uint32_t c,
              const std::uint32_t d)
{
    indices.push_back(a);
    indices.push_back(c);
    indices.push_back(b);
    indices.push_back(b);
    indices.push_back(c);
    indices.push_back(d);
}

void validate_raster_shape(const Raster &raster, const RasterFormat format, const int channels)
{
    if (raster.width <= 0 || raster.height <= 0 || raster.channels != channels ||
        raster.format != format)
    {
        throw std::invalid_argument("Raster format or dimensions are invalid");
    }
}

} // namespace

std::filesystem::path local_xyz_tile_path(const std::filesystem::path &pack_root,
                                          const std::string &layer_name,
                                          const geo_core::TileCoord tile,
                                          const std::string &extension)
{
    return pack_root / layer_name / std::to_string(tile.z) / std::to_string(tile.x) /
           (std::to_string(tile.y) + "." + extension);
}

Raster load_png_rgba(const std::filesystem::path &path)
{
    std::unique_ptr<std::FILE, FileCloser> file(std::fopen(path.string().c_str(), "rb"));
    if (file == nullptr)
    {
        throw std::runtime_error("Failed to open PNG: " + path.string());
    }

    png_byte signature[8] = {};
    if (std::fread(signature, 1, sizeof(signature), file.get()) != sizeof(signature) ||
        png_sig_cmp(signature, 0, sizeof(signature)) != 0)
    {
        throw std::runtime_error("File is not a PNG: " + path.string());
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr)
    {
        throw std::runtime_error("png_create_read_struct failed");
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr)
    {
        png_destroy_read_struct(&png, nullptr, nullptr);
        throw std::runtime_error("png_create_info_struct failed");
    }

    if (setjmp(png_jmpbuf(png)) != 0)
    {
        png_destroy_read_struct(&png, &info, nullptr);
        throw std::runtime_error("Failed to decode PNG: " + path.string());
    }

    png_init_io(png, file.get());
    png_set_sig_bytes(png, sizeof(signature));
    png_read_info(png, info);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

    if (bit_depth == 16)
    {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0)
    {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        png_set_gray_to_rgb(png);
    }
    if ((color_type & PNG_COLOR_MASK_ALPHA) == 0)
    {
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    Raster raster;
    raster.width = static_cast<int>(width);
    raster.height = static_cast<int>(height);
    raster.channels = 4;
    raster.format = RasterFormat::UInt8RGBA;
    raster.sampling_mode = SamplingMode::Center;
    raster.byte_data.resize(static_cast<std::size_t>(raster.width * raster.height * 4));

    std::vector<png_bytep> rows(static_cast<std::size_t>(raster.height));
    for (int row = 0; row < raster.height; ++row)
    {
        rows[static_cast<std::size_t>(row)] =
            raster.byte_data.data() + static_cast<std::size_t>(row * raster.width * 4);
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    return raster;
}

float terrain_rgb_to_meters(const std::uint8_t red,
                            const std::uint8_t green,
                            const std::uint8_t blue)
{
    const int encoded =
        static_cast<int>(red) * 256 * 256 + static_cast<int>(green) * 256 + static_cast<int>(blue);
    return -10000.0F + static_cast<float>(encoded) * 0.1F;
}

Raster decode_terrain_rgb(const Raster &image)
{
    validate_raster_shape(image, RasterFormat::UInt8RGBA, 4);
    if (image.byte_data.size() != static_cast<std::size_t>(image.width * image.height * 4))
    {
        throw std::invalid_argument("Invalid RGBA raster for Terrain-RGB decode");
    }

    Raster heights;
    heights.width = image.width;
    heights.height = image.height;
    heights.channels = 1;
    heights.format = RasterFormat::Float32;
    heights.sampling_mode = image.sampling_mode;
    heights.float_data.reserve(static_cast<std::size_t>(heights.width * heights.height));

    for (std::size_t index = 0; index < image.byte_data.size(); index += 4)
    {
        heights.float_data.push_back(terrain_rgb_to_meters(
            image.byte_data[index], image.byte_data[index + 1], image.byte_data[index + 2]));
    }

    return heights;
}

RasterStats float_raster_min_max(const Raster &raster)
{
    validate_raster_shape(raster, RasterFormat::Float32, 1);
    if (raster.float_data.size() != static_cast<std::size_t>(raster.width * raster.height))
    {
        throw std::invalid_argument("Float32 raster data size does not match dimensions");
    }

    RasterStats stats{
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (const float value : raster.float_data)
    {
        if (std::isfinite(value))
        {
            stats.min_value = std::min(stats.min_value, value);
            stats.max_value = std::max(stats.max_value, value);
        }
    }
    if (!std::isfinite(stats.min_value) || !std::isfinite(stats.max_value))
    {
        throw std::invalid_argument("Float32 raster contains no finite values");
    }
    return stats;
}

RasterSample sample_float_raster_bilinear(const Raster &raster, const double u, const double v)
{
    validate_raster_shape(raster, RasterFormat::Float32, 1);
    if (raster.width <= 0 || raster.height <= 0 ||
        raster.float_data.size() != static_cast<std::size_t>(raster.width * raster.height))
    {
        throw std::invalid_argument("Float32 raster data size does not match dimensions");
    }
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
    {
        return {};
    }

    const double x = u * static_cast<double>(raster.width - 1);
    const double y = v * static_cast<double>(raster.height - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, raster.width - 1);
    const int y1 = std::min(y0 + 1, raster.height - 1);
    const double fx = x - static_cast<double>(x0);
    const double fy = y - static_cast<double>(y0);

    const auto at = [&raster](const int col, const int row)
    { return raster.float_data[static_cast<std::size_t>(row * raster.width + col)]; };
    const float h00 = at(x0, y0);
    const float h10 = at(x1, y0);
    const float h01 = at(x0, y1);
    const float h11 = at(x1, y1);
    if (!std::isfinite(h00) || !std::isfinite(h10) || !std::isfinite(h01) || !std::isfinite(h11))
    {
        return {};
    }
    if (raster.no_data_value)
    {
        const float no_data = *raster.no_data_value;
        if (h00 == no_data || h10 == no_data || h01 == no_data || h11 == no_data)
        {
            return {};
        }
    }

    const double top = static_cast<double>(h00) + (static_cast<double>(h10) - h00) * fx;
    const double bottom = static_cast<double>(h01) + (static_cast<double>(h11) - h01) * fx;
    return {true, static_cast<float>(top + (bottom - top) * fy)};
}

TerrainMeshCpu build_terrain_mesh(const Raster &heights, const TerrainMeshOptions &options)
{
    validate_raster_shape(heights, RasterFormat::Float32, 1);
    if (heights.width < 2 || heights.height < 2 ||
        heights.float_data.size() != static_cast<std::size_t>(heights.width * heights.height))
    {
        throw std::invalid_argument("Height raster must be at least 2x2");
    }
    if (options.tile_size <= 0.0F || !std::isfinite(options.tile_size))
    {
        throw std::invalid_argument("Tile size must be positive and finite");
    }
    if (!std::isfinite(options.origin_x) || !std::isfinite(options.origin_z) ||
        !std::isfinite(options.height_scale) || !std::isfinite(options.skirt_depth))
    {
        throw std::invalid_argument("Terrain mesh options must be finite");
    }

    TerrainMeshCpu mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(heights.width * heights.height +
                                                   2 * heights.width + 2 * heights.height));
    mesh.indices.reserve(
        static_cast<std::size_t>((heights.width - 1) * (heights.height - 1) * 6 +
                                 (2 * (heights.width - 1) + 2 * (heights.height - 1)) * 6));

    for (int row = 0; row < heights.height; ++row)
    {
        const float v = static_cast<float>(row) / static_cast<float>(heights.height - 1);
        for (int col = 0; col < heights.width; ++col)
        {
            const float u = static_cast<float>(col) / static_cast<float>(heights.width - 1);
            const float meters =
                heights.float_data[static_cast<std::size_t>(row * heights.width + col)];
            if (!std::isfinite(meters))
            {
                throw std::invalid_argument("Height raster contains non-finite values");
            }
            mesh.vertices.push_back({
                options.origin_x + u * options.tile_size,
                meters * options.height_scale,
                options.origin_z + v * options.tile_size,
                u,
                v,
            });
        }
    }

    for (int row = 0; row < heights.height - 1; ++row)
    {
        for (int col = 0; col < heights.width - 1; ++col)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(row * heights.width + col);
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = static_cast<std::uint32_t>((row + 1) * heights.width + col);
            const std::uint32_t d = c + 1U;
            add_quad(mesh.indices, a, b, c, d);
        }
    }

    auto append_skirt_vertex = [&](const std::uint32_t source)
    {
        TerrainMeshVertex vertex = mesh.vertices[source];
        vertex.y -= options.skirt_depth;
        mesh.vertices.push_back(vertex);
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1U);
    };

    std::vector<std::uint32_t> north;
    std::vector<std::uint32_t> south;
    std::vector<std::uint32_t> west;
    std::vector<std::uint32_t> east;
    north.reserve(static_cast<std::size_t>(heights.width));
    south.reserve(static_cast<std::size_t>(heights.width));
    west.reserve(static_cast<std::size_t>(heights.height));
    east.reserve(static_cast<std::size_t>(heights.height));

    for (int col = 0; col < heights.width; ++col)
    {
        north.push_back(append_skirt_vertex(static_cast<std::uint32_t>(col)));
        south.push_back(append_skirt_vertex(
            static_cast<std::uint32_t>((heights.height - 1) * heights.width + col)));
    }
    for (int row = 0; row < heights.height; ++row)
    {
        west.push_back(append_skirt_vertex(static_cast<std::uint32_t>(row * heights.width)));
        east.push_back(append_skirt_vertex(
            static_cast<std::uint32_t>(row * heights.width + heights.width - 1)));
    }

    for (int col = 0; col < heights.width - 1; ++col)
    {
        const auto top_a = static_cast<std::uint32_t>(col);
        const auto top_b = static_cast<std::uint32_t>(col + 1);
        add_quad(mesh.indices,
                 top_a,
                 top_b,
                 north[static_cast<std::size_t>(col)],
                 north[static_cast<std::size_t>(col + 1)]);

        const auto south_a = static_cast<std::uint32_t>((heights.height - 1) * heights.width + col);
        const auto south_b = south_a + 1U;
        add_quad(mesh.indices,
                 south[static_cast<std::size_t>(col)],
                 south[static_cast<std::size_t>(col + 1)],
                 south_a,
                 south_b);
    }

    for (int row = 0; row < heights.height - 1; ++row)
    {
        const auto west_a = static_cast<std::uint32_t>(row * heights.width);
        const auto west_b = static_cast<std::uint32_t>((row + 1) * heights.width);
        add_quad(mesh.indices,
                 west[static_cast<std::size_t>(row)],
                 west_a,
                 west[static_cast<std::size_t>(row + 1)],
                 west_b);

        const auto east_a = static_cast<std::uint32_t>(row * heights.width + heights.width - 1);
        const auto east_b =
            static_cast<std::uint32_t>((row + 1) * heights.width + heights.width - 1);
        add_quad(mesh.indices,
                 east_a,
                 east[static_cast<std::size_t>(row)],
                 east_b,
                 east[static_cast<std::size_t>(row + 1)]);
    }

    return mesh;
}

} // namespace animus::terrain_core
