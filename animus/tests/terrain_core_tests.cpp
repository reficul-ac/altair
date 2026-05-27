#include "animus/terrain_core/terrain_data.hpp"
#include "animus/terrain_core/cache_metadata.hpp"
#include "animus/terrain_core/datum.hpp"
#include "animus/terrain_core/terrain_cache.hpp"
#include "animus/terrain_core/terrain_stream.hpp"
#include "animus/terrain_core/tile_source.hpp"

#include <sqlite3.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(ANIMUS_TESTS_HAVE_GDAL)
#include <gdal_priv.h>
#endif

#include <gtest/gtest.h>

namespace
{

using animus::geo_core::TileCoord;
using animus::terrain_core::build_terrain_mesh;
using animus::terrain_core::cache_key_string;
using animus::terrain_core::CacheMetadataRecord;
using animus::terrain_core::CacheMetadataStore;
using animus::terrain_core::decode_terrain_rgb;
using animus::terrain_core::float_raster_min_max;
using animus::terrain_core::GdalGeoTiffTileSource;
using animus::terrain_core::GeoidCorrectionGrid;
using animus::terrain_core::GeoidGrid;
using animus::terrain_core::height_above_terrain_m;
using animus::terrain_core::load_l3_raster;
using animus::terrain_core::load_png_rgba;
using animus::terrain_core::local_xyz_tile_path;
using animus::terrain_core::MbtilesTileSource;
using animus::terrain_core::merge_elevation_bathymetry;
using animus::terrain_core::PersistedTileInfo;
using animus::terrain_core::PreparedTile;
using animus::terrain_core::Raster;
using animus::terrain_core::RasterCacheEntry;
using animus::terrain_core::RasterFormat;
using animus::terrain_core::RasterLruCache;
using animus::terrain_core::RemoteHttpTileProvider;
using animus::terrain_core::RemoteHttpTileSource;
using animus::terrain_core::sample_float_raster_bilinear;
using animus::terrain_core::sample_geoid_grid;
using animus::terrain_core::save_png_rgba;
using animus::terrain_core::store_l3_raster;
using animus::terrain_core::synthesize_child_from_parent;
using animus::terrain_core::synthesize_parent_from_children;
using animus::terrain_core::terrain_rgb_to_meters;
using animus::terrain_core::TerrainMeshOptions;
using animus::terrain_core::TerrainStreamConfig;
using animus::terrain_core::TerrainStreamer;
using animus::terrain_core::TileCacheKey;
using animus::terrain_core::TileLoadRequest;
using animus::terrain_core::TileSourceType;
using animus::terrain_core::TileState;

struct FdCloser
{
    void operator()(int *fd) const
    {
        if (fd != nullptr && *fd >= 0)
        {
            close(*fd);
        }
        delete fd;
    }
};

using UniqueFd = std::unique_ptr<int, FdCloser>;

struct OneShotHttpServer
{
    std::string url;
    std::jthread thread;
};

Raster rgba_raster(int width, int height, std::vector<std::uint8_t> pixels)
{
    Raster raster;
    raster.width = width;
    raster.height = height;
    raster.channels = 4;
    raster.format = RasterFormat::UInt8RGBA;
    raster.byte_data = std::move(pixels);
    return raster;
}

Raster float_raster(int width, int height, std::vector<float> values)
{
    Raster raster;
    raster.width = width;
    raster.height = height;
    raster.channels = 1;
    raster.format = RasterFormat::Float32;
    raster.float_data = std::move(values);
    return raster;
}

RasterCacheEntry raster_entry(Raster raster)
{
    RasterCacheEntry entry;
    entry.raster = std::move(raster);
    return entry;
}

std::vector<std::uint8_t> read_binary(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> tiny_jpeg_rgb_120_80_40()
{
    constexpr std::array<std::uint8_t, 632> bytes{
        255, 216, 255, 224, 0,   16,  74,  70,  73,  70,  0,   1,   1,   0,   0,   1,   0,   1,
        0,   0,   255, 219, 0,   67,  0,   2,   1,   1,   1,   1,   1,   2,   1,   1,   1,   2,
        2,   2,   2,   2,   4,   3,   2,   2,   2,   2,   5,   4,   4,   3,   4,   6,   5,   6,
        6,   6,   5,   6,   6,   6,   7,   9,   8,   6,   7,   9,   7,   6,   6,   8,   11,  8,
        9,   10,  10,  10,  10,  10,  6,   8,   11,  12,  11,  10,  12,  9,   10,  10,  10,  255,
        219, 0,   67,  1,   2,   2,   2,   2,   2,   2,   5,   3,   3,   5,   10,  7,   6,   7,
        10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
        10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
        10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  255, 192, 0,   17,
        8,   0,   1,   0,   1,   3,   1,   17,  0,   2,   17,  1,   3,   17,  1,   255, 196, 0,
        31,  0,   0,   1,   5,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,
        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  255, 196, 0,   181, 16,  0,
        2,   1,   3,   3,   2,   4,   3,   5,   5,   4,   4,   0,   0,   1,   125, 1,   2,   3,
        0,   4,   17,  5,   18,  33,  49,  65,  6,   19,  81,  97,  7,   34,  113, 20,  50,  129,
        145, 161, 8,   35,  66,  177, 193, 21,  82,  209, 240, 36,  51,  98,  114, 130, 9,   10,
        22,  23,  24,  25,  26,  37,  38,  39,  40,  41,  42,  52,  53,  54,  55,  56,  57,  58,
        67,  68,  69,  70,  71,  72,  73,  74,  83,  84,  85,  86,  87,  88,  89,  90,  99,  100,
        101, 102, 103, 104, 105, 106, 115, 116, 117, 118, 119, 120, 121, 122, 131, 132, 133, 134,
        135, 136, 137, 138, 146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164, 165, 166,
        167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194, 195, 196, 197, 198,
        199, 200, 201, 202, 210, 211, 212, 213, 214, 215, 216, 217, 218, 225, 226, 227, 228, 229,
        230, 231, 232, 233, 234, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 196, 0,
        31,  1,   0,   3,   1,   1,   1,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,
        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  255, 196, 0,   181, 17,  0,
        2,   1,   2,   4,   4,   3,   4,   7,   5,   4,   4,   0,   1,   2,   119, 0,   1,   2,
        3,   17,  4,   5,   33,  49,  6,   18,  65,  81,  7,   97,  113, 19,  34,  50,  129, 8,
        20,  66,  145, 161, 177, 193, 9,   35,  51,  82,  240, 21,  98,  114, 209, 10,  22,  36,
        52,  225, 37,  241, 23,  24,  25,  26,  38,  39,  40,  41,  42,  53,  54,  55,  56,  57,
        58,  67,  68,  69,  70,  71,  72,  73,  74,  83,  84,  85,  86,  87,  88,  89,  90,  99,
        100, 101, 102, 103, 104, 105, 106, 115, 116, 117, 118, 119, 120, 121, 122, 130, 131, 132,
        133, 134, 135, 136, 137, 138, 146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164,
        165, 166, 167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194, 195, 196,
        197, 198, 199, 200, 201, 202, 210, 211, 212, 213, 214, 215, 216, 217, 218, 226, 227, 228,
        229, 230, 231, 232, 233, 234, 242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 218, 0,
        12,  3,   1,   0,   2,   17,  3,   17,  0,   63,  0,   249, 110, 191, 19,  63,  92,  63,
        255, 217,
    };
    return {bytes.begin(), bytes.end()};
}

void sqlite_exec(sqlite3 *db, const char *sql)
{
    char *error = nullptr;
    ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, &error), SQLITE_OK)
        << (error == nullptr ? "" : error);
    sqlite3_free(error);
}

