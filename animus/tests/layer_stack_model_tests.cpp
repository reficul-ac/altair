#include "layer_stack_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

const animus::app::LayerStackRow *find_row(const std::vector<animus::app::LayerStackRow> &rows,
                                           const std::string &id)
{
    const auto it =
        std::find_if(rows.begin(),
                     rows.end(),
                     [&id](const animus::app::LayerStackRow &row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

} // namespace

TEST(AnimusLayerStackModel, RowsMatchPhase10List)
{
    animus::app::AppLayerSettings settings;
    animus::app::LayerStackContext context;
    context.telemetry_loaded = true;
    context.plan_loaded = true;
    context.geotiff_configured = true;
    context.bathymetry_configured = true;
    context.terrain_tiles_loaded = true;
    context.terrain_confidence_available = true;

    const auto rows = animus::app::build_layer_stack_rows(settings, context);

    ASSERT_EQ(rows.size(), 13U);
    EXPECT_NE(find_row(rows, "vehicle_icons"), nullptr);
    EXPECT_NE(find_row(rows, "vehicle_labels"), nullptr);
    EXPECT_NE(find_row(rows, "track_tail"), nullptr);
    EXPECT_NE(find_row(rows, "heading_vectors"), nullptr);
    EXPECT_NE(find_row(rows, "planned_route"), nullptr);
    EXPECT_NE(find_row(rows, "geofence_rally"), nullptr);
    EXPECT_NE(find_row(rows, "terrain_confidence"), nullptr);
    EXPECT_NE(find_row(rows, "terrain_clearance_heatmap"), nullptr);
    EXPECT_NE(find_row(rows, "geotiff_overlay"), nullptr);
    EXPECT_NE(find_row(rows, "bathymetry"), nullptr);
    EXPECT_NE(find_row(rows, "hillshade"), nullptr);
    EXPECT_NE(find_row(rows, "tile_state_debug"), nullptr);
    EXPECT_NE(find_row(rows, "fallback_highlight"), nullptr);
}

TEST(AnimusLayerStackModel, MissingSourcesSurfaceWarnings)
{
    animus::app::AppLayerSettings settings;
    animus::app::LayerStackContext context;
    context.geotiff_configured = true;
    context.geotiff_missing = true;
    context.bathymetry_configured = true;
    context.bathymetry_missing = true;
    context.plan_error = true;

    const auto rows = animus::app::build_layer_stack_rows(settings, context);

    ASSERT_NE(find_row(rows, "geotiff_overlay"), nullptr);
    EXPECT_EQ(find_row(rows, "geotiff_overlay")->warning, animus::app::LayerWarningLevel::Warning);
    ASSERT_NE(find_row(rows, "bathymetry"), nullptr);
    EXPECT_EQ(find_row(rows, "bathymetry")->warning, animus::app::LayerWarningLevel::Warning);
    ASSERT_NE(find_row(rows, "planned_route"), nullptr);
    EXPECT_EQ(find_row(rows, "planned_route")->status, "error");
}

TEST(AnimusLayerStackModel, TileFailuresAndFallbacksSurfaceBadges)
{
    animus::app::AppLayerSettings settings;
    settings.tile_state_debug_visible = true;
    settings.fallback_highlight_visible = true;
    animus::app::LayerStackContext context;
    context.failed_tiles = 2U;
    context.fallback_tiles = 3U;
    context.synthetic_tiles = 1U;
    context.terrain_confidence_available = true;

    const auto rows = animus::app::build_layer_stack_rows(settings, context);

    ASSERT_NE(find_row(rows, "tile_state_debug"), nullptr);
    EXPECT_EQ(find_row(rows, "tile_state_debug")->warning_badge, "failures");
    ASSERT_NE(find_row(rows, "fallback_highlight"), nullptr);
    EXPECT_EQ(find_row(rows, "fallback_highlight")->warning_badge, "fallback");
    ASSERT_NE(find_row(rows, "terrain_confidence"), nullptr);
    EXPECT_EQ(find_row(rows, "terrain_confidence")->warning_badge, "fallback");
}

TEST(AnimusLayerStackModel, PresetsOnlyChangeLayerSettings)
{
    animus::app::AppLayerSettings settings;
    settings.geotiff_overlay_visible = false;
    settings.geotiff_overlay_opacity = 0.42F;
    settings.geotiff_overlay_draw_order = 7;

    const auto debug = animus::app::layer_preset_debug_tiles(settings);
    EXPECT_TRUE(debug.tile_state_debug_visible);
    EXPECT_TRUE(debug.fallback_highlight_visible);
    EXPECT_FALSE(debug.geotiff_overlay_visible);
    EXPECT_FLOAT_EQ(debug.geotiff_overlay_opacity, 0.42F);
    EXPECT_EQ(debug.geotiff_overlay_draw_order, 7);

    const auto capture = animus::app::layer_preset_capture_mode(settings, true);
    EXPECT_FALSE(capture.vehicle_labels_visible);
    EXPECT_FALSE(capture.heading_vectors_visible);
    EXPECT_TRUE(capture.planned_route_visible);
    EXPECT_TRUE(capture.geofence_rally_visible);
}
