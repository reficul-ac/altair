#include "animus/terrain_core/terrain_cache.hpp"

#include <png.h>

#if defined(ANIMUS_TERRAIN_CORE_HAS_GDAL)
#include <gdal_priv.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

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

std::string cache_component(const std::string_view value)
{
    if (value.empty())
    {
        return "_";
    }
    std::string result;
    result.reserve(value.size());
    for (const unsigned char raw : value)
    {
        const char ch = static_cast<char>(raw);
        if (std::isalnum(raw) != 0 || ch == '-' || ch == '_')
        {
            result.push_back(static_cast<char>(std::tolower(raw)));
        }
        else
        {
            result.push_back('_');
        }
    }
    return result;
}

void validate_raster_data(const Raster &raster)
{
    if (raster.width <= 0 || raster.height <= 0 || raster.channels <= 0)
    {
        throw std::invalid_argument("Raster dimensions are invalid");
    }
    const std::size_t pixels = static_cast<std::size_t>(raster.width * raster.height);
    if (raster.format == RasterFormat::Float32)
    {
        if (raster.channels != 1 || raster.float_data.size() != pixels)
        {
            throw std::invalid_argument("Float32 raster shape does not match data");
        }
        return;
    }
    if (raster.format == RasterFormat::UInt8RGBA)
    {
        if (raster.channels != 4 || raster.byte_data.size() != pixels * 4U)
        {
            throw std::invalid_argument("RGBA raster shape does not match data");
        }
        return;
    }
    throw std::invalid_argument("Unsupported raster format");
}

float sample_float(const Raster &source, const float x, const float y)
{
    const float clamped_x = std::clamp(x, 0.0F, static_cast<float>(source.width - 1));
    const float clamped_y = std::clamp(y, 0.0F, static_cast<float>(source.height - 1));
    const int x0 = static_cast<int>(std::floor(clamped_x));
    const int y0 = static_cast<int>(std::floor(clamped_y));
    const int x1 = std::min(x0 + 1, source.width - 1);
    const int y1 = std::min(y0 + 1, source.height - 1);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);
    const auto at = [&](const int row, const int col)
    { return source.float_data[static_cast<std::size_t>(row * source.width + col)]; };
    const float north = at(y0, x0) * (1.0F - tx) + at(y0, x1) * tx;
    const float south = at(y1, x0) * (1.0F - tx) + at(y1, x1) * tx;
    return north * (1.0F - ty) + south * ty;
}

std::uint8_t
sample_u8_channel(const Raster &source, const float x, const float y, const int channel)
{
    const float clamped_x = std::clamp(x, 0.0F, static_cast<float>(source.width - 1));
    const float clamped_y = std::clamp(y, 0.0F, static_cast<float>(source.height - 1));
    const int x0 = static_cast<int>(std::floor(clamped_x));
    const int y0 = static_cast<int>(std::floor(clamped_y));
    const int x1 = std::min(x0 + 1, source.width - 1);
    const int y1 = std::min(y0 + 1, source.height - 1);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);
    const auto at = [&](const int row, const int col)
    {
        const auto offset =
            static_cast<std::size_t>((row * source.width + col) * source.channels + channel);
        return static_cast<float>(source.byte_data[offset]);
    };
    const float north = at(y0, x0) * (1.0F - tx) + at(y0, x1) * tx;
    const float south = at(y1, x0) * (1.0F - tx) + at(y1, x1) * tx;
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(north * (1.0F - ty) + south * ty), 0L, 255L));
}