OneShotHttpServer
start_one_shot_http_server(std::string body, int status_code, std::chrono::milliseconds delay = {})
{
    UniqueFd listen_fd(new int(socket(AF_INET, SOCK_STREAM, 0)));
    if (*listen_fd < 0)
    {
        throw std::runtime_error("socket failed");
    }
    int reuse = 1;
    setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(*listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        throw std::runtime_error("bind failed");
    }
    if (listen(*listen_fd, 1) != 0)
    {
        throw std::runtime_error("listen failed");
    }
    socklen_t length = sizeof(address);
    if (getsockname(*listen_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0)
    {
        throw std::runtime_error("getsockname failed");
    }

    const int fd = *listen_fd;
    listen_fd.release();
    OneShotHttpServer server;
    server.url = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/{z}/{x}/{y}.png";
    server.thread = std::jthread(
        [fd, body = std::move(body), status_code, delay](std::stop_token)
        {
            UniqueFd listener(new int(fd));
            sockaddr_in client_address{};
            socklen_t client_length = sizeof(client_address);
            UniqueFd client(new int(
                accept(*listener, reinterpret_cast<sockaddr *>(&client_address), &client_length)));
            if (*client < 0)
            {
                return;
            }
            std::array<char, 512> request_buffer{};
            [[maybe_unused]] const ssize_t bytes_read =
                read(*client, request_buffer.data(), request_buffer.size());
            if (delay.count() > 0)
            {
                std::this_thread::sleep_for(delay);
            }
            const std::string status_text = status_code == 200 ? "OK" : "Not Found";
            const std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " +
                                         status_text +
                                         "\r\nContent-Length: " + std::to_string(body.size()) +
                                         "\r\nConnection: close\r\n\r\n" + body;
            (void)send(*client, response.data(), response.size(), MSG_NOSIGNAL);
        });
    return server;
}

std::optional<PreparedTile> wait_for_ready_tile(TerrainStreamer &streamer,
                                                const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto ready = streamer.drain_ready_cpu(1, 1024U * 1024U);
        if (!ready.empty())
        {
            return std::move(ready.front());
        }
        if (!streamer.drain_failed().empty())
        {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

TEST(TerrainCoreData, TerrainRgbKnownValuesDecodeToMeters)
{
    EXPECT_FLOAT_EQ(terrain_rgb_to_meters(0, 0, 0), -10000.0F);
    EXPECT_FLOAT_EQ(terrain_rgb_to_meters(1, 134, 160), 0.0F);
    EXPECT_NEAR(terrain_rgb_to_meters(1, 150, 0), 393.6F, 1.0e-3F);
}

TEST(TerrainCoreData, LocalXyzTilePathsUseLayerZoomXAndY)
{
    const std::filesystem::path root = "animus/data/tiles/lake_tahoe";
    EXPECT_EQ(
        local_xyz_tile_path(root, "imagery", TileCoord{12, 682, 1563}, "png").generic_string(),
        "animus/data/tiles/lake_tahoe/imagery/12/682/1563.png");
    EXPECT_EQ(
        local_xyz_tile_path(root, "elevation", TileCoord{12, 681, 1562}, "png").generic_string(),
        "animus/data/tiles/lake_tahoe/elevation/12/681/1562.png");
}

TEST(TerrainCoreData, LoadPngRgbaReturnsExpectedShapeAndChannels)
{
    constexpr std::array<unsigned char, 70> tiny_png{
        137, 80, 78, 71, 13, 10,  26,  10, 0,  0,  0,  13,  73,  72,  68,  82,  0, 0,
        0,   1,  0,  0,  0,  1,   8,   6,  0,  0,  0,  31,  21,  196, 137, 0,   0, 0,
        13,  73, 68, 65, 84, 120, 156, 99, 16, 50, 9,  251, 15,  0,   2,   148, 1, 156,
        29,  91, 70, 95, 0,  0,   0,   0,  73, 69, 78, 68,  174, 66,  96,  130,
    };
    const auto path = std::filesystem::temp_directory_path() / "animus_tiny_rgba.png";
    {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output);
        output.write(reinterpret_cast<const char *>(tiny_png.data()),
                     static_cast<std::streamsize>(tiny_png.size()));
    }

    const Raster raster = load_png_rgba(path);
    std::filesystem::remove(path);

    EXPECT_EQ(raster.width, 1);
    EXPECT_EQ(raster.height, 1);
    EXPECT_EQ(raster.channels, 4);
    EXPECT_EQ(raster.format, RasterFormat::UInt8RGBA);
    EXPECT_EQ(raster.byte_data.size(), 4U);
    EXPECT_EQ(raster.byte_data, (std::vector<std::uint8_t>{0x12, 0x34, 0x56, 0xFF}));
}

TEST(TerrainCoreData, DecodeTerrainRgbProducesFloatRasterAndTracksMinMax)
{
    const Raster image = rgba_raster(2,
                                     1,
                                     {
                                         0,
                                         0,
                                         0,
                                         255,
                                         1,
                                         134,
                                         160,
                                         255,
                                     });

    const Raster heights = decode_terrain_rgb(image);
    ASSERT_EQ(heights.float_data.size(), 2U);
    EXPECT_EQ(heights.channels, 1);
    EXPECT_EQ(heights.format, RasterFormat::Float32);
    EXPECT_FLOAT_EQ(heights.float_data[0], -10000.0F);
    EXPECT_FLOAT_EQ(heights.float_data[1], 0.0F);

    const auto stats = float_raster_min_max(heights);
    EXPECT_FLOAT_EQ(stats.min_value, -10000.0F);
    EXPECT_FLOAT_EQ(stats.max_value, 0.0F);
}

TEST(TerrainCoreData, DecodeTerrainRgbRejectsInvalidRasters)
{
    EXPECT_THROW((void)decode_terrain_rgb(Raster{}), std::invalid_argument);

    Raster wrong_format = float_raster(1, 1, {0.0F});
    EXPECT_THROW((void)decode_terrain_rgb(wrong_format), std::invalid_argument);

    Raster wrong_size = rgba_raster(2, 1, {0, 0, 0, 255});
    EXPECT_THROW((void)decode_terrain_rgb(wrong_size), std::invalid_argument);
}

TEST(TerrainCoreData, FloatRasterMinMaxHandlesFiniteValuesAndRejectsInvalidInput)
{
    const Raster raster = float_raster(3, 1, {10.0F, -4.0F, 7.5F});
    const auto stats = float_raster_min_max(raster);
    EXPECT_FLOAT_EQ(stats.min_value, -4.0F);
    EXPECT_FLOAT_EQ(stats.max_value, 10.0F);

    EXPECT_THROW((void)float_raster_min_max(Raster{}), std::invalid_argument);
    EXPECT_THROW((void)float_raster_min_max(rgba_raster(1, 1, {0, 0, 0, 255})),
                 std::invalid_argument);
    EXPECT_THROW((void)float_raster_min_max(float_raster(1, 1, {})), std::invalid_argument);
}

TEST(TerrainCoreData, MeshGenerationBuildsGridAndSkirts)
{
    const Raster heights = float_raster(3,
                                        2,
                                        {
                                            10.0F,
                                            20.0F,
                                            30.0F,
                                            40.0F,
                                            50.0F,
                                            60.0F,
                                        });

    const auto mesh = build_terrain_mesh(heights,
                                         TerrainMeshOptions{
                                             -1.0F,
                                             2.0F,
                                             4.0F,
                                             0.01F,
                                             0.25F,
                                         });

    EXPECT_EQ(mesh.vertices.size(), 16U);
    EXPECT_EQ(mesh.indices.size(), 48U);
    EXPECT_FLOAT_EQ(mesh.vertices[0].x, -1.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[0].z, 2.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[0].u, 0.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[0].v, 0.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[5].x, 3.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[5].z, 6.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[5].u, 1.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[5].v, 1.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[6].y, mesh.vertices[0].y - 0.25F);

    for (const auto &vertex : mesh.vertices)
    {
        EXPECT_TRUE(std::isfinite(vertex.x));
        EXPECT_TRUE(std::isfinite(vertex.y));
        EXPECT_TRUE(std::isfinite(vertex.z));
        EXPECT_TRUE(std::isfinite(vertex.u));
        EXPECT_TRUE(std::isfinite(vertex.v));
    }
    for (const auto index : mesh.indices)
    {
        EXPECT_LT(index, mesh.vertices.size());
    }
}

TEST(TerrainCoreData, MeshGenerationRejectsInvalidHeightRaster)
{
    EXPECT_THROW((void)build_terrain_mesh(float_raster(1, 2, {1.0F, 2.0F}), TerrainMeshOptions{}),
                 std::invalid_argument);
    EXPECT_THROW(
        (void)build_terrain_mesh(float_raster(2, 2, {1.0F, 2.0F, 3.0F}), TerrainMeshOptions{}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)build_terrain_mesh(
            float_raster(2, 2, {1.0F, std::numeric_limits<float>::quiet_NaN(), 3.0F, 4.0F}),
            TerrainMeshOptions{}),
        std::invalid_argument);
}

TEST(TerrainCoreData, FloatRasterBilinearSamplesEdgesAndInterior)
{
    const Raster raster = float_raster(2, 2, {0.0F, 10.0F, 20.0F, 30.0F});

    const auto corner = sample_float_raster_bilinear(raster, 1.0, 1.0);
    ASSERT_TRUE(corner.available);
    EXPECT_FLOAT_EQ(corner.value, 30.0F);

    const auto center = sample_float_raster_bilinear(raster, 0.5, 0.5);
    ASSERT_TRUE(center.available);
    EXPECT_FLOAT_EQ(center.value, 15.0F);

    EXPECT_FALSE(sample_float_raster_bilinear(raster, -0.01, 0.5).available);
}

TEST(TerrainCoreData, FloatRasterBilinearReturnsUnavailableForNoData)
{
    Raster raster = float_raster(2, 2, {0.0F, -9999.0F, 20.0F, 30.0F});
    raster.no_data_value = -9999.0F;

    EXPECT_FALSE(sample_float_raster_bilinear(raster, 0.5, 0.0).available);
}

TEST(TerrainCoreData, GeoidGridSamplesGeneratedOffsetFixture)
{
    const GeoidGrid grid{
        10.0,
        20.0,
        12.0,
        22.0,
        float_raster(2, 2, {1.0F, 3.0F, 5.0F, 7.0F}),
    };

    const auto sample = sample_geoid_grid(grid, 11.0, 21.0);
    ASSERT_TRUE(sample.available);
    EXPECT_FLOAT_EQ(sample.value, 4.0F);
    EXPECT_FALSE(sample_geoid_grid(grid, 9.0, 21.0).available);
}

TEST(TerrainCoreCache, CacheKeysAreStableAndPathSafe)
{
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Elevation,
        "USGS 3DEP/DEM",
        "Terrain RGB",
        "Lake Tahoe",
        256,
        11,
        13,
    };
    const std::string key = cache_key_string(TileCacheKey{layer, TileCoord{12, 682, 1563}});
    EXPECT_EQ(key, "elevation/usgs_3dep_dem/terrain_rgb/lake_tahoe/256/11-13/12/682/1563");
}

TEST(TerrainCoreCache, RasterLruEvictsLeastRecentlyUsedEntry)
{
    RasterLruCache cache(32);
    cache.put("a", raster_entry(float_raster(2, 2, {1.0F, 2.0F, 3.0F, 4.0F})));
    cache.put("b", raster_entry(float_raster(2, 2, {5.0F, 6.0F, 7.0F, 8.0F})));
    EXPECT_TRUE(cache.get("a").has_value());
    cache.put("c", raster_entry(float_raster(2, 2, {9.0F, 10.0F, 11.0F, 12.0F})));

    EXPECT_TRUE(cache.get("a").has_value());
    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_TRUE(cache.get("c").has_value());
    EXPECT_EQ(cache.stats().counters.evictions, 1U);
    EXPECT_EQ(cache.stats().counters.hits, 3U);
    EXPECT_EQ(cache.stats().counters.misses, 1U);
    EXPECT_EQ(cache.stats().counters.stores, 3U);
    EXPECT_LE(cache.stats().bytes, cache.stats().byte_limit);
}

TEST(TerrainCoreCache, L3PersistenceReloadsFloatRaster)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_l3_cache_test";
    std::filesystem::remove_all(root);
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Elevation,
        "fixture",
        "float32",
        "",
        2,
        0,
        4,
    };
    const TileCacheKey key{layer, TileCoord{1, 1, 1}};
    const Raster raster = float_raster(2, 2, {1.0F, -2.0F, 3.5F, 4.0F});
    store_l3_raster(root, key, raster, PersistedTileInfo{false, {}, -2.0F, 4.0F, 0});

    const auto loaded = load_l3_raster(root, key);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->raster.format, RasterFormat::Float32);
    EXPECT_EQ(loaded->raster.float_data, raster.float_data);
    EXPECT_FALSE(loaded->synthetic);
    EXPECT_EQ(loaded->synthesis_depth, 0);
    std::filesystem::remove_all(root);
}

