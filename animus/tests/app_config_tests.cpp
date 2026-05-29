#include "app_config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace
{

std::filesystem::path temp_path(const std::string &name)
{
    return std::filesystem::temp_directory_path() / name;
}

bool has_diagnostic(const std::vector<std::string> &diagnostics, const std::string &needle)
{
    for (const std::string &diagnostic : diagnostics)
    {
        if (diagnostic.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(AnimusAppConfig, DefaultsAreVersioned)
{
    const animus::app::AppConfig config = animus::app::default_app_config();

    EXPECT_EQ(config.version, 1);
    EXPECT_EQ(config.workspace_mode, "fly_test");
    EXPECT_EQ(config.view_mode, "terrain3d");
    EXPECT_EQ(config.telemetry_live_udp_port, 14550U);
    EXPECT_EQ(config.plots.plots.size(), 5U);
}

TEST(AnimusAppConfig, SavesAndLoadsRoundTrip)
{
    const std::filesystem::path path = temp_path("animus_app_config_round_trip.yaml");
    animus::app::AppConfig config;
    config.workspace_mode = "developer";
    config.active_panel = "settings";
    config.view_mode = "map2d";
    config.follow_selected = true;
    config.map_orientation = "track_up";
    config.overlay_enabled = false;
    config.overlay_opacity = 0.25F;
    config.mavlink_inspector_visible = false;
    config.telemetry_live_udp_host = "0.0.0.0";
    config.telemetry_live_udp_port = 14560U;
    config.overlays.push_back({"overlay.tif", true, 0.5F, 3, "geotiff:overlay.tif"});
    config.plots.paused = true;
    config.plots.plots.front().title = "Round Trip Plot";
    config.workspace_layouts["analyze"] = animus::app::default_workspace_layout("analyze");
    config.workspace_layouts["analyze"].main_panel = {200.0F, 80.0F, 520.0F, 460.0F};
    config.workspace_layouts["analyze"].plot_shelf_height_px = 320.0F;

    const auto save = animus::app::save_app_config_file(path, config);
    ASSERT_TRUE(save.saved);

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Loaded);
    EXPECT_EQ(load.config.workspace_mode, "developer");
    EXPECT_EQ(load.config.active_panel, "settings");
    EXPECT_EQ(load.config.view_mode, "map2d");
    EXPECT_TRUE(load.config.follow_selected);
    EXPECT_EQ(load.config.map_orientation, "track_up");
    EXPECT_FALSE(load.config.overlay_enabled);
    EXPECT_FLOAT_EQ(load.config.overlay_opacity, 0.25F);
    EXPECT_FALSE(load.config.mavlink_inspector_visible);
    ASSERT_EQ(load.config.overlays.size(), 1U);
    EXPECT_EQ(load.config.overlays.front().path.string(), "overlay.tif");
    EXPECT_EQ(load.config.telemetry_live_udp_host, "0.0.0.0");
    EXPECT_EQ(load.config.telemetry_live_udp_port, 14560U);
    EXPECT_TRUE(load.config.plots.paused);
    EXPECT_EQ(load.config.plots.plots.front().title, "Round Trip Plot");
    ASSERT_TRUE(load.config.workspace_layouts.contains("analyze"));
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("analyze").main_panel.x, 200.0F);
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("analyze").main_panel.width, 520.0F);
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("analyze").plot_shelf_height_px, 320.0F);

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, ReportsUnknownKeys)
{
    const std::filesystem::path path = temp_path("animus_app_config_unknown.yaml");
    {
        std::ofstream output(path);
        output << "version: 1\napp:\n  workspace: advanced\n  mystery: true\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Loaded);
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "unknown config key: app.mystery"));

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, ReportsInvalidValuesAndKeepsDefaults)
{
    const std::filesystem::path path = temp_path("animus_app_config_invalid.yaml");
    {
        std::ofstream output(path);
        output << "version: 1\nwindow:\n  width: -1\nlayers:\n  overlay_opacity: 2\n"
               << "telemetry:\n  live_udp_port: 70000\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Loaded);
    EXPECT_EQ(load.config.window_width, 1280);
    EXPECT_FLOAT_EQ(load.config.overlay_opacity, 0.65F);
    EXPECT_EQ(load.config.telemetry_live_udp_port, 14550U);
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "width"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "overlay_opacity"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "live_udp_port"));

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, ReportsBadYaml)
{
    const std::filesystem::path path = temp_path("animus_app_config_bad.yaml");
    {
        std::ofstream output(path);
        output << "version: [\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Error);
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "bad YAML"));

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, MissingConfigIsDiagnosticNotFatal)
{
    const auto load = animus::app::load_app_config_file(temp_path("animus_missing_config.yaml"));

    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Missing);
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "config file not found"));
}

TEST(AnimusAppConfig, AtomicSaveWritesTargetAndRemovesTemp)
{
    const std::filesystem::path path = temp_path("animus_app_config_atomic.yaml");
    std::filesystem::remove(path);
    std::filesystem::remove(path.parent_path() / (path.filename().string() + ".tmp"));

    const auto save = animus::app::save_app_config_file(path, animus::app::AppConfig{});

    EXPECT_TRUE(save.saved);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path.parent_path() / (path.filename().string() + ".tmp")));

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, LoadsLegacyJsonLikeConfig)
{
    const std::filesystem::path path = temp_path("animus_app_config_legacy.json");
    {
        std::ofstream output(path);
        output << "{\n"
               << "  \"workspace_mode\": \"advanced\",\n"
               << "  \"view_mode\": \"map2d\",\n"
               << "  \"overlay_opacity\": 0.4\n"
               << "}\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::LoadedLegacy);
    EXPECT_EQ(load.config.workspace_mode, "analyze");
    EXPECT_EQ(load.config.view_mode, "map2d");
    EXPECT_FLOAT_EQ(load.config.overlay_opacity, 0.4F);

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, CanonicalizesLegacyWorkspaceAliasesFromYaml)
{
    const std::filesystem::path path = temp_path("animus_app_config_legacy_workspace.yaml");
    {
        std::ofstream output(path);
        output << "version: 1\napp:\n  workspace: operator\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Loaded);
    EXPECT_EQ(load.config.workspace_mode, "fly_test");
    EXPECT_FALSE(has_diagnostic(load.diagnostics, "app.workspace"));

    std::filesystem::remove(path);
}

TEST(AnimusAppConfig, WorkspaceLayoutDefaultsAndInvalidGeometryDiagnostics)
{
    const std::filesystem::path path = temp_path("animus_app_config_workspace_layout.yaml");
    {
        std::ofstream output(path);
        output << "version: 1\nworkspaces:\n"
               << "  terrain:\n"
               << "    active_panel: layers\n"
               << "    view_mode: terrain3d\n"
               << "    plot_shelf_visible: false\n"
               << "    main_panel:\n"
               << "      x: 40\n"
               << "      y: 50\n"
               << "      width: -1\n"
               << "      height: 420\n"
               << "  mystery:\n"
               << "    active_panel: view\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Loaded);
    ASSERT_TRUE(load.config.workspace_layouts.contains("terrain"));
    EXPECT_EQ(load.config.workspace_layouts.at("terrain").active_panel, "layers");
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("terrain").main_panel.width, 440.0F);
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "width"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "unknown workspace layout key: mystery"));

    const auto plan = animus::app::default_workspace_layout("plan");
    EXPECT_EQ(plan.active_panel, "view");
    EXPECT_EQ(plan.view_mode, "map2d");
    EXPECT_FALSE(plan.plot_shelf_visible);
    EXPECT_FALSE(plan.inspector_visible);

    std::filesystem::remove(path);
}
