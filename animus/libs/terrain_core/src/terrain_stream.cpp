#include "animus/terrain_core/terrain_stream.hpp"
#include "animus/terrain_core/tile_source.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace animus::terrain_core
{
namespace
{

bool terminal_or_resident(const TileState state)
{
    return state == TileState::ReadyCpu || state == TileState::UploadQueued ||
           state == TileState::ReadyGpu || state == TileState::Visible ||
           state == TileState::UsingFallback || state == TileState::Failed;
}

bool coord_less(const geo_core::TileCoord a, const geo_core::TileCoord b)
{
    if (a.z != b.z)
    {
        return a.z < b.z;
    }
    if (a.y != b.y)
    {
        return a.y < b.y;
    }
    return a.x < b.x;
}

} // namespace

bool is_legal_tile_transition(const TileState from, const TileState to)
{
    if (from == to)
    {
        return true;
    }
    switch (from)
    {
    case TileState::Missing:
        return to == TileState::Queued || to == TileState::Failed;
    case TileState::Queued:
        return to == TileState::Loading || to == TileState::Failed || to == TileState::Retiring;
    case TileState::Loading:
        return to == TileState::Decoding || to == TileState::Failed;
    case TileState::Decoding:
        return to == TileState::Decoded || to == TileState::Failed;
    case TileState::Decoded:
        return to == TileState::BuildingMesh || to == TileState::Failed;
    case TileState::BuildingMesh:
        return to == TileState::ReadyCpu || to == TileState::Failed;
    case TileState::ReadyCpu:
        return to == TileState::UploadQueued || to == TileState::Retiring;
    case TileState::UploadQueued:
        return to == TileState::ReadyGpu || to == TileState::Failed;
    case TileState::ReadyGpu:
        return to == TileState::Visible || to == TileState::UsingFallback ||
               to == TileState::Retiring;
    case TileState::Visible:
        return to == TileState::ReadyGpu || to == TileState::UsingFallback ||
               to == TileState::Retiring;
    case TileState::UsingFallback:
        return to == TileState::ReadyGpu || to == TileState::Visible || to == TileState::Retiring;
    case TileState::Failed:
        return to == TileState::Queued || to == TileState::Retiring;
    case TileState::Retiring:
        return to == TileState::Queued || to == TileState::Failed;
    }
    return false;
}

int select_zoom_for_distance(const float distance, const int min_zoom, const int max_zoom)
{
    if (min_zoom > max_zoom)
    {
        throw std::invalid_argument("min zoom must be <= max zoom");
    }
    if (max_zoom == min_zoom)
    {
        return min_zoom;
    }
    if (distance <= 1.5F)
    {
        return max_zoom;
    }
    if (distance <= 6.0F)
    {
        return std::max(min_zoom, max_zoom - 1);
    }
    return min_zoom;
}

std::vector<geo_core::TileCoord>
build_tile_wishlist(const TerrainViewpoint &view, const int zoom, const std::size_t tile_budget)
{
    if (tile_budget == 0)
    {
        return {};
    }
    const int axis = geo_core::tiles_per_axis(zoom);
    const int center_x = std::clamp(static_cast<int>(std::floor(view.center_x)), 0, axis - 1);
    const int center_y = std::clamp(static_cast<int>(std::floor(view.center_y)), 0, axis - 1);
    const int radius = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(tile_budget)))) / 2;

    struct Candidate
    {
        geo_core::TileCoord coord;
        double priority = 0.0;
    };
    std::vector<Candidate> candidates;
    for (int dy = -radius - 1; dy <= radius + 1; ++dy)
    {
        for (int dx = -radius - 1; dx <= radius + 1; ++dx)
        {
            const int x = center_x + dx;
            const int y = center_y + dy;
            if (x < 0 || y < 0 || x >= axis || y >= axis)
            {
                continue;
            }
            const double tile_center_x = static_cast<double>(x) + 0.5;
            const double tile_center_y = static_cast<double>(y) + 0.5;
            const double dist_x = tile_center_x - view.center_x;
            const double dist_y = tile_center_y - view.center_y;
            candidates.push_back(
                {geo_core::TileCoord{zoom, x, y}, std::sqrt(dist_x * dist_x + dist_y * dist_y)});
        }
    }
    std::sort(candidates.begin(),
              candidates.end(),
              [](const Candidate &a, const Candidate &b)
              {
                  if (a.priority != b.priority)
                  {
                      return a.priority < b.priority;
                  }
                  return coord_less(a.coord, b.coord);
              });

    std::vector<geo_core::TileCoord> result;
    result.reserve(std::min(tile_budget, candidates.size()));
    for (const Candidate &candidate : candidates)
    {
        if (result.size() >= tile_budget)
        {
            break;
        }
        result.push_back(candidate.coord);
    }
    return result;
}