Raster crop_resample_raster(const Raster &source,
                            const float u0,
                            const float v0,
                            const float u1,
                            const float v1,
                            const int width,
                            const int height)
{
    validate_raster_data(source);
    Raster output;
    output.width = width;
    output.height = height;
    output.channels = source.channels;
    output.format = source.format;
    output.sampling_mode = source.sampling_mode;
    output.no_data_value = source.no_data_value;

    const float src_x0 = u0 * static_cast<float>(source.width - 1);
    const float src_y0 = v0 * static_cast<float>(source.height - 1);
    const float src_x1 = u1 * static_cast<float>(source.width - 1);
    const float src_y1 = v1 * static_cast<float>(source.height - 1);

    if (source.format == RasterFormat::Float32)
    {
        output.float_data.resize(static_cast<std::size_t>(width * height));
        for (int row = 0; row < height; ++row)
        {
            const float v =
                height == 1 ? 0.0F : static_cast<float>(row) / static_cast<float>(height - 1);
            for (int col = 0; col < width; ++col)
            {
                const float u =
                    width == 1 ? 0.0F : static_cast<float>(col) / static_cast<float>(width - 1);
                output.float_data[static_cast<std::size_t>(row * width + col)] = sample_float(
                    source, src_x0 + (src_x1 - src_x0) * u, src_y0 + (src_y1 - src_y0) * v);
            }
        }
        return output;
    }

    output.byte_data.resize(static_cast<std::size_t>(width * height * source.channels));
    for (int row = 0; row < height; ++row)
    {
        const float v =
            height == 1 ? 0.0F : static_cast<float>(row) / static_cast<float>(height - 1);
        for (int col = 0; col < width; ++col)
        {
            const float u =
                width == 1 ? 0.0F : static_cast<float>(col) / static_cast<float>(width - 1);
            for (int channel = 0; channel < source.channels; ++channel)
            {
                output.byte_data[static_cast<std::size_t>((row * width + col) * source.channels +
                                                          channel)] =
                    sample_u8_channel(source,
                                      src_x0 + (src_x1 - src_x0) * u,
                                      src_y0 + (src_y1 - src_y0) * v,
                                      channel);
            }
        }
    }
    return output;
}