TEST(TerrainCoreCache, L3PersistenceReloadsSyntheticSidecarMetadata)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_l3_synthetic_test";
    std::filesystem::remove_all(root);
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Elevation,
        "fixture",
        "float32",
        "",
        2,
        0,
        4,
    };
    const TileCacheKey key{layer, TileCoord{2, 2, 2}};
    const std::vector<TileCoord> sources{TileCoord{1, 1, 1}};
    store_l3_raster(root,
                    key,
                    float_raster(2, 2, {1.0F, 2.0F, 3.0F, 4.0F}),
                    PersistedTileInfo{true, sources, 1.0F, 4.0F, 1});

    const auto loaded = load_l3_raster(root, key);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->synthetic);
    EXPECT_EQ(loaded->synthesis_depth, 1);
    EXPECT_EQ(loaded->source_coords, sources);
    std::filesystem::remove_all(root);
}

TEST(TerrainCoreCache, SqliteMetadataPersistsTileRecords)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_cache_metadata_test";
    std::filesystem::remove_all(root);
    CacheMetadataStore store(root);
    CacheMetadataRecord record;
    record.layer_identity = "imagery/local_xyz/default/_/256/0-18";
    record.source_type = TileSourceType::Mbtiles;
    record.provenance = "fixture.mbtiles";
    record.source_identity = "fixture-source";
    record.byte_size = 128;
    record.coord = TileCoord{2, 1, 2};
    record.source_coords = {TileCoord{1, 0, 1}};
    record.validation_status = "valid";

    store.upsert(record);

    const auto loaded = store.read(record.layer_identity, record.coord);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->provenance, "fixture.mbtiles");
    EXPECT_EQ(loaded->source_identity, "fixture-source");
    EXPECT_EQ(loaded->byte_size, 128U);
    EXPECT_EQ(loaded->source_coords, record.source_coords);
    EXPECT_EQ(loaded->validation_status, "valid");
    EXPECT_TRUE(std::filesystem::exists(root / "metadata.sqlite3"));
    std::filesystem::remove_all(root);
}