TerrainStreamer::TerrainStreamer(TerrainStreamConfig config) : config_(std::move(config))
{
    config_.worker_count = std::max(1, config_.worker_count);
    config_.max_outstanding_jobs = std::max<std::size_t>(1, config_.max_outstanding_jobs);
    l2_rasters_.set_byte_limit(config_.l2_raster_byte_limit);
    for (int index = 0; index < config_.worker_count; ++index)
    {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

TerrainStreamer::~TerrainStreamer()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    work_available_.notify_all();
}

void TerrainStreamer::begin_frame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++frame_;
}

TileRuntimeState &TerrainStreamer::state_for_locked(const geo_core::TileCoord coord)
{
    auto [it, inserted] = states_.try_emplace(coord);
    TileRuntimeState &state = it->second;
    if (inserted)
    {
        state.coord = coord;
        if (coord.z > 0)
        {
            state.parent = geo_core::parent(coord);
        }
    }
    return state;
}

void TerrainStreamer::transition_locked(const geo_core::TileCoord coord, const TileState next)
{
    TileRuntimeState &state = state_for_locked(coord);
    if (!is_legal_tile_transition(state.state, next))
    {
        state.error = std::string("illegal tile transition from ") +
                      std::string(to_string(state.state)) + " to " + std::string(to_string(next));
        state.state = TileState::Failed;
    }
    else
    {
        state.state = next;
    }
    state.state_frame = frame_;
}

void TerrainStreamer::request_tiles(std::vector<TileLoadRequest> requests)
{
    std::sort(requests.begin(),
              requests.end(),
              [](const TileLoadRequest &a, const TileLoadRequest &b)
              {
                  if (a.priority != b.priority)
                  {
                      return a.priority < b.priority;
                  }
                  return coord_less(a.coord, b.coord);
              });

    bool queued_any = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const TileLoadRequest &request : requests)
        {
            if (work_queue_.size() + loading_jobs_ >= config_.max_outstanding_jobs)
            {
                break;
            }
            TileRuntimeState &state = state_for_locked(request.coord);
            if (terminal_or_resident(state.state) && state.state != TileState::Failed)
            {
                continue;
            }
            if (state.state == TileState::Failed &&
                state.request_generation == request.request_generation)
            {
                continue;
            }
            if (state.state == TileState::Queued || state.state == TileState::Loading ||
                state.state == TileState::Decoding || state.state == TileState::Decoded ||
                state.state == TileState::BuildingMesh)
            {
                continue;
            }
            state.priority = request.priority;
            state.requested_frame = frame_;
            state.request_generation = request.request_generation;
            transition_locked(request.coord, TileState::Queued);
            work_queue_.push_back(QueuedRequest{request});
            queued_any = true;
        }
    }
    if (queued_any)
    {
        work_available_.notify_all();
    }
}

std::vector<PreparedTile> TerrainStreamer::drain_ready_cpu(const std::size_t max_tiles,
                                                           const std::size_t max_bytes)
{
    std::vector<PreparedTile> drained;
    std::size_t bytes = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    while (!ready_cpu_queue_.empty() && drained.size() < max_tiles)
    {
        const std::size_t next_bytes = ready_cpu_queue_.front().estimated_cpu_bytes;
        if (!drained.empty() && bytes + next_bytes > max_bytes)
        {
            break;
        }
        bytes += next_bytes;
        drained.push_back(std::move(ready_cpu_queue_.front()));
        ready_cpu_queue_.pop_front();
    }
    return drained;
}

