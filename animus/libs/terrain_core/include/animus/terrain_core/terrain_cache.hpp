#pragma once

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_data.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace animus::terrain_core
{

enum class CacheTier
{
    None,
    L0Gpu,
    L1Prepared,
    L2Raster,
    L3Disk,
    LocalXyz,
    GdalGeoTiff,
    Synthesis,
};

enum class TileSourceType
{
    None,
    LocalXyz,
    DiskCache,
    GeoTiff,
    Mbtiles,
    RemoteHttp,
    Synthetic,
};

struct CacheCounters
{
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t stores = 0;
};

struct CacheStats
{
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t byte_limit = 0;
    CacheCounters counters;
};

struct TerrainCacheStats
{
    CacheStats l1_prepared;
    CacheStats l2_raster;
    CacheStats l3_disk;
    std::uint64_t synthesized_tiles = 0;
    std::uint64_t persisted_tiles = 0;
    std::uint64_t geotiff_extraction_failures = 0;
};

struct TileCacheKey
{
    LayerSpec layer;
    geo_core::TileCoord coord;
};

struct PersistedTileInfo
{
    bool synthetic = false;
    std::vector<geo_core::TileCoord> source_coords;
    float min_value = 0.0F;
    float max_value = 0.0F;
    int synthesis_depth = 0;
};

struct RasterCacheEntry
{
    Raster raster;
    CacheTier tier = CacheTier::None;
    TileSourceType source_type = TileSourceType::None;
    bool synthetic = false;
    int synthesis_depth = 0;
    std::vector<geo_core::TileCoord> source_coords;
};

std::string cache_key_string(const TileCacheKey &key);
std::string cache_layer_path(const LayerSpec &layer);
std::filesystem::path l3_tile_data_path(const std::filesystem::path &cache_root,
                                        const TileCacheKey &key,
                                        RasterFormat format);
std::filesystem::path l3_tile_sidecar_path(const std::filesystem::path &cache_root,
                                           const TileCacheKey &key);

std::string_view to_string(CacheTier tier);
std::string_view to_string(TileSourceType source_type);

std::size_t estimate_raster_bytes(const Raster &raster);
std::size_t estimate_mesh_bytes(const TerrainMeshCpu &mesh);

Raster load_float32_raster(const std::filesystem::path &path, int width, int height);
void save_float32_raster(const std::filesystem::path &path, const Raster &raster);
void save_png_rgba(const std::filesystem::path &path, const Raster &raster);

std::optional<RasterCacheEntry> load_l3_raster(const std::filesystem::path &cache_root,
                                               const TileCacheKey &key);
void store_l3_raster(const std::filesystem::path &cache_root,
                     const TileCacheKey &key,
                     const Raster &raster,
                     const PersistedTileInfo &info);

Raster resample_raster_bilinear(const Raster &source, int width, int height);
Raster synthesize_child_from_parent(const Raster &parent,
                                    geo_core::TileCoord parent_coord,
                                    geo_core::TileCoord child_coord);
Raster synthesize_parent_from_children(const std::array<Raster, 4> &children);
Raster merge_elevation_bathymetry(const Raster &elevation, const std::optional<Raster> &bathymetry);

class RasterLruCache
{
  public:
    explicit RasterLruCache(std::size_t byte_limit = 0);

    void set_byte_limit(std::size_t byte_limit);
    std::optional<RasterCacheEntry> get(const std::string &key);
    void put(std::string key, RasterCacheEntry entry);
    [[nodiscard]] CacheStats stats() const;

  private:
    struct Entry
    {
        std::string key;
        RasterCacheEntry value;
        std::size_t bytes = 0;
    };

    void evict_to_limit();

    std::size_t byte_limit_ = 0;
    std::size_t bytes_ = 0;
    CacheCounters counters_;
    std::list<Entry> lru_;
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

class GdalGeoTiffTileSource
{
  public:
    explicit GdalGeoTiffTileSource(std::filesystem::path path);

    [[nodiscard]] bool available() const;
    [[nodiscard]] Raster load_tile(geo_core::TileCoord coord, int resolution) const;
    [[nodiscard]] Raster load_tile_rgba(geo_core::TileCoord coord, int resolution) const;

  private:
    std::filesystem::path path_;
};

} // namespace animus::terrain_core