TEST(TerrainCoreSources, MbtilesReadsMetadataAndFlipsTmsRows)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_mbtiles_test";
    const auto png_path = root / "tile.png";
    const auto mbtiles_path = root / "fixture.mbtiles";
    std::filesystem::remove_all(root);
    save_png_rgba(png_path, rgba_raster(1, 1, {10, 20, 30, 255}));
    const std::vector<std::uint8_t> png_bytes = read_binary(png_path);

    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(mbtiles_path.string().c_str(), &db), SQLITE_OK);
    sqlite_exec(db, "CREATE TABLE metadata(name TEXT, value TEXT)");
    sqlite_exec(db,
                "CREATE TABLE tiles(zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, "
                "tile_data BLOB)");
    sqlite_exec(db, "INSERT INTO metadata(name,value) VALUES('name','fixture')");
    sqlite3_stmt *statement = nullptr;
    ASSERT_EQ(
        sqlite3_prepare_v2(
            db,
            "INSERT INTO tiles(zoom_level,tile_column,tile_row,tile_data) VALUES(?1,?2,?3,?4)",
            -1,
            &statement,
            nullptr),
        SQLITE_OK);
    sqlite3_bind_int(statement, 1, 1);
    sqlite3_bind_int(statement, 2, 1);
    sqlite3_bind_int(statement, 3, 0);
    sqlite3_bind_blob(
        statement, 4, png_bytes.data(), static_cast<int>(png_bytes.size()), SQLITE_TRANSIENT);
    EXPECT_EQ(sqlite3_step(statement), SQLITE_DONE);
    sqlite3_finalize(statement);
    sqlite3_close(db);

    MbtilesTileSource source(mbtiles_path);
    EXPECT_EQ(source.metadata_value("name"), std::optional<std::string>("fixture"));
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Imagery, "mbtiles", "default", "", 1, 0, 2};
    const auto loaded = source.load_tile(TileCoord{1, 1, 1}, layer);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->source_type, TileSourceType::Mbtiles);
    EXPECT_EQ(loaded->raster.byte_data, (std::vector<std::uint8_t>{10, 20, 30, 255}));
    EXPECT_FALSE(source.load_tile(TileCoord{1, 0, 1}, layer).has_value());
    std::filesystem::remove_all(root);
}

