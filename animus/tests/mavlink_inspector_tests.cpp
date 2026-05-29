#include "mavlink_inspector.hpp"

#include <gtest/gtest.h>

TEST(AnimusMavlinkInspector, PlotThisCreatesAndReusesDedicatedPlot)
{
    animus::app::PlotShelfConfig config;
    config.plots.clear();
    animus::app::PlotUiState state;
    const animus::app::SignalCatalog catalog;

    const auto first = animus::app::plot_mavlink_inspector_field(
        config, state, catalog, "ATTITUDE", "roll", animus::app::MavlinkInspectorPlotTarget::Dedicated);
    ASSERT_NE(first.plot, nullptr);
    ASSERT_NE(first.series, nullptr);
    EXPECT_TRUE(first.created_plot);
    EXPECT_TRUE(first.created_series);
    EXPECT_EQ(config.plots.size(), 1U);
    EXPECT_EQ(first.plot->title, "MAVLink Inspector");

    const auto second = animus::app::plot_mavlink_inspector_field(
        config, state, catalog, "ATTITUDE", "roll", animus::app::MavlinkInspectorPlotTarget::Dedicated);
    EXPECT_FALSE(second.created_plot);
    EXPECT_FALSE(second.created_series);
    ASSERT_EQ(config.plots.front().series.size(), 1U);
}

TEST(AnimusMavlinkInspector, AddToExistingPlotUsesSelectedPlotAndAvoidsDuplicates)
{
    animus::app::PlotShelfConfig config = animus::app::default_plot_shelf_config();
    animus::app::PlotUiState state;
    state.selected_plot_id = "speed_climb";
    const animus::app::SignalCatalog catalog;

    const auto first = animus::app::plot_mavlink_inspector_field(
        config,
        state,
        catalog,
        "GLOBAL_POSITION_INT",
        "relative_alt",
        animus::app::MavlinkInspectorPlotTarget::Existing);
    ASSERT_NE(first.plot, nullptr);
    ASSERT_NE(first.series, nullptr);
    EXPECT_EQ(first.plot->id, "speed_climb");
    EXPECT_TRUE(first.created_series);

    const auto second = animus::app::plot_mavlink_inspector_field(
        config,
        state,
        catalog,
        "GLOBAL_POSITION_INT",
        "relative_alt",
        animus::app::MavlinkInspectorPlotTarget::Existing);
    EXPECT_FALSE(second.created_series);
    EXPECT_EQ(first.plot->series.size(), 3U);
}

TEST(AnimusMavlinkInspector, GeneratedSeriesUsesCatalogDefaults)
{
    const animus::app::SignalCatalog catalog;

    const auto series = animus::app::make_mavlink_inspector_series(catalog, "ATTITUDE", "roll");
    ASSERT_TRUE(series);
    EXPECT_EQ(series->signal.source, animus::app::SignalSource::Mavlink);
    EXPECT_EQ(series->signal.mavlink_message, "ATTITUDE");
    EXPECT_EQ(series->signal.mavlink_field, "roll");
    EXPECT_EQ(series->signal.field_path, "mavlink.ATTITUDE.roll");
    EXPECT_EQ(series->transform, animus::app::SignalTransform::RadToDeg);
    EXPECT_EQ(series->label, "ATTITUDE Roll");
    EXPECT_EQ(series->unit_override, "rad");
    EXPECT_EQ(series->entity_binding, animus::app::PlotEntityBindingMode::Selected);
    EXPECT_FALSE(series->explicit_entity);
}

TEST(AnimusMavlinkInspector, NonNumericFieldsAreNotPlottable)
{
    const animus::app::SignalCatalog catalog;

    const auto series = animus::app::make_mavlink_inspector_series(catalog, "HEARTBEAT", "type");
    EXPECT_FALSE(series);
}
