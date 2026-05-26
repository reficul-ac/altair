#include "animus/terrain_lab/terrain_data.hpp"

#include <png.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace animus::terrain_lab {
namespace {

struct FileCloser {
    void operator()(std::FILE* file) const
    {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

void add_quad(
    std::vector<std::uint32_t>& indices,
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c,
    std::uint32_t d)
{
    indices.push_back(a);
    indices.push_back(c);
    indices.push_back(b);
    indices.push_back(b);
    indices.push_back(c);
    indices.push_back(d);
}

} // namespace

std::filesystem::path tile_png_path(
    const std::filesystem::path& pack_root,
    const std::string& layer,
    const XyzTile& tile)
{
    return pack_root / layer / std::to_string(tile.z) / std::to_string(tile.x) /
           (std::to_string(tile.y) + ".png");
}

float terrain_rgb_to_meters(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    const int encoded = static_cast<int>(red) * 256 * 256 + static_cast<int>(green) * 256 +
                        static_cast<int>(blue);
    return -10000.0F + static_cast<float>(encoded) * 0.1F;
}

RgbaImage load_png_rgba(const std::filesystem::path& path)
{
    std::unique_ptr<std::FILE, FileCloser> file(std::fopen(path.string().c_str(), "rb"));
    if (file == nullptr) {
        throw std::runtime_error("Failed to open PNG: " + path.string());
    }

    png_byte signature[8] = {};
    if (std::fread(signature, 1, sizeof(signature), file.get()) != sizeof(signature) ||
        png_sig_cmp(signature, 0, sizeof(signature)) != 0) {
        throw std::runtime_error("File is not a PNG: " + path.string());
    }

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        throw std::runtime_error("png_create_read_struct failed");
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        throw std::runtime_error("png_create_info_struct failed");
    }

    if (setjmp(png_jmpbuf(png)) != 0) {
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

    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if ((color_type & PNG_COLOR_MASK_ALPHA) == 0) {
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    RgbaImage image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels.resize(static_cast<std::size_t>(image.width * image.height * 4));

    std::vector<png_bytep> rows(static_cast<std::size_t>(image.height));
    for (int row = 0; row < image.height; ++row) {
        rows[static_cast<std::size_t>(row)] =
            image.pixels.data() + static_cast<std::size_t>(row * image.width * 4);
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    return image;
}

HeightGrid decode_terrain_rgb(const RgbaImage& image)
{
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width * image.height * 4)) {
        throw std::invalid_argument("Invalid RGBA image for Terrain-RGB decode");
    }

    HeightGrid grid;
    grid.width = image.width;
    grid.height = image.height;
    grid.meters.reserve(static_cast<std::size_t>(grid.width * grid.height));

    for (std::size_t index = 0; index < image.pixels.size(); index += 4) {
        const float meters = terrain_rgb_to_meters(
            image.pixels[index],
            image.pixels[index + 1],
            image.pixels[index + 2]);
        grid.meters.push_back(meters);
        grid.min_meters = std::min(grid.min_meters, meters);
        grid.max_meters = std::max(grid.max_meters, meters);
    }

    return grid;
}

TerrainMeshCpu build_tile_mesh(
    const HeightGrid& heights,
    float origin_x,
    float origin_z,
    float tile_size,
    float height_scale,
    float skirt_depth)
{
    if (heights.width < 2 || heights.height < 2 ||
        heights.meters.size() != static_cast<std::size_t>(heights.width * heights.height)) {
        throw std::invalid_argument("Height grid must be at least 2x2");
    }
    if (tile_size <= 0.0F) {
        throw std::invalid_argument("Tile size must be positive");
    }

    TerrainMeshCpu mesh;
    mesh.vertices.reserve(
        static_cast<std::size_t>(heights.width * heights.height + 2 * heights.width +
                                 2 * heights.height));
    mesh.indices.reserve(
        static_cast<std::size_t>(
            (heights.width - 1) * (heights.height - 1) * 6 +
            (2 * (heights.width - 1) + 2 * (heights.height - 1)) * 6));

    for (int row = 0; row < heights.height; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(heights.height - 1);
        for (int col = 0; col < heights.width; ++col) {
            const float u = static_cast<float>(col) / static_cast<float>(heights.width - 1);
            const float meters =
                heights.meters[static_cast<std::size_t>(row * heights.width + col)];
            mesh.vertices.push_back({
                origin_x + u * tile_size,
                meters * height_scale,
                origin_z + v * tile_size,
                u,
                v,
            });
        }
    }

    for (int row = 0; row < heights.height - 1; ++row) {
        for (int col = 0; col < heights.width - 1; ++col) {
            const std::uint32_t a = static_cast<std::uint32_t>(row * heights.width + col);
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = static_cast<std::uint32_t>((row + 1) * heights.width + col);
            const std::uint32_t d = c + 1U;
            add_quad(mesh.indices, a, b, c, d);
        }
    }

    auto append_skirt_vertex = [&](std::uint32_t source) {
        render_core::TerrainVertex vertex = mesh.vertices[source];
        vertex.y -= skirt_depth;
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

    for (int col = 0; col < heights.width; ++col) {
        north.push_back(append_skirt_vertex(static_cast<std::uint32_t>(col)));
        south.push_back(append_skirt_vertex(
            static_cast<std::uint32_t>((heights.height - 1) * heights.width + col)));
    }
    for (int row = 0; row < heights.height; ++row) {
        west.push_back(append_skirt_vertex(static_cast<std::uint32_t>(row * heights.width)));
        east.push_back(append_skirt_vertex(
            static_cast<std::uint32_t>(row * heights.width + heights.width - 1)));
    }

    for (int col = 0; col < heights.width - 1; ++col) {
        const auto top_a = static_cast<std::uint32_t>(col);
        const auto top_b = static_cast<std::uint32_t>(col + 1);
        add_quad(mesh.indices, top_a, top_b, north[static_cast<std::size_t>(col)],
                 north[static_cast<std::size_t>(col + 1)]);

        const auto south_a =
            static_cast<std::uint32_t>((heights.height - 1) * heights.width + col);
        const auto south_b = south_a + 1U;
        add_quad(
            mesh.indices,
            south[static_cast<std::size_t>(col)],
            south[static_cast<std::size_t>(col + 1)],
            south_a,
            south_b);
    }

    for (int row = 0; row < heights.height - 1; ++row) {
        const auto west_a = static_cast<std::uint32_t>(row * heights.width);
        const auto west_b = static_cast<std::uint32_t>((row + 1) * heights.width);
        add_quad(mesh.indices, west[static_cast<std::size_t>(row)], west_a,
                 west[static_cast<std::size_t>(row + 1)], west_b);

        const auto east_a =
            static_cast<std::uint32_t>(row * heights.width + heights.width - 1);
        const auto east_b =
            static_cast<std::uint32_t>((row + 1) * heights.width + heights.width - 1);
        add_quad(mesh.indices, east_a, east[static_cast<std::size_t>(row)],
                 east_b, east[static_cast<std::size_t>(row + 1)]);
    }

    return mesh;
}

} // namespace animus::terrain_lab