TEST(TerrainCoreSources, MbtilesReadsJpegPayloadsThroughTileSource)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_mbtiles_jpeg_test";
    const auto mbtiles_path = root / "fixture.mbtiles";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::vector<std::uint8_t> jpeg_bytes = tiny_jpeg_rgb_120_80_40();

    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(mbtiles_path.string().c_str(), &db), SQLITE_OK);
    sqlite_exec(db, "CREATE TABLE metadata(name TEXT, value TEXT)");
    sqlite_exec(db,
                "CREATE TABLE tiles(zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, "
                "tile_data BLOB)");
    sqlite3_stmt *statement = nullptr;
    ASSERT_EQ(
        sqlite3_prepare_v2(
            db,
            "INSERT INTO tiles(zoom_level,tile_column,tile_row,tile_data) VALUES(?1,?2,?3,?4)",
            -1,
            &statement,
            nullptr),
        SQLITE_OK);
    sqlite3_bind_int(statement, 1, 1);
    sqlite3_bind_int(statement, 2, 1);
    sqlite3_bind_int(statement, 3, 0);
    sqlite3_bind_blob(
        statement, 4, jpeg_bytes.data(), static_cast<int>(jpeg_bytes.size()), SQLITE_TRANSIENT);
    EXPECT_EQ(sqlite3_step(statement), SQLITE_DONE);
    sqlite3_finalize(statement);
    sqlite3_close(db);

    MbtilesTileSource source(mbtiles_path);
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Imagery, "mbtiles", "default", "", 1, 0, 2};
    const auto loaded = source.load_tile(TileCoord{1, 1, 1}, layer);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->source_type, TileSourceType::Mbtiles);
    EXPECT_EQ(loaded->raster.width, 1);
    EXPECT_EQ(loaded->raster.height, 1);
    EXPECT_EQ(loaded->raster.channels, 4);
    EXPECT_EQ(loaded->raster.format, RasterFormat::UInt8RGBA);
    ASSERT_EQ(loaded->raster.byte_data.size(), 4U);
    EXPECT_NEAR(static_cast<int>(loaded->raster.byte_data[0]), 120, 4);
    EXPECT_NEAR(static_cast<int>(loaded->raster.byte_data[1]), 80, 4);
    EXPECT_NEAR(static_cast<int>(loaded->raster.byte_data[2]), 40, 4);
    EXPECT_EQ(loaded->raster.byte_data[3], 255U);
    EXPECT_FALSE(source.load_tile(TileCoord{1, 0, 1}, layer).has_value());
    std::filesystem::remove_all(root);
}

TEST(TerrainCoreSources, RemoteHttpDoesNothingWithoutExplicitProvider)
{
    RemoteHttpTileSource source(RemoteHttpTileProvider{});
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Imagery, "remote", "default", "", 1, 0, 2};

    EXPECT_FALSE(source.load_tile(TileCoord{1, 1, 1}, layer).has_value());
}

TEST(TerrainCoreSources, RemoteHttpFetchesLoopbackTileAndHandles404)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_remote_http_test";
    const auto png_path = root / "tile.png";
    std::filesystem::remove_all(root);
    save_png_rgba(png_path, rgba_raster(1, 1, {77, 88, 99, 255}));
    const std::vector<std::uint8_t> png_bytes = read_binary(png_path);

    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Imagery, "remote", "default", "", 1, 0, 2};

    {
        OneShotHttpServer server = start_one_shot_http_server(
            std::string(reinterpret_cast<const char *>(png_bytes.data()), png_bytes.size()), 200);
        RemoteHttpTileProvider provider;
        provider.url_template = server.url;
        provider.cache_identity = "loopback-fixture";
        provider.timeout = std::chrono::milliseconds(1000);
        RemoteHttpTileSource source(provider);
        const auto loaded = source.load_tile(TileCoord{1, 1, 1}, layer);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->source_type, TileSourceType::RemoteHttp);
        EXPECT_EQ(loaded->raster.byte_data, (std::vector<std::uint8_t>{77, 88, 99, 255}));
        EXPECT_EQ(source.cache_identity(), "loopback-fixture");
    }

    {
        OneShotHttpServer server = start_one_shot_http_server("", 404);
        RemoteHttpTileProvider provider;
        provider.url_template = server.url;
        provider.timeout = std::chrono::milliseconds(1000);
        RemoteHttpTileSource source(provider);
        EXPECT_FALSE(source.load_tile(TileCoord{1, 1, 1}, layer).has_value());
    }

    std::filesystem::remove_all(root);
}

TEST(TerrainCoreSources, RemoteHttpTimeoutFailsDeterministically)
{
    OneShotHttpServer server = start_one_shot_http_server("", 200, std::chrono::milliseconds(250));
    RemoteHttpTileProvider provider;
    provider.url_template = server.url;
    provider.timeout = std::chrono::milliseconds(25);
    RemoteHttpTileSource source(provider);
    animus::terrain_core::LayerSpec layer{
        animus::terrain_core::LayerType::Imagery, "remote", "default", "", 1, 0, 2};

    EXPECT_THROW((void)source.load_tile(TileCoord{1, 1, 1}, layer), std::runtime_error);
}