std::vector<PreparedTile> TerrainStreamer::drain_failed()
{
    std::vector<PreparedTile> drained;
    std::lock_guard<std::mutex> lock(mutex_);
    while (!failed_queue_.empty())
    {
        drained.push_back(std::move(failed_queue_.front()));
        failed_queue_.pop_front();
    }
    return drained;
}

void TerrainStreamer::mark_upload_queued(const geo_core::TileCoord coord)
{
    std::lock_guard<std::mutex> lock(mutex_);
    transition_locked(coord, TileState::UploadQueued);
}

void TerrainStreamer::mark_ready_gpu(const geo_core::TileCoord coord)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_for_locked(coord).state == TileState::Missing)
    {
        state_for_locked(coord).state = TileState::ReadyGpu;
        state_for_locked(coord).state_frame = frame_;
        return;
    }
    transition_locked(coord, TileState::ReadyGpu);
}

void TerrainStreamer::mark_visible(const geo_core::TileCoord coord, const bool using_fallback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    transition_locked(coord, using_fallback ? TileState::UsingFallback : TileState::Visible);
}

void TerrainStreamer::mark_retiring(const geo_core::TileCoord coord)
{
    std::lock_guard<std::mutex> lock(mutex_);
    transition_locked(coord, TileState::Retiring);
}

TileState TerrainStreamer::state_of(const geo_core::TileCoord coord) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(coord);
    return it == states_.end() ? TileState::Missing : it->second.state;
}

bool TerrainStreamer::is_ready_gpu(const geo_core::TileCoord coord) const
{
    const TileState state = state_of(coord);
    return state == TileState::ReadyGpu || state == TileState::Visible ||
           state == TileState::UsingFallback;
}

std::optional<geo_core::TileCoord>
TerrainStreamer::best_ready_ancestor_locked(geo_core::TileCoord coord) const
{
    while (coord.z > 0)
    {
        coord = geo_core::parent(coord);
        const auto it = states_.find(coord);
        if (it != states_.end() &&
            (it->second.state == TileState::ReadyGpu || it->second.state == TileState::Visible ||
             it->second.state == TileState::UsingFallback))
        {
            return coord;
        }
    }
    return std::nullopt;
}

std::vector<TileRenderDecision>
TerrainStreamer::choose_visible_tiles(const std::vector<geo_core::TileCoord> &desired) const
{
    std::vector<TileRenderDecision> decisions;
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<geo_core::TileCoord, std::vector<geo_core::TileCoord>> by_parent;
    for (const geo_core::TileCoord coord : desired)
    {
        by_parent[coord.z > 0 ? geo_core::parent(coord) : coord].push_back(coord);
    }

    for (const auto &[parent_coord, children] : by_parent)
    {
        bool all_children_ready = true;
        for (const geo_core::TileCoord child : children)
        {
            const auto it = states_.find(child);
            all_children_ready = all_children_ready && it != states_.end() &&
                                 (it->second.state == TileState::ReadyGpu ||
                                  it->second.state == TileState::Visible ||
                                  it->second.state == TileState::UsingFallback);
        }

        if (all_children_ready)
        {
            for (const geo_core::TileCoord child : children)
            {
                decisions.push_back({child, false});
            }
            continue;
        }

        if (config_.parent_fallback)
        {
            const auto parent_it = states_.find(parent_coord);
            if (parent_it != states_.end() && (parent_it->second.state == TileState::ReadyGpu ||
                                               parent_it->second.state == TileState::Visible ||
                                               parent_it->second.state == TileState::UsingFallback))
            {
                decisions.push_back({parent_coord, true});
                continue;
            }
            if (!children.empty())
            {
                if (const auto ancestor = best_ready_ancestor_locked(children.front()))
                {
                    decisions.push_back({*ancestor, true});
                }
            }
        }
    }

    std::sort(decisions.begin(),
              decisions.end(),
              [](const TileRenderDecision &a, const TileRenderDecision &b)
              { return coord_less(a.coord, b.coord); });
    decisions.erase(std::unique(decisions.begin(),
                                decisions.end(),
                                [](const TileRenderDecision &a, const TileRenderDecision &b)
                                { return a.coord == b.coord; }),
                    decisions.end());
    return decisions;
}

