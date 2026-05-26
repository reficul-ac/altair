#include "animus/terrain_core/tile_source.hpp"

#include <curl/curl.h>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace animus::terrain_core
{
namespace
{

struct SqliteCloser
{
    void operator()(sqlite3 *db) const
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
    }
};

struct StatementCloser
{
    void operator()(sqlite3_stmt *statement) const
    {
        if (statement != nullptr)
        {
            sqlite3_finalize(statement);
        }
    }
};

using SqliteDb = std::unique_ptr<sqlite3, SqliteCloser>;
using SqliteStatement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

SqliteDb open_readonly_db(const std::filesystem::path &path)
{
    sqlite3 *raw = nullptr;
    const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(path.string().c_str(), &raw, flags, nullptr) != SQLITE_OK)
    {
        std::string message = "Failed to open SQLite database: " + path.string();
        if (raw != nullptr)
        {
            message += ": ";
            message += sqlite3_errmsg(raw);
            sqlite3_close(raw);
        }
        throw std::runtime_error(message);
    }
    return SqliteDb(raw);
}

SqliteStatement prepare(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        throw std::runtime_error(std::string("Failed to prepare SQLite statement: ") +
                                 sqlite3_errmsg(db));
    }
    return SqliteStatement(statement);
}

std::string replace_all(std::string text, const std::string &needle, const std::string &value)
{
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos)
    {
        text.replace(pos, needle.size(), value);
        pos += value.size();
    }
    return text;
}

std::size_t curl_write(void *contents, std::size_t size, std::size_t nmemb, void *userp)
{
    const std::size_t byte_count = size * nmemb;
    auto *bytes = static_cast<std::vector<std::uint8_t> *>(userp);
    const auto *begin = static_cast<const std::uint8_t *>(contents);
    bytes->insert(bytes->end(), begin, begin + byte_count);
    return byte_count;
}

Raster synthetic_raster_for(const LayerSpec &layer, const geo_core::TileCoord coord)
{
    const int resolution = std::max(1, layer.resolution);
    if (layer.type == LayerType::Elevation || layer.type == LayerType::Bathymetry)
    {
        Raster raster;
        raster.width = resolution;
        raster.height = resolution;
        raster.channels = 1;
        raster.format = RasterFormat::Float32;
        raster.float_data.resize(static_cast<std::size_t>(resolution * resolution));
        const float base = static_cast<float>((coord.x + coord.y + coord.z) % 16) * 2.0F;
        std::fill(raster.float_data.begin(), raster.float_data.end(), base);
        return raster;
    }

    Raster raster;
    raster.width = resolution;
    raster.height = resolution;
    raster.channels = 4;
    raster.format = RasterFormat::UInt8RGBA;
    raster.byte_data.resize(static_cast<std::size_t>(resolution * resolution * 4));
    for (int row = 0; row < resolution; ++row)
    {
        for (int col = 0; col < resolution; ++col)
        {
            const auto offset = static_cast<std::size_t>((row * resolution + col) * 4);
            raster.byte_data[offset] = static_cast<std::uint8_t>((coord.x * 37 + col) & 0xFF);
            raster.byte_data[offset + 1U] = static_cast<std::uint8_t>((coord.y * 53 + row) & 0xFF);
            raster.byte_data[offset + 2U] = static_cast<std::uint8_t>((coord.z * 29) & 0xFF);
            raster.byte_data[offset + 3U] = 255U;
        }
    }
    return raster;
}

} // namespace

LocalXyzTileSource::LocalXyzTileSource(std::filesystem::path root,
                                       std::string layer_name,
                                       std::string extension)
    : root_(std::move(root)), layer_name_(std::move(layer_name)), extension_(std::move(extension))
{
}

TileSourceType LocalXyzTileSource::type() const
{
    return TileSourceType::LocalXyz;
}

std::string LocalXyzTileSource::cache_identity() const
{
    return "local_xyz:" + root_.generic_string() + ":" + layer_name_ + ":" + extension_;
}

std::optional<TileSourceResult> LocalXyzTileSource::load_tile(const geo_core::TileCoord coord,
                                                              const LayerSpec &layer) const
{
    const auto path = local_xyz_tile_path(root_, layer_name_, coord, extension_);
    if (!std::filesystem::exists(path))
    {
        return std::nullopt;
    }
    if (layer.type == LayerType::Elevation && extension_ == "png")
    {
        return TileSourceResult{decode_terrain_rgb(load_png_rgba(path)), type(), path.string()};
    }
    if (extension_ == "f32")
    {
        return TileSourceResult{
            load_float32_raster(path, layer.resolution, layer.resolution), type(), path.string()};
    }
    return TileSourceResult{load_png_rgba(path), type(), path.string()};
}