TEST(TerrainCoreDatum, AltitudeReferencesResolveAgainstOrthometricTerrain)
{
    const GeoidCorrectionGrid no_grid;

    const auto msl = height_above_terrain_m(animus::terrain_core::AltitudeReference::MslOrthometric,
                                            1500.0,
                                            1450.0,
                                            39.0,
                                            -120.0,
                                            no_grid);
    ASSERT_TRUE(msl.has_value());
    EXPECT_DOUBLE_EQ(*msl, 50.0);

    const auto relative =
        height_above_terrain_m(animus::terrain_core::AltitudeReference::TerrainRelative,
                               75.0,
                               1450.0,
                               39.0,
                               -120.0,
                               no_grid);
    ASSERT_TRUE(relative.has_value());
    EXPECT_DOUBLE_EQ(*relative, 75.0);

    EXPECT_FALSE(
        height_above_terrain_m(
            animus::terrain_core::AltitudeReference::Unknown, 1500.0, 1450.0, 39.0, -120.0, no_grid)
            .has_value());
}

TEST(TerrainCoreStreaming, L3PersistenceSurvivesFreshStreamerInstance)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_stream_l3_restart";
    const auto pack_root = root / "pack";
    const auto cache_root = root / "cache";
    std::filesystem::remove_all(root);

    const TileCoord coord{1, 1, 1};
    save_png_rgba(local_xyz_tile_path(pack_root, "imagery", coord, "png"),
                  rgba_raster(2,
                              2,
                              {
                                  10,
                                  20,
                                  30,
                                  255,
                                  40,
                                  50,
                                  60,
                                  255,
                                  70,
                                  80,
                                  90,
                                  255,
                                  100,
                                  110,
                                  120,
                                  255,
                              }));
    save_png_rgba(local_xyz_tile_path(pack_root, "elevation", coord, "png"),
                  rgba_raster(2,
                              2,
                              {
                                  1,
                                  134,
                                  160,
                                  255,
                                  1,
                                  134,
                                  160,
                                  255,
                                  1,
                                  134,
                                  160,
                                  255,
                                  1,
                                  134,
                                  160,
                                  255,
                              }));

    TileLoadRequest request;
    request.coord = coord;
    request.pack_root = pack_root;
    request.cache_root = cache_root;
    request.imagery_spec.resolution = 2;
    request.elevation_spec.resolution = 2;
    request.mesh_options.tile_size = 1.0F;
    request.request_generation = 1;

    {
        TerrainStreamer streamer(TerrainStreamConfig{0, 1, 1, 1, 1, 1, 1, 1, 1024U * 1024U, true});
        streamer.request_tiles({request});
        const auto tile = wait_for_ready_tile(streamer, std::chrono::seconds(2));
        ASSERT_TRUE(tile.has_value());
        EXPECT_EQ(tile->cache_tier, animus::terrain_core::CacheTier::LocalXyz);
    }

    request.pack_root = root / "missing-pack";
    request.request_generation = 2;
    {
        TerrainStreamer streamer(TerrainStreamConfig{0, 1, 1, 1, 1, 1, 1, 1, 1024U * 1024U, true});
        streamer.request_tiles({request});
        const auto tile = wait_for_ready_tile(streamer, std::chrono::seconds(2));
        ASSERT_TRUE(tile.has_value());
        EXPECT_EQ(tile->cache_tier, animus::terrain_core::CacheTier::L3Disk);
        EXPECT_EQ(tile->source_type, animus::terrain_core::TileSourceType::DiskCache);
        const auto snapshot = streamer.snapshot();
        EXPECT_GE(snapshot.cache_stats.l3_disk.counters.hits, 2U);
    }

    std::filesystem::remove_all(root);
}

TEST(TerrainCoreSynthesis, SynthesizesChildRastersFromExactParentQuadrants)
{
    const Raster parent = float_raster(3,
                                       3,
                                       {
                                           0.0F,
                                           1.0F,
                                           2.0F,
                                           3.0F,
                                           4.0F,
                                           5.0F,
                                           6.0F,
                                           7.0F,
                                           8.0F,
                                       });
    const Raster southwest =
        synthesize_child_from_parent(parent, TileCoord{1, 1, 1}, TileCoord{2, 2, 3});
    EXPECT_EQ(southwest.float_data,
              (std::vector<float>{3.0F, 3.5F, 4.0F, 4.5F, 5.0F, 5.5F, 6.0F, 6.5F, 7.0F}));

    const Raster rgba_parent =
        rgba_raster(3,
                    3,
                    {
                        0,  0, 0, 255, 10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255, 40, 0, 0, 255,
                        50, 0, 0, 255, 60, 0, 0, 255, 70, 0, 0, 255, 80, 0, 0, 255,
                    });
    const Raster northeast =
        synthesize_child_from_parent(rgba_parent, TileCoord{1, 1, 1}, TileCoord{2, 3, 2});
    ASSERT_EQ(northeast.byte_data.size(), 36U);
    EXPECT_EQ(northeast.byte_data[0], 10);
    EXPECT_EQ(northeast.byte_data[4], 15);
    EXPECT_EQ(northeast.byte_data[8], 20);
    EXPECT_EQ(northeast.byte_data[24], 40);
    EXPECT_EQ(northeast.byte_data[32], 50);
}

TEST(TerrainCoreSynthesis, SynthesizesParentRastersFromFourChildren)
{
    const std::array<Raster, 4> children{
        float_raster(2, 2, {1.0F, 1.0F, 1.0F, 1.0F}),
        float_raster(2, 2, {2.0F, 2.0F, 2.0F, 2.0F}),
        float_raster(2, 2, {3.0F, 3.0F, 3.0F, 3.0F}),
        float_raster(2, 2, {4.0F, 4.0F, 4.0F, 4.0F}),
    };
    const Raster synthesized_parent = synthesize_parent_from_children(children);
    ASSERT_EQ(synthesized_parent.float_data.size(), 4U);
    EXPECT_EQ(synthesized_parent.float_data, (std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));

    const std::array<Raster, 4> rgba_children{
        rgba_raster(1, 1, {1, 10, 20, 255}),
        rgba_raster(1, 1, {2, 10, 20, 255}),
        rgba_raster(1, 1, {3, 10, 20, 255}),
        rgba_raster(1, 1, {4, 10, 20, 255}),
    };
    const Raster rgba_parent = synthesize_parent_from_children(rgba_children);
    EXPECT_EQ(rgba_parent.byte_data, (std::vector<std::uint8_t>{1, 10, 20, 255}));
}