std::optional<std::string> read_text_file(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
    {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::optional<std::string_view> field_value_after(const std::string_view text,
                                                  const std::string_view field)
{
    const std::string quoted_field = "\"" + std::string(field) + "\"";
    const std::size_t field_pos = text.find(quoted_field);
    if (field_pos == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::size_t colon_pos = text.find(':', field_pos + quoted_field.size());
    if (colon_pos == std::string_view::npos)
    {
        return std::nullopt;
    }
    return text.substr(colon_pos + 1);
}

bool parse_json_bool_field(const std::string_view text, const std::string_view field)
{
    const auto value = field_value_after(text, field);
    if (!value)
    {
        return false;
    }
    const std::size_t first = value->find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && value->substr(first, 4) == "true";
}

int parse_json_int_field(const std::string_view text, const std::string_view field)
{
    const auto value = field_value_after(text, field);
    if (!value)
    {
        return 0;
    }
    const std::size_t first = value->find_first_of("-0123456789");
    if (first == std::string_view::npos)
    {
        return 0;
    }
    try
    {
        return std::stoi(std::string(value->substr(first)));
    }
    catch (const std::exception &)
    {
        return 0;
    }
}

std::vector<geo_core::TileCoord> parse_source_coords(const std::string_view text)
{
    std::vector<geo_core::TileCoord> coords;
    const auto value = field_value_after(text, "source_coords");
    if (!value)
    {
        return coords;
    }
    const std::size_t array_end = value->find(']');
    if (array_end == std::string_view::npos)
    {
        return coords;
    }
    const std::string_view source_array = value->substr(0, array_end);
    std::size_t cursor = 0;
    while (true)
    {
        const std::size_t z_pos = source_array.find("\"z\"", cursor);
        if (z_pos == std::string_view::npos)
        {
            break;
        }
        const std::size_t x_pos = source_array.find("\"x\"", z_pos);
        const std::size_t y_pos = source_array.find("\"y\"", x_pos);
        if (x_pos == std::string_view::npos || y_pos == std::string_view::npos)
        {
            break;
        }
        const std::size_t object_end = source_array.find('}', y_pos);
        if (object_end == std::string_view::npos)
        {
            break;
        }
        const std::string_view chunk = source_array.substr(z_pos, object_end - z_pos + 1U);
        coords.push_back({
            parse_json_int_field(chunk, "z"),
            parse_json_int_field(chunk, "x"),
            parse_json_int_field(chunk, "y"),
        });
        cursor = object_end + 1U;
    }
    return coords;
}

PersistedTileInfo load_l3_sidecar(const std::filesystem::path &path)
{
    PersistedTileInfo info;
    const auto text = read_text_file(path);
    if (!text)
    {
        return info;
    }
    info.synthetic = parse_json_bool_field(*text, "synthetic");
    info.synthesis_depth = parse_json_int_field(*text, "synthesis_depth");
    info.source_coords = parse_source_coords(*text);
    return info;
}

} // namespace

std::string cache_layer_path(const LayerSpec &layer)
{
    std::ostringstream stream;
    stream << to_string(layer.type) << '/' << cache_component(layer.source) << '/'
           << cache_component(layer.style) << '/' << cache_component(layer.extra) << '/'
           << layer.resolution << '/' << layer.min_zoom << '-' << layer.max_zoom;
    return stream.str();
}

std::string cache_key_string(const TileCacheKey &key)
{
    return cache_layer_path(key.layer) + '/' + geo_core::tile_key(key.coord);
}

std::filesystem::path l3_tile_data_path(const std::filesystem::path &cache_root,
                                        const TileCacheKey &key,
                                        const RasterFormat format)
{
    const std::string extension = format == RasterFormat::UInt8RGBA ? ".png" : ".f32";
    return cache_root / cache_layer_path(key.layer) / std::to_string(key.coord.z) /
           std::to_string(key.coord.x) / (std::to_string(key.coord.y) + extension);
}

std::filesystem::path l3_tile_sidecar_path(const std::filesystem::path &cache_root,
                                           const TileCacheKey &key)
{
    return cache_root / cache_layer_path(key.layer) / std::to_string(key.coord.z) /
           std::to_string(key.coord.x) / (std::to_string(key.coord.y) + ".json");
}

std::string_view to_string(const CacheTier tier)
{
    switch (tier)
    {
    case CacheTier::None:
        return "none";
    case CacheTier::L0Gpu:
        return "l0-gpu";
    case CacheTier::L1Prepared:
        return "l1-prepared";
    case CacheTier::L2Raster:
        return "l2-raster";
    case CacheTier::L3Disk:
        return "l3-disk";
    case CacheTier::LocalXyz:
        return "local-xyz";
    case CacheTier::GdalGeoTiff:
        return "gdal-geotiff";
    case CacheTier::Synthesis:
        return "synthesis";
    }
    return "unknown";
}

std::string_view to_string(const TileSourceType source_type)
{
    switch (source_type)
    {
    case TileSourceType::None:
        return "none";
    case TileSourceType::LocalXyz:
        return "local-xyz";
    case TileSourceType::DiskCache:
        return "disk-cache";
    case TileSourceType::GeoTiff:
        return "geotiff";
    case TileSourceType::Mbtiles:
        return "mbtiles";
    case TileSourceType::RemoteHttp:
        return "remote-http";
    case TileSourceType::Synthetic:
        return "synthetic";
    }
    return "unknown";
}

std::size_t estimate_raster_bytes(const Raster &raster)
{
    return raster.byte_data.size() + raster.float_data.size() * sizeof(float);
}

std::size_t estimate_mesh_bytes(const TerrainMeshCpu &mesh)
{
    return mesh.vertices.size() * sizeof(TerrainMeshVertex) +
           mesh.indices.size() * sizeof(std::uint32_t);
}

Raster load_float32_raster(const std::filesystem::path &path, const int width, const int height)
{
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Float32 raster dimensions must be positive");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open Float32 raster: " + path.string());
    }
    Raster raster;
    raster.width = width;
    raster.height = height;
    raster.channels = 1;
    raster.format = RasterFormat::Float32;
    raster.float_data.resize(static_cast<std::size_t>(width * height));
    input.read(reinterpret_cast<char *>(raster.float_data.data()),
               static_cast<std::streamsize>(raster.float_data.size() * sizeof(float)));
    if (!input)
    {
        throw std::runtime_error("Failed to read Float32 raster: " + path.string());
    }
    return raster;
}

void save_float32_raster(const std::filesystem::path &path, const Raster &raster)
{
    validate_raster_data(raster);
    if (raster.format != RasterFormat::Float32)
    {
        throw std::invalid_argument("Expected Float32 raster for .f32 persistence");
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        throw std::runtime_error("Failed to open Float32 output: " + path.string());
    }
    output.write(reinterpret_cast<const char *>(raster.float_data.data()),
                 static_cast<std::streamsize>(raster.float_data.size() * sizeof(float)));
}

