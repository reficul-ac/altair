#include "animus/terrain_lab/terrain_data.hpp"

#include <cmath>
#include <filesystem>

#include <gtest/gtest.h>

namespace {

using animus::terrain_lab::HeightGrid;
using animus::terrain_lab::RgbaImage;
using animus::terrain_lab::XyzTile;
using animus::terrain_lab::build_tile_mesh;
using animus::terrain_lab::decode_terrain_rgb;
using animus::terrain_lab::terrain_rgb_to_meters;
using animus::terrain_lab::tile_png_path;

TEST(TerrainLabData, TerrainRgbKnownValuesDecodeToMeters)
{
    EXPECT_FLOAT_EQ(terrain_rgb_to_meters(0, 0, 0), -10000.0F);
    EXPECT_FLOAT_EQ(terrain_rgb_to_meters(1, 134, 160), 0.0F);
    EXPECT_NEAR(terrain_rgb_to_meters(1, 150, 0), 393.6F, 1.0e-3F);
}

TEST(TerrainLabData, XyzTilePathsUseLayerZoomXAndY)
{
    const std::filesystem::path root = "animus/data/tiles/lake_tahoe";
    EXPECT_EQ(
        tile_png_path(root, "imagery", XyzTile{12, 682, 1563}).generic_string(),
        "animus/data/tiles/lake_tahoe/imagery/12/682/1563.png");
    EXPECT_EQ(
        tile_png_path(root, "elevation", XyzTile{12, 681, 1562}).generic_string(),
        "animus/data/tiles/lake_tahoe/elevation/12/681/1562.png");
}

TEST(TerrainLabData, DecodeTerrainRgbTracksMinMax)
{
    const RgbaImage image{
        2,
        1,
        {
            0, 0, 0, 255,
            1, 134, 160, 255,
        },
    };

    const auto grid = decode_terrain_rgb(image);
    ASSERT_EQ(grid.meters.size(), 2U);
    EXPECT_FLOAT_EQ(grid.meters[0], -10000.0F);
    EXPECT_FLOAT_EQ(grid.meters[1], 0.0F);
    EXPECT_FLOAT_EQ(grid.min_meters, -10000.0F);
    EXPECT_FLOAT_EQ(grid.max_meters, 0.0F);
}

TEST(TerrainLabData, MeshGenerationBuildsGridAndSkirts)
{
    const HeightGrid heights{
        3,
        2,
        {
            10.0F, 20.0F, 30.0F,
            40.0F, 50.0F, 60.0F,
        },
        10.0F,
        60.0F,
    };

    const auto mesh = build_tile_mesh(heights, -1.0F, 2.0F, 4.0F, 0.01F, 0.25F);

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

    for (const auto& vertex : mesh.vertices) {
        EXPECT_TRUE(std::isfinite(vertex.x));
        EXPECT_TRUE(std::isfinite(vertex.y));
        EXPECT_TRUE(std::isfinite(vertex.z));
        EXPECT_TRUE(std::isfinite(vertex.u));
        EXPECT_TRUE(std::isfinite(vertex.v));
    }
    for (const auto index : mesh.indices) {
        EXPECT_LT(index, mesh.vertices.size());
    }
}

} // namespace