TEST(TerrainCoreStreaming, SyntheticL3InputsDoNotSeedAnotherSyntheticTile)
{
    const auto root = std::filesystem::temp_directory_path() / "animus_synthetic_loop_block";
    const auto cache_root = root / "cache";
    std::filesystem::remove_all(root);

    const TileCoord parent{1, 1, 1};
    animus::terrain_core::LayerSpec imagery{
        animus::terrain_core::LayerType::Imagery, "local_xyz", "default", "", 2, 0, 18};
    animus::terrain_core::LayerSpec elevation{
        animus::terrain_core::LayerType::Elevation, "local_xyz", "terrain_rgb", "", 2, 0, 18};
    store_l3_raster(cache_root,
                    TileCacheKey{imagery, parent},
                    rgba_raster(2, 2, {1, 1, 1, 255, 1, 1, 1, 255, 1, 1, 1, 255, 1, 1, 1, 255}),
                    PersistedTileInfo{true, {TileCoord{0, 0, 0}}, 0.0F, 255.0F, 1});
    store_l3_raster(cache_root,
                    TileCacheKey{elevation, parent},
                    float_raster(2, 2, {0.0F, 0.0F, 0.0F, 0.0F}),
                    PersistedTileInfo{true, {TileCoord{0, 0, 0}}, 0.0F, 0.0F, 1});

    TerrainStreamer streamer(TerrainStreamConfig{0, 2, 1, 1, 1, 1, 1, 1, 1024U * 1024U, true});
    TileLoadRequest request;
    request.coord = TileCoord{2, 2, 2};
    request.pack_root = root / "missing-pack";
    request.cache_root = cache_root;
    request.imagery_spec = imagery;
    request.elevation_spec = elevation;
    request.mesh_options.tile_size = 1.0F;
    request.request_generation = 1;
    request.max_synthesis_depth = 1;
    streamer.request_tiles({request});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && streamer.drain_failed().empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(streamer.state_of(request.coord), TileState::Failed);
    EXPECT_FALSE(load_l3_raster(cache_root, TileCacheKey{imagery, request.coord}).has_value());
    std::filesystem::remove_all(root);
}

TEST(TerrainCoreData, MergesElevationAndBathymetryAtSeaLevel)
{
    const Raster elevation = float_raster(
        6, 1, {10.0F, 0.0F, -1.0F, -2.0F, std::numeric_limits<float>::quiet_NaN(), -3.0F});
    Raster bathymetry = float_raster(
        6, 1, {-100.0F, -20.0F, -30.0F, std::numeric_limits<float>::quiet_NaN(), -40.0F, -9999.0F});
    bathymetry.no_data_value = -9999.0F;

    const Raster merged = merge_elevation_bathymetry(elevation, bathymetry);
    ASSERT_EQ(merged.float_data.size(), 6U);
    EXPECT_FLOAT_EQ(merged.float_data[0], 10.0F);
    EXPECT_FLOAT_EQ(merged.float_data[1], -20.0F);
    EXPECT_FLOAT_EQ(merged.float_data[2], -30.0F);
    EXPECT_FLOAT_EQ(merged.float_data[3], -2.0F);
    EXPECT_FLOAT_EQ(merged.float_data[4], -40.0F);
    EXPECT_FLOAT_EQ(merged.float_data[5], -3.0F);
}

#if defined(ANIMUS_TESTS_HAVE_GDAL)
TEST(TerrainCoreGeoTiff, ExtractsGeneratedFloat32TileThroughGdal)
{
    const auto path = std::filesystem::temp_directory_path() / "animus_geotiff_fixture.tif";
    std::filesystem::remove(path);

    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    ASSERT_NE(driver, nullptr);
    GDALDataset *dataset = driver->Create(path.string().c_str(), 4, 4, 1, GDT_Float32, nullptr);
    ASSERT_NE(dataset, nullptr);
    const std::vector<float> values{
        1.0F,
        2.0F,
        3.0F,
        4.0F,
        5.0F,
        6.0F,
        7.0F,
        8.0F,
        9.0F,
        10.0F,
        11.0F,
        12.0F,
        13.0F,
        14.0F,
        15.0F,
        16.0F,
    };
    ASSERT_EQ(
        dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, 4, 4, const_cast<float *>(values.data()), 4, 4, GDT_Float32, 0, 0),
        CE_None);
    GDALClose(dataset);

    const Raster tile = GdalGeoTiffTileSource(path).load_tile(TileCoord{0, 0, 0}, 256);
    EXPECT_EQ(tile.width, 256);
    EXPECT_EQ(tile.height, 256);
    EXPECT_EQ(tile.format, RasterFormat::Float32);
    EXPECT_EQ(tile.float_data.size(), 256U * 256U);
    EXPECT_FLOAT_EQ(tile.float_data.front(), 1.0F);
    EXPECT_FLOAT_EQ(tile.float_data.back(), 16.0F);
    std::filesystem::remove(path);
}

TEST(TerrainCoreGeoTiff, ExtractsGeneratedRgbaOverlayThroughGdal)
{
    const auto path = std::filesystem::temp_directory_path() / "animus_overlay_fixture.tif";
    std::filesystem::remove(path);

    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    ASSERT_NE(driver, nullptr);
    GDALDataset *dataset = driver->Create(path.string().c_str(), 2, 2, 4, GDT_Byte, nullptr);
    ASSERT_NE(dataset, nullptr);
    const std::vector<std::uint8_t> red{255, 0, 0, 255};
    const std::vector<std::uint8_t> green{0, 255, 0, 255};
    const std::vector<std::uint8_t> blue{0, 0, 255, 255};
    const std::vector<std::uint8_t> alpha{255, 128, 64, 0};
    ASSERT_EQ(
        dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, 2, 2, const_cast<std::uint8_t *>(red.data()), 2, 2, GDT_Byte, 0, 0),
        CE_None);
    ASSERT_EQ(
        dataset->GetRasterBand(2)->RasterIO(
            GF_Write, 0, 0, 2, 2, const_cast<std::uint8_t *>(green.data()), 2, 2, GDT_Byte, 0, 0),
        CE_None);
    ASSERT_EQ(
        dataset->GetRasterBand(3)->RasterIO(
            GF_Write, 0, 0, 2, 2, const_cast<std::uint8_t *>(blue.data()), 2, 2, GDT_Byte, 0, 0),
        CE_None);
    ASSERT_EQ(
        dataset->GetRasterBand(4)->RasterIO(
            GF_Write, 0, 0, 2, 2, const_cast<std::uint8_t *>(alpha.data()), 2, 2, GDT_Byte, 0, 0),
        CE_None);
    GDALClose(dataset);

    const Raster tile = GdalGeoTiffTileSource(path).load_tile_rgba(TileCoord{0, 0, 0}, 2);
    EXPECT_EQ(tile.width, 2);
    EXPECT_EQ(tile.height, 2);
    EXPECT_EQ(tile.format, RasterFormat::UInt8RGBA);
    EXPECT_EQ(tile.byte_data.front(), 255);
    EXPECT_EQ(tile.byte_data[3], 255);
    EXPECT_EQ(tile.byte_data[7], 128);
    std::filesystem::remove(path);
}
#endif

