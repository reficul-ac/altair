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
    layouts["terrain"].inspector_visible = false;

    const auto reset = animus::app::reset_workspace_layout(layouts, "analyze");

    EXPECT_TRUE(reset.created_default);
    EXPECT_FLOAT_EQ(layouts["analyze"].plot_shelf_height_px,
                    animus::app::default_workspace_layout("analyze").plot_shelf_height_px);
    EXPECT_FALSE(layouts["terrain"].inspector_visible);
}
