#pragma once

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"
#include "animus/terrain_core/terrain_data.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace animus::terrain_core
{

struct PreparedTile
{
    geo_core::TileCoord coord;
    Raster imagery;
    Raster heights;
    TerrainMeshCpu mesh;
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    std::size_t estimated_cpu_bytes = 0;
    CacheTier cache_tier = CacheTier::None;
    TileSourceType source_type = TileSourceType::None;
    bool synthetic = false;
    int synthesis_depth = 0;
    std::string error;
    std::uint64_t request_generation = 0;
};

struct TileLoadRequest
{
    geo_core::TileCoord coord;
    float priority = 0.0F;
    std::filesystem::path pack_root;
    std::string imagery_layer = "imagery";
    std::string imagery_extension = "png";
    std::string elevation_layer = "elevation";
    std::string elevation_extension = "png";
    TerrainMeshOptions mesh_options;
    std::uint64_t request_generation = 0;
    int simulate_slow_load_ms = 0;
    LayerSpec imagery_spec{LayerType::Imagery, "local_xyz", "default", "", 256, 0, 18};
    LayerSpec elevation_spec{LayerType::Elevation, "local_xyz", "terrain_rgb", "", 256, 0, 18};
    LayerSpec bathymetry_spec{LayerType::Bathymetry, "local_xyz", "float32", "", 256, 0, 18};
    std::filesystem::path cache_root = "animus/cache/terrain";
    std::filesystem::path elevation_geotiff;
    std::filesystem::path bathymetry_geotiff;
    bool use_bathymetry = false;
    bool persist_cache = true;
    int max_synthesis_depth = 1;
};

struct TerrainStreamConfig
{
    int min_zoom = 11;
    int max_zoom = 13;
    std::size_t tile_budget = 25;
    std::size_t resident_tile_cap = 64;
    std::size_t max_outstanding_jobs = 16;
    int worker_count = 2;
    int max_texture_uploads_per_frame = 2;
    int max_mesh_uploads_per_frame = 2;
    std::size_t max_upload_bytes_per_frame = 32U * 1024U * 1024U;
    bool parent_fallback = true;
    std::size_t l1_prepared_byte_limit = 128U * 1024U * 1024U;
    std::size_t l2_raster_byte_limit = 128U * 1024U * 1024U;
};

struct TerrainViewpoint
{
    double center_x = 0.0;
    double center_y = 0.0;
    float distance = 4.0F;
};

struct TileRuntimeState
{
    geo_core::TileCoord coord;
    TileState state = TileState::Missing;
    std::optional<geo_core::TileCoord> parent;
    bool children_ready = false;
    float priority = 0.0F;
    std::uint64_t requested_frame = 0;
    std::uint64_t state_frame = 0;
    std::uint64_t request_generation = 0;
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    std::size_t estimated_cpu_bytes = 0;
    CacheTier cache_tier = CacheTier::None;
    TileSourceType source_type = TileSourceType::None;
    bool synthetic = false;
    int synthesis_depth = 0;
    std::string error;
};

struct TileRenderDecision
{
    geo_core::TileCoord coord;
    bool using_fallback = false;
};

struct TerrainStreamSnapshot
{
    std::vector<TileRuntimeState> tiles;
    std::size_t queued_jobs = 0;
    std::size_t loading_jobs = 0;
    std::size_t ready_cpu_tiles = 0;
    std::size_t failed_tiles = 0;
    std::size_t resident_gpu_tiles = 0;
    std::size_t resident_gpu_bytes = 0;
    TerrainCacheStats cache_stats;
    std::uint64_t frame = 0;
};

[[nodiscard]] bool is_legal_tile_transition(TileState from, TileState to);
[[nodiscard]] int select_zoom_for_distance(float distance, int min_zoom, int max_zoom);
[[nodiscard]] std::vector<geo_core::TileCoord>
build_tile_wishlist(const TerrainViewpoint &view, int zoom, std::size_t tile_budget);

class TerrainStreamer
{
  public:
    explicit TerrainStreamer(TerrainStreamConfig config);
    ~TerrainStreamer();

    TerrainStreamer(const TerrainStreamer &) = delete;
    TerrainStreamer &operator=(const TerrainStreamer &) = delete;
    TerrainStreamer(TerrainStreamer &&) = delete;
    TerrainStreamer &operator=(TerrainStreamer &&) = delete;

    void begin_frame();
    void request_tiles(std::vector<TileLoadRequest> requests);
    [[nodiscard]] std::vector<PreparedTile> drain_ready_cpu(std::size_t max_tiles,
                                                            std::size_t max_bytes);
    [[nodiscard]] std::vector<PreparedTile> drain_failed();

    void mark_upload_queued(geo_core::TileCoord coord);
    void mark_ready_gpu(geo_core::TileCoord coord);
    void mark_visible(geo_core::TileCoord coord, bool using_fallback);
    void mark_retiring(geo_core::TileCoord coord);

    [[nodiscard]] TileState state_of(geo_core::TileCoord coord) const;
    [[nodiscard]] bool is_ready_gpu(geo_core::TileCoord coord) const;
    [[nodiscard]] std::vector<TileRenderDecision>
    choose_visible_tiles(const std::vector<geo_core::TileCoord> &desired) const;
    [[nodiscard]] TerrainStreamSnapshot snapshot() const;
    void update_l0_stats(std::size_t resident_gpu_tiles, std::size_t resident_gpu_bytes);

  private:
    struct PreparedCacheEntry
    {
        std::string key;
        PreparedTile tile;
        std::size_t bytes = 0;
    };

    struct QueuedRequest
    {
        TileLoadRequest request;
    };

    void worker_loop();
    [[nodiscard]] PreparedTile prepare_tile(const TileLoadRequest &request);
    [[nodiscard]] std::optional<PreparedTile> prepared_cache_get_locked(const std::string &key);
    void prepared_cache_put_locked(std::string key, PreparedTile tile);
    void evict_prepared_cache_locked();
    [[nodiscard]] RasterCacheEntry
    load_raster(TileLoadRequest request, LayerSpec layer, bool height_layer);
    [[nodiscard]] std::optional<RasterCacheEntry>
    synthesize_raster(const TileLoadRequest &request, const LayerSpec &layer, bool height_layer);
    void transition_locked(geo_core::TileCoord coord, TileState next);
    [[nodiscard]] TileRuntimeState &state_for_locked(geo_core::TileCoord coord);
    [[nodiscard]] std::optional<geo_core::TileCoord>
    best_ready_ancestor_locked(geo_core::TileCoord coord) const;

    TerrainStreamConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable work_available_;
    std::deque<QueuedRequest> work_queue_;
    std::deque<PreparedTile> ready_cpu_queue_;
    std::deque<PreparedTile> failed_queue_;
    std::unordered_map<geo_core::TileCoord, TileRuntimeState> states_;
    std::list<PreparedCacheEntry> l1_prepared_;
    std::unordered_map<std::string, std::list<PreparedCacheEntry>::iterator> l1_prepared_index_;
    std::size_t l1_prepared_bytes_ = 0;
    CacheCounters l1_prepared_counters_;
    RasterLruCache l2_rasters_;
    CacheCounters l3_counters_;
    TerrainCacheStats cache_stats_;
    std::vector<std::jthread> workers_;
    std::size_t loading_jobs_ = 0;
    std::size_t resident_gpu_bytes_ = 0;
    std::size_t resident_gpu_tiles_ = 0;
    std::uint64_t frame_ = 0;
    bool stopping_ = false;
};

} // namespace animus::terrain_core
