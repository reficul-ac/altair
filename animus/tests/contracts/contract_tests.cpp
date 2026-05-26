#include "animus/data_core/cache_key.hpp"
#include "animus/geo_core/tile_math.hpp"
#include "animus/terrain_core/contracts.hpp"

#include <array>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

#include <gtest/gtest.h>

namespace
{

using animus::data_core::cache_component;
using animus::data_core::layer_cache_prefix;
using animus::data_core::tile_cache_key;
using animus::geo_core::children;
using animus::geo_core::GeoBounds;
using animus::geo_core::hash_value;
using animus::geo_core::is_valid;
using animus::geo_core::lat_lon_to_tile;
using animus::geo_core::lat_lon_to_tile_uv;
using animus::geo_core::max_tile_zoom;
using animus::geo_core::parent;
using animus::geo_core::tile_key;
using animus::geo_core::tile_to_bounds;
using animus::geo_core::TileCoord;
using animus::geo_core::tiles_per_axis;
using animus::geo_core::Vec2;
using animus::geo_core::web_mercator_max_latitude_deg;
using animus::terrain_core::LayerSpec;
using animus::terrain_core::LayerType;
using animus::terrain_core::Raster;
using animus::terrain_core::RasterFormat;
using animus::terrain_core::SamplingMode;
using animus::terrain_core::TileState;
using animus::terrain_core::to_string;

TEST(TerrainContracts, LayerSpecDefaultsMatchArchitecture)
{
    const LayerSpec spec;

    EXPECT_EQ(spec.type, LayerType::Imagery);
    EXPECT_TRUE(spec.source.empty());
    EXPECT_TRUE(spec.style.empty());
    EXPECT_TRUE(spec.extra.empty());
    EXPECT_EQ(spec.resolution, 256);
    EXPECT_EQ(spec.min_zoom, 0);
    EXPECT_EQ(spec.max_zoom, 18);
}

TEST(TerrainContracts, RasterDefaultsAreEmptyCenterSampledRgba)
{
    const Raster raster;

    EXPECT_EQ(raster.width, 0);
    EXPECT_EQ(raster.height, 0);
    EXPECT_EQ(raster.channels, 0);
    EXPECT_EQ(raster.format, RasterFormat::UInt8RGBA);
    EXPECT_EQ(raster.sampling_mode, SamplingMode::Center);
    EXPECT_TRUE(raster.float_data.empty());
    EXPECT_TRUE(raster.byte_data.empty());
    EXPECT_FALSE(raster.no_data_value.has_value());
}

TEST(DataContracts, CacheComponentsAreStableAndPathSafe)
{
    EXPECT_EQ(cache_component("Terrain RGB/Local Pack"), "terrain_rgb_local_pack");
    EXPECT_EQ(cache_component(""), "_");
}

TEST(DataContracts, LayerCachePrefixIncludesLayerIdentity)
{
    LayerSpec spec;
    spec.type = LayerType::Elevation;
    spec.source = "Local DEM";
    spec.style = "Terrain RGB";
    spec.extra = "Meters";
    spec.resolution = 512;
    spec.min_zoom = 3;
    spec.max_zoom = 12;

    EXPECT_EQ(layer_cache_prefix(spec), "elevation/local_dem/terrain_rgb/meters/512/3-12");
}

TEST(GeoCore, TileCoordEqualityOrderingValidityKeyAndHashAreStable)
{
    const TileCoord tile{3, 4, 5};
    const TileCoord same{3, 4, 5};
    const TileCoord other{3, 4, 6};

    EXPECT_EQ(tile, same);
    EXPECT_LT(tile, other);
    EXPECT_TRUE(is_valid(tile));
    EXPECT_FALSE(is_valid(TileCoord{-1, 0, 0}));
    EXPECT_FALSE(is_valid(TileCoord{2, 4, 0}));
    EXPECT_EQ(tiles_per_axis(3), 8);
    EXPECT_EQ(tile_key(tile), "3/4/5");
    EXPECT_EQ(hash_value(tile), hash_value(same));
    EXPECT_THROW((void)hash_value(TileCoord{2, 4, 0}), std::invalid_argument);

    const std::unordered_set<TileCoord> set{tile};
    EXPECT_EQ(set.count(same), 1U);
}

TEST(GeoCore, LatLonToTileUsesXyzWebMercatorClampRules)
{
    EXPECT_EQ(lat_lon_to_tile(0.0, 0.0, 0), (TileCoord{0, 0, 0}));
    EXPECT_EQ(lat_lon_to_tile(0.0, 0.0, 2), (TileCoord{2, 2, 2}));
    EXPECT_EQ(lat_lon_to_tile(0.1, 0.1, 1), (TileCoord{1, 1, 0}));
    EXPECT_EQ(lat_lon_to_tile(0.0, -180.0, 2), (TileCoord{2, 0, 2}));
    EXPECT_EQ(lat_lon_to_tile(0.0, 180.0, 2), (TileCoord{2, 3, 2}));
    EXPECT_EQ(lat_lon_to_tile(0.0, 181.0, 2), (TileCoord{2, 3, 2}));
    EXPECT_EQ(lat_lon_to_tile(web_mercator_max_latitude_deg + 10.0, 0.0, 3), (TileCoord{3, 4, 0}));
    EXPECT_EQ(lat_lon_to_tile(-web_mercator_max_latitude_deg - 10.0, 0.0, 3), (TileCoord{3, 4, 7}));

    EXPECT_THROW((void)lat_lon_to_tile(0.0, 0.0, -1), std::invalid_argument);
    EXPECT_THROW((void)lat_lon_to_tile(0.0, 0.0, max_tile_zoom + 1), std::invalid_argument);
}

TEST(GeoCore, TileToBoundsReturnsKnownLowZoomBounds)
{
    const GeoBounds root = tile_to_bounds(TileCoord{0, 0, 0});
    EXPECT_NEAR(root.south_deg, -web_mercator_max_latitude_deg, 1e-12);
    EXPECT_DOUBLE_EQ(root.west_deg, -180.0);
    EXPECT_NEAR(root.north_deg, web_mercator_max_latitude_deg, 1e-12);
    EXPECT_DOUBLE_EQ(root.east_deg, 180.0);

    const GeoBounds northwest = tile_to_bounds(TileCoord{1, 0, 0});
    EXPECT_NEAR(northwest.south_deg, 0.0, 1e-12);
    EXPECT_DOUBLE_EQ(northwest.west_deg, -180.0);
    EXPECT_NEAR(northwest.north_deg, web_mercator_max_latitude_deg, 1e-12);
    EXPECT_DOUBLE_EQ(northwest.east_deg, 0.0);

    const GeoBounds southeast = tile_to_bounds(TileCoord{1, 1, 1});
    EXPECT_NEAR(southeast.south_deg, -web_mercator_max_latitude_deg, 1e-12);
    EXPECT_DOUBLE_EQ(southeast.west_deg, 0.0);
    EXPECT_NEAR(southeast.north_deg, 0.0, 1e-12);
    EXPECT_DOUBLE_EQ(southeast.east_deg, 180.0);

    EXPECT_THROW((void)tile_to_bounds(TileCoord{2, 4, 0}), std::invalid_argument);
}

TEST(GeoCore, LatLonToTileUvUsesTopLeftOrigin)
{
    const Vec2 root_center = lat_lon_to_tile_uv(0.0, 0.0, TileCoord{0, 0, 0});
    EXPECT_NEAR(root_center.u, 0.5, 1e-12);
    EXPECT_NEAR(root_center.v, 0.5, 1e-12);

    const TileCoord northwest_tile{1, 0, 0};
    const GeoBounds northwest_bounds = tile_to_bounds(northwest_tile);

    const Vec2 northwest =
        lat_lon_to_tile_uv(northwest_bounds.north_deg, northwest_bounds.west_deg, northwest_tile);
    EXPECT_NEAR(northwest.u, 0.0, 1e-12);
    EXPECT_NEAR(northwest.v, 0.0, 1e-12);

    const Vec2 southeast =
        lat_lon_to_tile_uv(northwest_bounds.south_deg, northwest_bounds.east_deg, northwest_tile);
    EXPECT_NEAR(southeast.u, 1.0, 1e-12);
    EXPECT_NEAR(southeast.v, 1.0, 1e-12);

    EXPECT_THROW((void)lat_lon_to_tile_uv(0.0, 0.0, TileCoord{2, 4, 0}), std::invalid_argument);
}

TEST(GeoCore, ParentAndChildrenFollowXyzQuadtree)
{
    EXPECT_EQ(parent(TileCoord{0, 0, 0}), (TileCoord{0, 0, 0}));
    EXPECT_EQ(parent(TileCoord{3, 5, 6}), (TileCoord{2, 2, 3}));

    const std::array<TileCoord, 4> expected{
        TileCoord{3, 4, 6},
        TileCoord{3, 5, 6},
        TileCoord{3, 4, 7},
        TileCoord{3, 5, 7},
    };
    EXPECT_EQ(children(TileCoord{2, 2, 3}), expected);

    EXPECT_THROW((void)parent(TileCoord{2, 4, 0}), std::invalid_argument);
    EXPECT_THROW((void)children(TileCoord{max_tile_zoom, 0, 0}), std::invalid_argument);
}

TEST(DataContracts, TileCacheKeyAppendsStableTileKey)
{
    LayerSpec spec;
    spec.type = LayerType::Imagery;
    spec.source = "Local Pack";
    spec.style = "Natural Color";
    spec.extra = "V1";
    spec.resolution = 256;
    spec.min_zoom = 0;
    spec.max_zoom = 14;

    EXPECT_EQ(tile_cache_key(spec, TileCoord{6, 10, 22}),
              "imagery/local_pack/natural_color/v1/256/0-14/6/10/22");
}

TEST(TerrainContracts, TileStateStringsCoverAllDeclaredStates)
{
    constexpr std::array<TileState, 13> states = {
        TileState::Missing,
        TileState::Queued,
        TileState::Loading,
        TileState::Decoding,
        TileState::Decoded,
        TileState::BuildingMesh,
        TileState::ReadyCpu,
        TileState::UploadQueued,
        TileState::ReadyGpu,
        TileState::Visible,
        TileState::Failed,
        TileState::UsingFallback,
        TileState::Retiring,
    };

    for (const TileState state : states)
    {
        EXPECT_NE(to_string(state), std::string_view("unknown"));
        EXPECT_FALSE(to_string(state).empty());
    }
}

} // namespace