TerrainStreamSnapshot TerrainStreamer::snapshot() const
{
    TerrainStreamSnapshot snapshot;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.queued_jobs = work_queue_.size();
    snapshot.loading_jobs = loading_jobs_;
    snapshot.ready_cpu_tiles = ready_cpu_queue_.size();
    snapshot.resident_gpu_tiles = resident_gpu_tiles_;
    snapshot.resident_gpu_bytes = resident_gpu_bytes_;
    snapshot.cache_stats = cache_stats_;
    snapshot.cache_stats.l1_prepared = CacheStats{l1_prepared_.size(),
                                                  l1_prepared_bytes_,
                                                  config_.l1_prepared_byte_limit,
                                                  l1_prepared_counters_};
    snapshot.cache_stats.l2_raster = l2_rasters_.stats();
    snapshot.cache_stats.l3_disk = CacheStats{0, 0, 0, l3_counters_};
    snapshot.frame = frame_;
    snapshot.tiles.reserve(states_.size());
    for (const auto &[coord, state] : states_)
    {
        (void)coord;
        snapshot.tiles.push_back(state);
        if (state.state == TileState::Failed)
        {
            ++snapshot.failed_tiles;
        }
    }
    std::sort(snapshot.tiles.begin(),
              snapshot.tiles.end(),
              [](const TileRuntimeState &a, const TileRuntimeState &b)
              { return coord_less(a.coord, b.coord); });
    return snapshot;
}

void TerrainStreamer::update_l0_stats(const std::size_t resident_gpu_tiles,
                                      const std::size_t resident_gpu_bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    resident_gpu_tiles_ = resident_gpu_tiles;
    resident_gpu_bytes_ = resident_gpu_bytes;
}

std::optional<PreparedTile> TerrainStreamer::prepared_cache_get_locked(const std::string &key)
{
    const auto it = l1_prepared_index_.find(key);
    if (it == l1_prepared_index_.end())
    {
        ++l1_prepared_counters_.misses;
        return std::nullopt;
    }
    l1_prepared_.splice(l1_prepared_.begin(), l1_prepared_, it->second);
    ++l1_prepared_counters_.hits;
    PreparedTile tile = it->second->tile;
    tile.cache_tier = CacheTier::L1Prepared;
    return tile;
}

void TerrainStreamer::prepared_cache_put_locked(std::string key, PreparedTile tile)
{
    const std::size_t bytes = tile.estimated_cpu_bytes;
    if (const auto it = l1_prepared_index_.find(key); it != l1_prepared_index_.end())
    {
        l1_prepared_bytes_ -= it->second->bytes;
        l1_prepared_.erase(it->second);
        l1_prepared_index_.erase(it);
    }
    l1_prepared_.push_front(PreparedCacheEntry{std::move(key), std::move(tile), bytes});
    l1_prepared_index_[l1_prepared_.front().key] = l1_prepared_.begin();
    l1_prepared_bytes_ += bytes;
    ++l1_prepared_counters_.stores;
    evict_prepared_cache_locked();
}

void TerrainStreamer::evict_prepared_cache_locked()
{
    if (config_.l1_prepared_byte_limit == 0)
    {
        return;
    }
    while (l1_prepared_bytes_ > config_.l1_prepared_byte_limit && !l1_prepared_.empty())
    {
        auto last = std::prev(l1_prepared_.end());
        l1_prepared_bytes_ -= last->bytes;
        l1_prepared_index_.erase(last->key);
        l1_prepared_.erase(last);
        ++l1_prepared_counters_.evictions;
    }
}

