#include "animus/terrain_core/terrain_data.hpp"
#include "animus/terrain_core/terrain_cache.hpp"
#include "animus/terrain_core/terrain_stream.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(ANIMUS_TESTS_HAVE_GDAL)
#include <gdal_priv.h>
#endif

#include <gtest/gtest.h>

namespace
{

using animus::geo_core::TileCoord;
using animus::terrain_core::build_terrain_mesh;
using animus::terrain_core::cache_key_string;
using animus::terrain_core::decode_terrain_rgb;
using animus::terrain_core::float_raster_min_max;
using animus::terrain_core::GdalGeoTiffTileSource;
using animus::terrain_core::load_l3_raster;
using animus::terrain_core::load_png_rgba;
using animus::terrain_core::local_xyz_tile_path;
using animus::terrain_core::merge_elevation_bathymetry;
using animus::terrain_core::PersistedTileInfo;
using animus::terrain_core::PreparedTile;
using animus::terrain_core::Raster;
using animus::terrain_core::RasterCacheEntry;
using animus::terrain_core::RasterFormat;
using animus::terrain_core::RasterLruCache;
using animus::terrain_core::sample_float_raster_bilinear;
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
using animus::terrain_core::TileState;

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