void save_png_rgba(const std::filesystem::path &path, const Raster &raster)
{
    validate_raster_data(raster);
    if (raster.format != RasterFormat::UInt8RGBA)
    {
        throw std::invalid_argument("Expected RGBA raster for PNG persistence");
    }
    std::filesystem::create_directories(path.parent_path());
    std::unique_ptr<std::FILE, FileCloser> file(std::fopen(path.string().c_str(), "wb"));
    if (file == nullptr)
    {
        throw std::runtime_error("Failed to open PNG output: " + path.string());
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr)
    {
        throw std::runtime_error("png_create_write_struct failed");
    }
    png_infop info = png_create_info_struct(png);
    if (info == nullptr)
    {
        png_destroy_write_struct(&png, nullptr);
        throw std::runtime_error("png_create_info_struct failed");
    }
    if (setjmp(png_jmpbuf(png)) != 0)
    {
        png_destroy_write_struct(&png, &info);
        throw std::runtime_error("Failed to encode PNG: " + path.string());
    }
    png_init_io(png, file.get());
    png_set_IHDR(png,
                 info,
                 static_cast<png_uint_32>(raster.width),
                 static_cast<png_uint_32>(raster.height),
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_bytep> rows(static_cast<std::size_t>(raster.height));
    for (int row = 0; row < raster.height; ++row)
    {
        rows[static_cast<std::size_t>(row)] = const_cast<png_bytep>(
            raster.byte_data.data() + static_cast<std::size_t>(row * raster.width * 4));
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
}

std::optional<RasterCacheEntry> load_l3_raster(const std::filesystem::path &cache_root,
                                               const TileCacheKey &key)
{
    const auto sidecar = l3_tile_sidecar_path(cache_root, key);
    const PersistedTileInfo persisted = load_l3_sidecar(sidecar);
    const auto png_path = l3_tile_data_path(cache_root, key, RasterFormat::UInt8RGBA);
    if (std::filesystem::exists(png_path))
    {
        return RasterCacheEntry{load_png_rgba(png_path),
                                CacheTier::L3Disk,
                                TileSourceType::DiskCache,
                                persisted.synthetic,
                                persisted.synthesis_depth,
                                persisted.source_coords};
    }
    const auto f32_path = l3_tile_data_path(cache_root, key, RasterFormat::Float32);
    if (std::filesystem::exists(f32_path))
    {
        return RasterCacheEntry{
            load_float32_raster(f32_path, key.layer.resolution, key.layer.resolution),
            CacheTier::L3Disk,
            TileSourceType::DiskCache,
            persisted.synthetic,
            persisted.synthesis_depth,
            persisted.source_coords,
        };
    }
    return std::nullopt;
}

void store_l3_raster(const std::filesystem::path &cache_root,
                     const TileCacheKey &key,
                     const Raster &raster,
                     const PersistedTileInfo &info)
{
    validate_raster_data(raster);
    const auto data_path = l3_tile_data_path(cache_root, key, raster.format);
    if (raster.format == RasterFormat::UInt8RGBA)
    {
        save_png_rgba(data_path, raster);
    }
    else
    {
        save_float32_raster(data_path, raster);
    }

    const auto sidecar = l3_tile_sidecar_path(cache_root, key);
    std::filesystem::create_directories(sidecar.parent_path());
    std::ofstream output(sidecar);
    if (!output)
    {
        throw std::runtime_error("Failed to open L3 sidecar: " + sidecar.string());
    }
    output << "{\n"
           << "  \"schema\": 1,\n"
           << "  \"layer\": \"" << cache_layer_path(key.layer) << "\",\n"
           << "  \"coord\": {\"z\": " << key.coord.z << ", \"x\": " << key.coord.x
           << ", \"y\": " << key.coord.y << "},\n"
           << "  \"format\": \"" << to_string(raster.format) << "\",\n"
           << "  \"synthetic\": " << (info.synthetic ? "true" : "false") << ",\n"
           << "  \"synthesis_depth\": " << info.synthesis_depth << ",\n"
           << "  \"min\": " << info.min_value << ",\n"
           << "  \"max\": " << info.max_value << ",\n"
           << "  \"source_coords\": [";
    for (std::size_t index = 0; index < info.source_coords.size(); ++index)
    {
        const auto coord = info.source_coords[index];
        output << (index == 0 ? "" : ", ") << "{\"z\": " << coord.z << ", \"x\": " << coord.x
               << ", \"y\": " << coord.y << "}";
    }
    output << "]\n}\n";
}

Raster resample_raster_bilinear(const Raster &source, const int width, const int height)
{
    return crop_resample_raster(source, 0.0F, 0.0F, 1.0F, 1.0F, width, height);
}

Raster synthesize_child_from_parent(const Raster &parent,
                                    const geo_core::TileCoord parent_coord,
                                    const geo_core::TileCoord child_coord)
{
    const auto expected_children = geo_core::children(parent_coord);
    const auto it = std::find(expected_children.begin(), expected_children.end(), child_coord);
    if (it == expected_children.end())
    {
        throw std::invalid_argument("Requested child is not a child of parent tile");
    }
    const bool east = (child_coord.x % 2) != 0;
    const bool south = (child_coord.y % 2) != 0;
    const float u0 = east ? 0.5F : 0.0F;
    const float v0 = south ? 0.5F : 0.0F;
    return crop_resample_raster(parent, u0, v0, u0 + 0.5F, v0 + 0.5F, parent.width, parent.height);
}

Raster synthesize_parent_from_children(const std::array<Raster, 4> &children)
{
    validate_raster_data(children[0]);
    const int width = children[0].width;
    const int height = children[0].height;
    const int channels = children[0].channels;
    const RasterFormat format = children[0].format;
    for (const Raster &child : children)
    {
        validate_raster_data(child);
        if (child.width != width || child.height != height || child.channels != channels ||
            child.format != format)
        {
            throw std::invalid_argument("Child rasters must share shape and format");
        }
    }

    Raster mosaic;
    mosaic.width = width * 2;
    mosaic.height = height * 2;
    mosaic.channels = channels;
    mosaic.format = format;
    mosaic.sampling_mode = children[0].sampling_mode;
    mosaic.no_data_value = children[0].no_data_value;
    if (format == RasterFormat::Float32)
    {
        mosaic.float_data.resize(static_cast<std::size_t>(mosaic.width * mosaic.height));
    }
    else
    {
        mosaic.byte_data.resize(static_cast<std::size_t>(mosaic.width * mosaic.height * channels));
    }

    for (int child_index = 0; child_index < 4; ++child_index)
    {
        const int x_offset = (child_index % 2) * width;
        const int y_offset = (child_index / 2) * height;
        for (int row = 0; row < height; ++row)
        {
            for (int col = 0; col < width; ++col)
            {
                const int dst_col = x_offset + col;
                const int dst_row = y_offset + row;
                if (format == RasterFormat::Float32)
                {
                    mosaic.float_data[static_cast<std::size_t>(dst_row * mosaic.width + dst_col)] =
                        children[static_cast<std::size_t>(child_index)]
                            .float_data[static_cast<std::size_t>(row * width + col)];
                }
                else
                {
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        mosaic.byte_data[static_cast<std::size_t>(
                            (dst_row * mosaic.width + dst_col) * channels + channel)] =
                            children[static_cast<std::size_t>(child_index)]
                                .byte_data[static_cast<std::size_t>((row * width + col) * channels +
                                                                    channel)];
                    }
                }
            }
        }
    }
    return resample_raster_bilinear(mosaic, width, height);
}

Raster merge_elevation_bathymetry(const Raster &elevation, const std::optional<Raster> &bathymetry)
{
    validate_raster_data(elevation);
    if (elevation.format != RasterFormat::Float32)
    {
        throw std::invalid_argument("Elevation merge requires Float32 elevation");
    }
    if (bathymetry)
    {
        validate_raster_data(*bathymetry);
        if (bathymetry->format != RasterFormat::Float32 || bathymetry->width != elevation.width ||
            bathymetry->height != elevation.height)
        {
            throw std::invalid_argument("Bathymetry merge requires matching Float32 rasters");
        }
    }

    Raster merged = elevation;
    for (std::size_t index = 0; index < elevation.float_data.size(); ++index)
    {
        const float elev = elevation.float_data[index];
        if (!std::isfinite(elev))
        {
            merged.float_data[index] = bathymetry ? bathymetry->float_data[index] : elev;
            continue;
        }
        if (elev > 0.0F || !bathymetry)
        {
            merged.float_data[index] = elev;
            continue;
        }
        const float bathy = bathymetry->float_data[index];
        const bool bathy_is_no_data =
            bathymetry->no_data_value && bathy == *bathymetry->no_data_value;
        merged.float_data[index] = std::isfinite(bathy) && !bathy_is_no_data ? bathy : elev;
    }
    return merged;
}

RasterLruCache::RasterLruCache(const std::size_t byte_limit) : byte_limit_(byte_limit)
{
}

void RasterLruCache::set_byte_limit(const std::size_t byte_limit)
{
    byte_limit_ = byte_limit;
    evict_to_limit();
}

std::optional<RasterCacheEntry> RasterLruCache::get(const std::string &key)
{
    const auto it = index_.find(key);
    if (it == index_.end())
    {
        ++counters_.misses;
        return std::nullopt;
    }
    lru_.splice(lru_.begin(), lru_, it->second);
    ++counters_.hits;
    return it->second->value;
}

void RasterLruCache::put(std::string key, RasterCacheEntry entry)
{
    const std::size_t bytes = estimate_raster_bytes(entry.raster);
    if (const auto it = index_.find(key); it != index_.end())
    {
        bytes_ -= it->second->bytes;
        lru_.erase(it->second);
        index_.erase(it);
    }
    lru_.push_front(Entry{std::move(key), std::move(entry), bytes});
    index_[lru_.front().key] = lru_.begin();
    bytes_ += bytes;
    ++counters_.stores;
    evict_to_limit();
}

CacheStats RasterLruCache::stats() const
{
    return CacheStats{lru_.size(), bytes_, byte_limit_, counters_};
}

void RasterLruCache::evict_to_limit()
{
    if (byte_limit_ == 0)
    {
        return;
    }
    while (bytes_ > byte_limit_ && !lru_.empty())
    {
        auto last = std::prev(lru_.end());
        bytes_ -= last->bytes;
        index_.erase(last->key);
        lru_.erase(last);
        ++counters_.evictions;
    }
}

GdalGeoTiffTileSource::GdalGeoTiffTileSource(std::filesystem::path path) : path_(std::move(path))
{
}

bool GdalGeoTiffTileSource::available() const
{
    return !path_.empty();
}

Raster GdalGeoTiffTileSource::load_tile(const geo_core::TileCoord coord, const int resolution) const
{
    if (path_.empty())
    {
        throw std::runtime_error("GeoTIFF path is empty");
    }
#if defined(ANIMUS_TERRAIN_CORE_HAS_GDAL)
    GDALAllRegister();
    GDALDatasetUniquePtr dataset(static_cast<GDALDataset *>(
        GDALOpenEx(path_.string().c_str(), GDAL_OF_RASTER, nullptr, nullptr, nullptr)));
    if (!dataset)
    {
        throw std::runtime_error("Failed to open GeoTIFF: " + path_.string());
    }
    GDALRasterBand *band = dataset->GetRasterBand(1);
    if (band == nullptr)
    {
        throw std::runtime_error("GeoTIFF has no raster band: " + path_.string());
    }

    double transform[6] = {};
    int src_x = 0;
    int src_y = 0;
    int src_width = dataset->GetRasterXSize();
    int src_height = dataset->GetRasterYSize();
    if (dataset->GetGeoTransform(transform) == CE_None && transform[1] != 0.0 &&
        transform[5] != 0.0)
    {
        const auto bounds = geo_core::tile_to_bounds(coord);
        const double x0 = (bounds.west_deg - transform[0]) / transform[1];
        const double x1 = (bounds.east_deg - transform[0]) / transform[1];
        const double y0 = (bounds.north_deg - transform[3]) / transform[5];
        const double y1 = (bounds.south_deg - transform[3]) / transform[5];
        src_x = std::clamp(
            static_cast<int>(std::floor(std::min(x0, x1))), 0, dataset->GetRasterXSize() - 1);
        src_y = std::clamp(
            static_cast<int>(std::floor(std::min(y0, y1))), 0, dataset->GetRasterYSize() - 1);
        const int x_end = std::clamp(
            static_cast<int>(std::ceil(std::max(x0, x1))), src_x + 1, dataset->GetRasterXSize());
        const int y_end = std::clamp(
            static_cast<int>(std::ceil(std::max(y0, y1))), src_y + 1, dataset->GetRasterYSize());
        src_width = x_end - src_x;
        src_height = y_end - src_y;
    }

    Raster raster;
    raster.width = resolution;
    raster.height = resolution;
    raster.channels = 1;
    raster.format = RasterFormat::Float32;
    raster.float_data.resize(static_cast<std::size_t>(resolution * resolution));
    if (band->RasterIO(GF_Read,
                       src_x,
                       src_y,
                       src_width,
                       src_height,
                       raster.float_data.data(),
                       resolution,
                       resolution,
                       GDT_Float32,
                       0,
                       0) != CE_None)
    {
        throw std::runtime_error("Failed to read GeoTIFF tile: " + path_.string());
    }
    int has_no_data = 0;
    const double no_data = band->GetNoDataValue(&has_no_data);
    if (has_no_data != 0)
    {
        raster.no_data_value = static_cast<float>(no_data);
    }
    return raster;
#else
    (void)coord;
    (void)resolution;
    throw std::runtime_error("terrain_core was built without GDAL support");
#endif
}

Raster GdalGeoTiffTileSource::load_tile_rgba(const geo_core::TileCoord coord,
                                             const int resolution) const
{
    if (path_.empty())
    {
        throw std::runtime_error("GeoTIFF path is empty");
    }
#if defined(ANIMUS_TERRAIN_CORE_HAS_GDAL)
    GDALAllRegister();
    GDALDatasetUniquePtr dataset(static_cast<GDALDataset *>(
        GDALOpenEx(path_.string().c_str(), GDAL_OF_RASTER, nullptr, nullptr, nullptr)));
    if (!dataset)
    {
        throw std::runtime_error("Failed to open GeoTIFF: " + path_.string());
    }

    double transform[6] = {};
    int src_x = 0;
    int src_y = 0;
    int src_width = dataset->GetRasterXSize();
    int src_height = dataset->GetRasterYSize();
    if (dataset->GetGeoTransform(transform) == CE_None && transform[1] != 0.0 &&
        transform[5] != 0.0)
    {
        const auto bounds = geo_core::tile_to_bounds(coord);
        const double x0 = (bounds.west_deg - transform[0]) / transform[1];
        const double x1 = (bounds.east_deg - transform[0]) / transform[1];
        const double y0 = (bounds.north_deg - transform[3]) / transform[5];
        const double y1 = (bounds.south_deg - transform[3]) / transform[5];
        src_x = std::clamp(
            static_cast<int>(std::floor(std::min(x0, x1))), 0, dataset->GetRasterXSize() - 1);
        src_y = std::clamp(
            static_cast<int>(std::floor(std::min(y0, y1))), 0, dataset->GetRasterYSize() - 1);
        const int x_end = std::clamp(
            static_cast<int>(std::ceil(std::max(x0, x1))), src_x + 1, dataset->GetRasterXSize());
        const int y_end = std::clamp(
            static_cast<int>(std::ceil(std::max(y0, y1))), src_y + 1, dataset->GetRasterYSize());
        src_width = x_end - src_x;
        src_height = y_end - src_y;
    }

    const int band_count = dataset->GetRasterCount();
    if (band_count <= 0)
    {
        throw std::runtime_error("GeoTIFF has no raster bands: " + path_.string());
    }
    Raster raster;
    raster.width = resolution;
    raster.height = resolution;
    raster.channels = 4;
    raster.format = RasterFormat::UInt8RGBA;
    raster.byte_data.resize(static_cast<std::size_t>(resolution * resolution * 4), 255U);

    const int read_bands = std::min(4, band_count);
    for (int band_index = 1; band_index <= read_bands; ++band_index)
    {
        GDALRasterBand *band = dataset->GetRasterBand(band_index);
        if (band == nullptr)
        {
            continue;
        }
        std::vector<std::uint8_t> channel(static_cast<std::size_t>(resolution * resolution));
        if (band->RasterIO(GF_Read,
                           src_x,
                           src_y,
                           src_width,
                           src_height,
                           channel.data(),
                           resolution,
                           resolution,
                           GDT_Byte,
                           0,
                           0) != CE_None)
        {
            throw std::runtime_error("Failed to read GeoTIFF overlay: " + path_.string());
        }
        const int channel_index = read_bands == 1 ? 0 : band_index - 1;
        for (int pixel = 0; pixel < resolution * resolution; ++pixel)
        {
            raster.byte_data[static_cast<std::size_t>(pixel * 4 + channel_index)] =
                channel[static_cast<std::size_t>(pixel)];
        }
    }
    if (read_bands == 1)
    {
        for (int pixel = 0; pixel < resolution * resolution; ++pixel)
        {
            const std::uint8_t value = raster.byte_data[static_cast<std::size_t>(pixel * 4)];
            raster.byte_data[static_cast<std::size_t>(pixel * 4 + 1)] = value;
            raster.byte_data[static_cast<std::size_t>(pixel * 4 + 2)] = value;
        }
    }
    return raster;
#else
    (void)coord;
    (void)resolution;
    throw std::runtime_error("terrain_core was built without GDAL support");
#endif
}

} // namespace animus::terrain_core