std::optional<RasterCacheEntry> TerrainStreamer::synthesize_raster(const TileLoadRequest &request,
                                                                   const LayerSpec &layer,
                                                                   const bool height_layer)
{
    if (request.max_synthesis_depth <= 0)
    {
        return std::nullopt;
    }

    TileLoadRequest source_request = request;
    source_request.max_synthesis_depth = 0;
    if (request.coord.z > 0)
    {
        try
        {
            const geo_core::TileCoord parent_coord = geo_core::parent(request.coord);
            source_request.coord = parent_coord;
            RasterCacheEntry parent = load_raster(source_request, layer, height_layer);
            if (!parent.synthetic)
            {
                RasterCacheEntry entry{
                    synthesize_child_from_parent(parent.raster, parent_coord, request.coord),
                    CacheTier::Synthesis,
                    TileSourceType::Synthetic,
                    true,
                    1,
                    {parent_coord},
                };
                const TileCacheKey key{layer, request.coord};
                if (request.persist_cache && !request.cache_root.empty())
                {
                    const auto stats = entry.raster.format == RasterFormat::Float32
                                           ? float_raster_min_max(entry.raster)
                                           : RasterStats{0.0F, 255.0F};
                    store_l3_raster(
                        request.cache_root,
                        key,
                        entry.raster,
                        PersistedTileInfo{
                            true, entry.source_coords, stats.min_value, stats.max_value, 1});
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++cache_stats_.persisted_tiles;
                    ++l3_counters_.stores;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++cache_stats_.synthesized_tiles;
                }
                return entry;
            }
        }
        catch (const std::exception &)
        {
        }
    }

    try
    {
        const std::array<geo_core::TileCoord, 4> child_coords = geo_core::children(request.coord);
        std::array<Raster, 4> child_rasters;
        std::vector<geo_core::TileCoord> sources;
        for (std::size_t index = 0; index < child_coords.size(); ++index)
        {
            source_request.coord = child_coords[index];
            RasterCacheEntry child = load_raster(source_request, layer, height_layer);
            if (child.synthetic)
            {
                return std::nullopt;
            }
            child_rasters[index] = std::move(child.raster);
            sources.push_back(child_coords[index]);
        }
        RasterCacheEntry entry{
            synthesize_parent_from_children(child_rasters),
            CacheTier::Synthesis,
            TileSourceType::Synthetic,
            true,
            1,
            sources,
        };
        const TileCacheKey key{layer, request.coord};
        if (request.persist_cache && !request.cache_root.empty())
        {
            const auto stats = entry.raster.format == RasterFormat::Float32
                                   ? float_raster_min_max(entry.raster)
                                   : RasterStats{0.0F, 255.0F};
            store_l3_raster(
                request.cache_root,
                key,
                entry.raster,
                PersistedTileInfo{true, entry.source_coords, stats.min_value, stats.max_value, 1});
            std::lock_guard<std::mutex> lock(mutex_);
            ++cache_stats_.persisted_tiles;
            ++l3_counters_.stores;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++cache_stats_.synthesized_tiles;
        }
        return entry;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

RasterCacheEntry
TerrainStreamer::load_raster(TileLoadRequest request, LayerSpec layer, const bool height_layer)
{
    layer.resolution = std::max(1, layer.resolution);
    const TileCacheKey key{layer, request.coord};
    const std::string key_string = cache_key_string(key);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto cached = l2_rasters_.get(key_string))
        {
            cached->tier = CacheTier::L2Raster;
            return *cached;
        }
    }

    if (!request.cache_root.empty())
    {
        try
        {
            if (auto persisted = load_l3_raster(request.cache_root, key))
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++l3_counters_.hits;
                    l2_rasters_.put(key_string, *persisted);
                }
                return *persisted;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            ++l3_counters_.misses;
        }
        catch (const std::exception &)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++l3_counters_.misses;
        }
    }

    try
    {
        Raster raster;
        if (!height_layer)
        {
            RasterCacheEntry source_entry;
            bool found = false;
            if (!request.imagery_mbtiles.empty())
            {
                if (auto result =
                        MbtilesTileSource(request.imagery_mbtiles).load_tile(request.coord, layer))
                {
                    source_entry = RasterCacheEntry{std::move(result->raster),
                                                    CacheTier::LocalXyz,
                                                    result->source_type,
                                                    false,
                                                    0,
                                                    {}};
                    found = true;
                }
            }
            if (!found && !request.remote_imagery_url_template.empty())
            {
                RemoteHttpTileProvider provider;
                provider.url_template = request.remote_imagery_url_template;
                provider.cache_identity = request.remote_imagery_cache_identity;
                provider.user_agent = request.remote_imagery_user_agent;
                provider.timeout =
                    std::chrono::milliseconds(std::max(1, request.remote_imagery_timeout_ms));
                if (auto result = RemoteHttpTileSource(provider).load_tile(request.coord, layer))
                {
                    source_entry = RasterCacheEntry{std::move(result->raster),
                                                    CacheTier::LocalXyz,
                                                    result->source_type,
                                                    false,
                                                    0,
                                                    {}};
                    found = true;
                }
            }
            if (!found)
            {
                raster = load_png_rgba(local_xyz_tile_path(request.pack_root,
                                                           request.imagery_layer,
                                                           request.coord,
                                                           request.imagery_extension));
                source_entry = RasterCacheEntry{
                    std::move(raster), CacheTier::LocalXyz, TileSourceType::LocalXyz, false, 0, {}};
            }
            RasterCacheEntry entry = std::move(source_entry);
            if (request.persist_cache && !request.cache_root.empty())
            {
                store_l3_raster(request.cache_root,
                                key,
                                entry.raster,
                                PersistedTileInfo{false, {}, 0.0F, 255.0F, 0});
                std::lock_guard<std::mutex> lock(mutex_);
                ++cache_stats_.persisted_tiles;
                ++l3_counters_.stores;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                l2_rasters_.put(key_string, entry);
            }
            return entry;
        }
        else if (layer.type == LayerType::Bathymetry)
        {
            raster = load_float32_raster(
                local_xyz_tile_path(
                    request.pack_root, request.elevation_layer, request.coord, "f32"),
                layer.resolution,
                layer.resolution);
        }
        else
        {
            raster =
                decode_terrain_rgb(load_png_rgba(local_xyz_tile_path(request.pack_root,
                                                                     request.elevation_layer,
                                                                     request.coord,
                                                                     request.elevation_extension)));
        }

        RasterCacheEntry entry{
            std::move(raster), CacheTier::LocalXyz, TileSourceType::LocalXyz, false, 0, {}};
        if (request.persist_cache && !request.cache_root.empty())
        {
            const auto stats = entry.raster.format == RasterFormat::Float32
                                   ? float_raster_min_max(entry.raster)
                                   : RasterStats{0.0F, 255.0F};
            store_l3_raster(request.cache_root,
                            key,
                            entry.raster,
                            PersistedTileInfo{false, {}, stats.min_value, stats.max_value, 0});
            std::lock_guard<std::mutex> lock(mutex_);
            ++cache_stats_.persisted_tiles;
            ++l3_counters_.stores;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            l2_rasters_.put(key_string, entry);
        }
        return entry;
    }
    catch (const std::exception &local_error)
    {
        const std::filesystem::path geotiff = layer.type == LayerType::Bathymetry
                                                  ? request.bathymetry_geotiff
                                                  : request.elevation_geotiff;
        if (height_layer && !geotiff.empty())
        {
            try
            {
                Raster raster =
                    GdalGeoTiffTileSource(geotiff).load_tile(request.coord, layer.resolution);
                RasterCacheEntry entry{std::move(raster),
                                       CacheTier::GdalGeoTiff,
                                       TileSourceType::GeoTiff,
                                       false,
                                       0,
                                       {}};
                if (request.persist_cache && !request.cache_root.empty())
                {
                    const auto stats = float_raster_min_max(entry.raster);
                    store_l3_raster(
                        request.cache_root,
                        key,
                        entry.raster,
                        PersistedTileInfo{false, {}, stats.min_value, stats.max_value, 0});
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++cache_stats_.persisted_tiles;
                    ++l3_counters_.stores;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    l2_rasters_.put(key_string, entry);
                }
                return entry;
            }
            catch (const std::exception &)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++cache_stats_.geotiff_extraction_failures;
            }
        }

        if (auto synthesized = synthesize_raster(request, layer, height_layer))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            l2_rasters_.put(key_string, *synthesized);
            return *synthesized;
        }
        throw local_error;
    }
}