TEST(TerrainCoreStreaming, TileStateTransitionsRejectInvalidEdges)
{
    EXPECT_TRUE(
        animus::terrain_core::is_legal_tile_transition(TileState::Missing, TileState::Queued));
    EXPECT_TRUE(
        animus::terrain_core::is_legal_tile_transition(TileState::Queued, TileState::Loading));
    EXPECT_TRUE(animus::terrain_core::is_legal_tile_transition(TileState::BuildingMesh,
                                                               TileState::ReadyCpu));
    EXPECT_TRUE(
        animus::terrain_core::is_legal_tile_transition(TileState::ReadyGpu, TileState::Visible));
    EXPECT_FALSE(
        animus::terrain_core::is_legal_tile_transition(TileState::Missing, TileState::ReadyGpu));
    EXPECT_FALSE(
        animus::terrain_core::is_legal_tile_transition(TileState::Failed, TileState::ReadyCpu));
}

TEST(TerrainCoreStreaming, WishlistRespectsBudgetAndPriority)
{
    const auto wishlist = animus::terrain_core::build_tile_wishlist(
        animus::terrain_core::TerrainViewpoint{682.25, 1563.25, 2.0F}, 12, 5);
    ASSERT_EQ(wishlist.size(), 5U);
    EXPECT_EQ(wishlist.front(), (TileCoord{12, 682, 1563}));

    const int zoom = animus::terrain_core::select_zoom_for_distance(1.0F, 11, 13);
    EXPECT_EQ(zoom, 13);
    EXPECT_EQ(animus::terrain_core::select_zoom_for_distance(8.0F, 11, 13), 11);
}

TEST(TerrainCoreStreaming, MissingTileFailsWithoutCrashingWorker)
{
    TerrainStreamer streamer(TerrainStreamConfig{
        11,
        13,
        4,
        8,
        2,
        1,
        2,
        2,
        1024U * 1024U,
        true,
    });
    const auto temp_root = std::filesystem::temp_directory_path() / "animus_missing_stream_tile";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    TileLoadRequest request;
    request.coord = TileCoord{12, 682, 1563};
    request.priority = 0.0F;
    request.pack_root = temp_root;
    request.imagery_layer = "imagery";
    request.imagery_extension = "png";
    request.elevation_layer = "elevation";
    request.elevation_extension = "png";
    request.mesh_options = TerrainMeshOptions{};
    request.mesh_options.tile_size = 1.0F;
    request.request_generation = 1;
    request.simulate_slow_load_ms = 0;
    request.cache_root = temp_root / "cache";
    request.max_synthesis_depth = 0;
    streamer.request_tiles({request});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline &&
           streamer.state_of(TileCoord{12, 682, 1563}) != TileState::Failed)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(streamer.state_of(TileCoord{12, 682, 1563}), TileState::Failed);
    const auto failed = streamer.drain_failed();
    ASSERT_EQ(failed.size(), 1U);
    EXPECT_FALSE(failed.front().error.empty());
    std::filesystem::remove_all(temp_root);
}

TEST(TerrainCoreStreaming, ParentFallbackHidesChildrenUntilSiblingSetReady)
{
    TerrainStreamer streamer(TerrainStreamConfig{11, 13, 4, 8, 2, 1, 2, 2, 1024U, true});
    const TileCoord parent{11, 341, 781};
    const std::array<TileCoord, 4> children = animus::geo_core::children(parent);
    streamer.mark_ready_gpu(parent);
    streamer.mark_ready_gpu(children[0]);
    streamer.mark_ready_gpu(children[1]);
    streamer.mark_ready_gpu(children[2]);

    std::vector<TileCoord> desired(children.begin(), children.end());
    auto visible = streamer.choose_visible_tiles(desired);
    ASSERT_EQ(visible.size(), 1U);
    EXPECT_EQ(visible.front().coord, parent);
    EXPECT_TRUE(visible.front().using_fallback);

    streamer.mark_ready_gpu(children[3]);
    visible = streamer.choose_visible_tiles(desired);
    ASSERT_EQ(visible.size(), 4U);
    for (const auto &decision : visible)
    {
        EXPECT_FALSE(decision.using_fallback);
    }
}

TEST(TerrainCoreBoundaries, TerrainCoreDoesNotIncludeRenderGlAltairOrBayekHeaders)
{
    const auto root = std::filesystem::path(ANIMUS_SOURCE_DIR) / "libs/terrain_core";
    const std::array<std::string, 8> forbidden{
        "render_core",
        "<GL/",
        "<GLFW/",
        "glew",
        "glfw",
        "bayek/",
        "vehicle/",
        "altair/",
    };

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto extension = entry.path().extension().string();
        if (extension != ".hpp" && extension != ".cpp")
        {
            continue;
        }
        std::ifstream input(entry.path());
        ASSERT_TRUE(input) << entry.path();
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        for (const auto &token : forbidden)
        {
            EXPECT_EQ(contents.find(token), std::string::npos)
                << entry.path().generic_string() << " contains " << token;
        }
    }
}

} // namespace
