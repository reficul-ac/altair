#pragma once

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace animus::terrain_core
{

struct CacheMetadataRecord
{
    std::string layer_identity;
    TileSourceType source_type = TileSourceType::None;
    std::string provenance;
    std::string source_identity;
    std::int64_t created_unix_s = 0;
    std::int64_t updated_unix_s = 0;
    std::uint64_t byte_size = 0;
    geo_core::TileCoord coord;
    std::vector<geo_core::TileCoord> source_coords;
    std::string validation_status = "unknown";
};

class CacheMetadataStore
{
  public:
    explicit CacheMetadataStore(std::filesystem::path cache_root);
    void initialize();
    void upsert(const CacheMetadataRecord &record);
    [[nodiscard]] std::optional<CacheMetadataRecord> read(const std::string &layer_identity,
                                                          geo_core::TileCoord coord) const;
    [[nodiscard]] std::filesystem::path database_path() const;

  private:
    std::filesystem::path cache_root_;
};

} // namespace animus::terrain_core