PreparedTile TerrainStreamer::prepare_tile(const TileLoadRequest &request)
{
    if (request.simulate_slow_load_ms > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(request.simulate_slow_load_ms));
    }

    const std::string prepared_key =
        cache_key_string(TileCacheKey{request.imagery_spec, request.coord}) + '+' +
        cache_key_string(TileCacheKey{request.elevation_spec, request.coord});
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto cached = prepared_cache_get_locked(prepared_key))
        {
            cached->request_generation = request.request_generation;
            return *cached;
        }
    }

    PreparedTile prepared;
    prepared.coord = request.coord;
    prepared.request_generation = request.request_generation;
    RasterCacheEntry imagery = load_raster(request, request.imagery_spec, false);
    RasterCacheEntry elevation = load_raster(request, request.elevation_spec, true);
    std::optional<Raster> bathymetry;
    if (request.use_bathymetry)
    {
        TileLoadRequest bathy_request = request;
        bathy_request.elevation_layer = "bathymetry";
        try
        {
            bathymetry = load_raster(bathy_request, request.bathymetry_spec, true).raster;
        }
        catch (const std::exception &)
        {
            bathymetry = std::nullopt;
        }
    }
    prepared.imagery = std::move(imagery.raster);
    prepared.heights = merge_elevation_bathymetry(elevation.raster, bathymetry);
    const RasterStats stats = float_raster_min_max(prepared.heights);
    prepared.min_height_m = stats.min_value;
    prepared.max_height_m = stats.max_value;
    prepared.mesh = build_terrain_mesh(prepared.heights, request.mesh_options);
    prepared.estimated_cpu_bytes = estimate_raster_bytes(prepared.imagery) +
                                   estimate_raster_bytes(prepared.heights) +
                                   estimate_mesh_bytes(prepared.mesh);
    prepared.cache_tier = elevation.tier;
    prepared.source_type = elevation.source_type;
    prepared.synthetic = elevation.synthetic || imagery.synthetic;
    prepared.synthesis_depth = std::max(elevation.synthesis_depth, imagery.synthesis_depth);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        prepared_cache_put_locked(prepared_key, prepared);
    }
    return prepared;
}

