#pragma once

#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"
#include "animus/terrain_core/terrain_data.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace animus::terrain_core
{

struct TileSourceResult
{
    Raster raster;
    TileSourceType source_type = TileSourceType::None;
    std::string provenance;
};

class TileSource
{
  public:
    virtual ~TileSource() = default;
    [[nodiscard]] virtual TileSourceType type() const = 0;
    [[nodiscard]] virtual std::string cache_identity() const = 0;
    [[nodiscard]] virtual std::optional<TileSourceResult>
    load_tile(geo_core::TileCoord coord, const LayerSpec &layer) const = 0;
};

class LocalXyzTileSource final : public TileSource
{
  public:
    LocalXyzTileSource(std::filesystem::path root, std::string layer_name, std::string extension);

    [[nodiscard]] TileSourceType type() const override;
    [[nodiscard]] std::string cache_identity() const override;
    [[nodiscard]] std::optional<TileSourceResult> load_tile(geo_core::TileCoord coord,
                                                            const LayerSpec &layer) const override;

  private:
    std::filesystem::path root_;
    std::string layer_name_;
    std::string extension_;
};

class MbtilesTileSource final : public TileSource
{
  public:
    explicit MbtilesTileSource(std::filesystem::path path, bool tms_y_flip = true);

    [[nodiscard]] TileSourceType type() const override;
    [[nodiscard]] std::string cache_identity() const override;
    [[nodiscard]] std::optional<TileSourceResult> load_tile(geo_core::TileCoord coord,
                                                            const LayerSpec &layer) const override;
    [[nodiscard]] std::optional<std::string> metadata_value(const std::string &name) const;

  private:
    std::filesystem::path path_;
    bool tms_y_flip_ = true;
};

struct RemoteHttpTileProvider
{
    std::string url_template;
    std::string cache_identity;
    std::string user_agent = "Animus/0.1";
    std::vector<std::string> headers;
    std::chrono::milliseconds timeout{5000};
};

class RemoteHttpTileSource final : public TileSource
{
  public:
    explicit RemoteHttpTileSource(RemoteHttpTileProvider provider);

    [[nodiscard]] TileSourceType type() const override;
    [[nodiscard]] std::string cache_identity() const override;
    [[nodiscard]] std::string url_for(geo_core::TileCoord coord) const;
    [[nodiscard]] std::optional<TileSourceResult> load_tile(geo_core::TileCoord coord,
                                                            const LayerSpec &layer) const override;

  private:
    RemoteHttpTileProvider provider_;
};

class SyntheticTileSource final : public TileSource
{
  public:
    [[nodiscard]] TileSourceType type() const override;
    [[nodiscard]] std::string cache_identity() const override;
    [[nodiscard]] std::optional<TileSourceResult> load_tile(geo_core::TileCoord coord,
                                                            const LayerSpec &layer) const override;
};

} // namespace animus::terrain_core