MbtilesTileSource::MbtilesTileSource(std::filesystem::path path, const bool tms_y_flip)
    : path_(std::move(path)), tms_y_flip_(tms_y_flip)
{
}

TileSourceType MbtilesTileSource::type() const
{
    return TileSourceType::Mbtiles;
}

std::string MbtilesTileSource::cache_identity() const
{
    return "mbtiles:" + path_.generic_string();
}

std::optional<std::string> MbtilesTileSource::metadata_value(const std::string &name) const
{
    auto db = open_readonly_db(path_);
    auto statement = prepare(db.get(), "SELECT value FROM metadata WHERE name = ?1 LIMIT 1");
    sqlite3_bind_text(statement.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), 0));
    return text == nullptr ? std::optional<std::string>{} : std::string(text);
}

std::optional<TileSourceResult> MbtilesTileSource::load_tile(const geo_core::TileCoord coord,
                                                             const LayerSpec &) const
{
    auto db = open_readonly_db(path_);
    auto statement = prepare(db.get(),
                             "SELECT tile_data FROM tiles "
                             "WHERE zoom_level = ?1 AND tile_column = ?2 AND tile_row = ?3 "
                             "LIMIT 1");
    const int axis = geo_core::tiles_per_axis(coord.z);
    const int tile_row = tms_y_flip_ ? axis - 1 - coord.y : coord.y;
    sqlite3_bind_int(statement.get(), 1, coord.z);
    sqlite3_bind_int(statement.get(), 2, coord.x);
    sqlite3_bind_int(statement.get(), 3, tile_row);
    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        return std::nullopt;
    }
    const auto *blob = static_cast<const std::uint8_t *>(sqlite3_column_blob(statement.get(), 0));
    const int byte_count = sqlite3_column_bytes(statement.get(), 0);
    if (blob == nullptr || byte_count <= 0)
    {
        return std::nullopt;
    }
    std::span<const std::uint8_t> bytes(blob, static_cast<std::size_t>(byte_count));
    return TileSourceResult{decode_image_rgba(bytes), type(), path_.string()};
}

RemoteHttpTileSource::RemoteHttpTileSource(RemoteHttpTileProvider provider)
    : provider_(std::move(provider))
{
}

TileSourceType RemoteHttpTileSource::type() const
{
    return TileSourceType::RemoteHttp;
}

std::string RemoteHttpTileSource::cache_identity() const
{
    return provider_.cache_identity.empty() ? provider_.url_template : provider_.cache_identity;
}

std::string RemoteHttpTileSource::url_for(const geo_core::TileCoord coord) const
{
    std::string url = provider_.url_template;
    url = replace_all(std::move(url), "{z}", std::to_string(coord.z));
    url = replace_all(std::move(url), "{x}", std::to_string(coord.x));
    url = replace_all(std::move(url), "{y}", std::to_string(coord.y));
    return url;
}

std::optional<TileSourceResult> RemoteHttpTileSource::load_tile(const geo_core::TileCoord coord,
                                                                const LayerSpec &) const
{
    if (provider_.url_template.empty())
    {
        return std::nullopt;
    }

    CURL *curl = curl_easy_init();
    if (curl == nullptr)
    {
        throw std::runtime_error("Failed to initialize libcurl");
    }
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl, curl_easy_cleanup);
    std::vector<std::uint8_t> bytes;
    const std::string url = url_for(coord);
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &bytes);
    curl_easy_setopt(
        handle.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(provider_.timeout.count()));
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, provider_.user_agent.c_str());

    curl_slist *headers = nullptr;
    for (const std::string &header : provider_.headers)
    {
        headers = curl_slist_append(headers, header.c_str());
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(headers,
                                                                            curl_slist_free_all);
    if (headers != nullptr)
    {
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode code = curl_easy_perform(handle.get());
    if (code != CURLE_OK)
    {
        throw std::runtime_error(std::string("Remote tile request failed: ") +
                                 curl_easy_strerror(code));
    }
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status == 404)
    {
        return std::nullopt;
    }
    if (status < 200 || status >= 300)
    {
        throw std::runtime_error("Remote tile HTTP status " + std::to_string(status));
    }
    return TileSourceResult{decode_image_rgba(bytes), type(), url};
}

TileSourceType SyntheticTileSource::type() const
{
    return TileSourceType::Synthetic;
}

std::string SyntheticTileSource::cache_identity() const
{
    return "synthetic";
}

std::optional<TileSourceResult> SyntheticTileSource::load_tile(const geo_core::TileCoord coord,
                                                               const LayerSpec &layer) const
{
    return TileSourceResult{synthetic_raster_for(layer, coord), type(), "synthetic"};
}

} // namespace animus::terrain_core
