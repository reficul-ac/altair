#include "workspace_layout_model.hpp"

#include <gtest/gtest.h>

TEST(WorkspaceLayoutModel, SwitchCreatesMissingDefaultWithoutOverwritingExisting)
{
    std::map<std::string, animus::app::AppWorkspaceLayout> layouts;
    auto existing = animus::app::default_workspace_layout("analyze");
    existing.plot_shelf_height_px = 333.0F;
    layouts["analyze"] = existing;

    const auto analyze = animus::app::workspace_layout_for_switch(layouts, "advanced");
    const auto plan = animus::app::workspace_layout_for_switch(layouts, "plan");

    EXPECT_FALSE(analyze.created_default);
    EXPECT_FLOAT_EQ(analyze.layout.plot_shelf_height_px, 333.0F);
    EXPECT_TRUE(plan.created_default);
    EXPECT_TRUE(layouts.contains("plan"));
}

TEST(WorkspaceLayoutModel, ResetOnlyReplacesCurrentWorkspace)
{
    std::map<std::string, animus::app::AppWorkspaceLayout> layouts;
    layouts["analyze"] = animus::app::default_workspace_layout("analyze");
    layouts["terrain"] = animus::app::default_workspace_layout("terrain");
    layouts["analyze"].plot_shelf_height_px = 333.0F;
    layouts["analyze"].main_panel = {42.0F, 43.0F, 444.0F, 445.0F};
    layouts["analyze"].inspector = {900.0F, 80.0F, 280.0F, 520.0F};
    layouts["terrain"].main_panel = {123.0F, 124.0F, 400.0F, 401.0F};
    layouts["terrain"].inspector_visible = false;

    const auto reset = animus::app::reset_workspace_layout(layouts, "analyze");

    EXPECT_TRUE(reset.created_default);
    EXPECT_FLOAT_EQ(layouts["analyze"].plot_shelf_height_px,
                    animus::app::default_workspace_layout("analyze").plot_shelf_height_px);
    EXPECT_EQ(layouts["analyze"].main_panel,
              animus::app::default_workspace_layout("analyze").main_panel);
    EXPECT_EQ(layouts["analyze"].inspector,
              animus::app::default_workspace_layout("analyze").inspector);
    EXPECT_FLOAT_EQ(layouts["terrain"].main_panel.x, 123.0F);
    EXPECT_FALSE(layouts["terrain"].inspector_visible);
}

TEST(WorkspaceLayoutModel, ExportAliasCreatesExportDefault)
{
    std::map<std::string, animus::app::AppWorkspaceLayout> layouts;

    const auto transition = animus::app::workspace_layout_for_switch(layouts, "report");

    EXPECT_EQ(transition.workspace_id, "export");
    EXPECT_TRUE(transition.created_default);
    EXPECT_TRUE(layouts.contains("export"));
    EXPECT_EQ(transition.layout.active_panel, "capture");
    EXPECT_EQ(transition.layout.bottom_drawer_state, animus::app::BottomDrawerState::Hidden);
}

TEST(WorkspaceLayoutModel, SafeViewportSubtractsVisibleChrome)
{
    const auto rect =
        animus::app::safe_viewport_rect({1440.0F, 900.0F, 38.0F, 150.0F, 340.0F, 160.0F, 12.0F});

    EXPECT_FLOAT_EQ(rect.x, 162.0F);
    EXPECT_FLOAT_EQ(rect.y, 50.0F);
    EXPECT_FLOAT_EQ(rect.width, 926.0F);
    EXPECT_FLOAT_EQ(rect.height, 678.0F);
}