void TerrainStreamer::worker_loop()
{
    while (true)
    {
        TileLoadRequest request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_available_.wait(lock, [this] { return stopping_ || !work_queue_.empty(); });
            if (stopping_ && work_queue_.empty())
            {
                return;
            }
            request = std::move(work_queue_.front().request);
            work_queue_.pop_front();
            ++loading_jobs_;
            transition_locked(request.coord, TileState::Loading);
        }

        PreparedTile prepared;
        try
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                transition_locked(request.coord, TileState::Decoding);
            }
            prepared = prepare_tile(request);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                transition_locked(request.coord, TileState::Decoded);
                transition_locked(request.coord, TileState::BuildingMesh);
                TileRuntimeState &state = state_for_locked(request.coord);
                state.min_height_m = prepared.min_height_m;
                state.max_height_m = prepared.max_height_m;
                state.estimated_cpu_bytes = prepared.estimated_cpu_bytes;
                state.cache_tier = prepared.cache_tier;
                state.source_type = prepared.source_type;
                state.synthetic = prepared.synthetic;
                state.synthesis_depth = prepared.synthesis_depth;
                transition_locked(request.coord, TileState::ReadyCpu);
                ready_cpu_queue_.push_back(std::move(prepared));
                --loading_jobs_;
            }
        }
        catch (const std::exception &error)
        {
            prepared.coord = request.coord;
            prepared.request_generation = request.request_generation;
            prepared.error = error.what();
            std::lock_guard<std::mutex> lock(mutex_);
            TileRuntimeState &state = state_for_locked(request.coord);
            state.error = prepared.error;
            transition_locked(request.coord, TileState::Failed);
            failed_queue_.push_back(std::move(prepared));
            --loading_jobs_;
        }
    }
}

} // namespace animus::terrain_core
