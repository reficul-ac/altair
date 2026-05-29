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
    EXPECT_TRUE(config.layers.vehicle_icons_visible);
    EXPECT_TRUE(config.layers.vehicle_labels_visible);
    EXPECT_TRUE(config.layers.track_tail_visible);
    EXPECT_TRUE(config.layers.heading_vectors_visible);
    EXPECT_TRUE(config.layers.planned_route_visible);
    EXPECT_TRUE(config.layers.geofence_rally_visible);
    EXPECT_TRUE(config.layers.terrain_confidence_visible);
    EXPECT_FALSE(config.layers.terrain_clearance_heatmap_visible);
    EXPECT_TRUE(config.layers.geotiff_overlay_visible);
    EXPECT_FLOAT_EQ(config.layers.geotiff_overlay_opacity, 0.65F);
    EXPECT_FALSE(config.layers.bathymetry_visible);
    EXPECT_TRUE(config.layers.hillshade_visible);
    EXPECT_FALSE(config.layers.tile_state_debug_visible);
    EXPECT_FALSE(config.layers.fallback_highlight_visible);
    EXPECT_EQ(config.selected_entity_tail_points, 1000U);
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
    config.layers.vehicle_icons_visible = false;
    config.layers.vehicle_labels_visible = false;
    config.layers.track_tail_visible = false;
    config.layers.heading_vectors_visible = false;
    config.layers.planned_route_visible = false;
    config.layers.geofence_rally_visible = false;
    config.layers.terrain_confidence_visible = false;
    config.layers.terrain_clearance_heatmap_visible = true;
    config.layers.geotiff_overlay_visible = false;
    config.layers.geotiff_overlay_opacity = 0.25F;
    config.layers.geotiff_overlay_draw_order = 3;
    config.layers.bathymetry_visible = true;
    config.layers.bathymetry_opacity = 0.4F;
    config.layers.hillshade_visible = false;
    config.layers.tile_state_debug_visible = true;
    config.layers.fallback_highlight_visible = true;
    config.mavlink_inspector_visible = false;
    config.telemetry_live_udp_host = "0.0.0.0";
    config.telemetry_live_udp_port = 14560U;
    config.overlays.push_back({"overlay.tif", true, 0.5F, 3, "geotiff:overlay.tif"});
    config.plots.paused = true;
    config.plots.plots.front().title = "Round Trip Plot";
    config.vehicle_visuals.defaults_by_type["rc_plane"] = "animus.rc_plane.generic";
    config.vehicle_visuals.entities["1:1"] = {
        "animus.rc_plane.generic", true, 1.5F, "none", "terrain_resolved"};
    config.workspace_layouts["analyze"] = animus::app::default_workspace_layout("analyze");
    config.workspace_layouts["analyze"].main_panel = {200.0F, 80.0F, 520.0F, 460.0F};
    config.workspace_layouts["analyze"].plot_shelf_height_px = 320.0F;
    config.selected_entity_tail_points = 250U;
    config.layers.selected_entity_tail_points = 250U;
    config.selected_vehicle_test.test_name = "FT-12";
    config.selected_vehicle_test.phase = "Cruise";
    config.selected_vehicle_test.target_speed = "24 m/s";
    config.selected_vehicle_test.target_altitude = "150 m";
    config.selected_vehicle_test.target_heading = "090";
    config.ghost_recent_baseline_path = "baseline.tlog";
    config.ghost_layer_visible = true;
    config.report_export_default_dir = "reports";

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
    EXPECT_FALSE(load.config.layers.vehicle_icons_visible);
    EXPECT_FALSE(load.config.layers.vehicle_labels_visible);
    EXPECT_FALSE(load.config.layers.track_tail_visible);
    EXPECT_FALSE(load.config.layers.heading_vectors_visible);
    EXPECT_FALSE(load.config.layers.planned_route_visible);
    EXPECT_FALSE(load.config.layers.geofence_rally_visible);
    EXPECT_FALSE(load.config.layers.terrain_confidence_visible);
    EXPECT_TRUE(load.config.layers.terrain_clearance_heatmap_visible);
    EXPECT_FALSE(load.config.layers.geotiff_overlay_visible);
    EXPECT_FLOAT_EQ(load.config.layers.geotiff_overlay_opacity, 0.25F);
    EXPECT_EQ(load.config.layers.geotiff_overlay_draw_order, 3);
    EXPECT_TRUE(load.config.layers.bathymetry_visible);
    EXPECT_FLOAT_EQ(load.config.layers.bathymetry_opacity, 0.4F);
    EXPECT_FALSE(load.config.layers.hillshade_visible);
    EXPECT_TRUE(load.config.layers.tile_state_debug_visible);
    EXPECT_TRUE(load.config.layers.fallback_highlight_visible);
    EXPECT_FALSE(load.config.mavlink_inspector_visible);
    ASSERT_EQ(load.config.overlays.size(), 1U);
    EXPECT_EQ(load.config.overlays.front().path.string(), "overlay.tif");
    EXPECT_EQ(load.config.telemetry_live_udp_host, "0.0.0.0");
    EXPECT_EQ(load.config.telemetry_live_udp_port, 14560U);
    EXPECT_TRUE(load.config.plots.paused);
    EXPECT_EQ(load.config.plots.plots.front().title, "Round Trip Plot");
    ASSERT_TRUE(load.config.vehicle_visuals.defaults_by_type.contains("rc_plane"));
    EXPECT_EQ(load.config.vehicle_visuals.defaults_by_type.at("rc_plane"),
              "animus.rc_plane.generic");
    ASSERT_TRUE(load.config.vehicle_visuals.entities.contains("1:1"));
    EXPECT_TRUE(load.config.vehicle_visuals.entities.at("1:1").force_icon_only);
    EXPECT_FLOAT_EQ(load.config.vehicle_visuals.entities.at("1:1").scale, 1.5F);
    EXPECT_EQ(load.config.vehicle_visuals.entities.at("1:1").heading_source, "none");
    ASSERT_TRUE(load.config.workspace_layouts.contains("analyze"));
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("analyze").main_panel.x, 200.0F);
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("analyze").main_panel.width, 520.0F);
    EXPECT_FLOAT_EQ(load.config.workspace_layouts.at("analyze").plot_shelf_height_px, 320.0F);
    EXPECT_EQ(load.config.selected_entity_tail_points, 250U);
    EXPECT_EQ(load.config.layers.selected_entity_tail_points, 250U);
    EXPECT_EQ(load.config.selected_vehicle_test.test_name, "FT-12");
    EXPECT_EQ(load.config.selected_vehicle_test.phase, "Cruise");
    EXPECT_EQ(load.config.selected_vehicle_test.target_speed, "24 m/s");
    EXPECT_EQ(load.config.selected_vehicle_test.target_altitude, "150 m");
    EXPECT_EQ(load.config.selected_vehicle_test.target_heading, "090");
    EXPECT_EQ(load.config.ghost_recent_baseline_path.string(), "baseline.tlog");
    EXPECT_TRUE(load.config.ghost_layer_visible);
    EXPECT_EQ(load.config.report_export_default_dir.string(), "reports");

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
               << "  geotiff_overlay_opacity: 9\n  bathymetry_opacity: -0.5\n"
               << "  mystery_layer: true\n"
               << "telemetry:\n  live_udp_port: 70000\n";
        output << "vehicle_visuals:\n"
               << "  unknown: true\n"
               << "  defaults_by_type: []\n"
               << "  entities:\n"
               << "    bad-key:\n"
               << "      vehicle_id: \"\"\n"
               << "    \"1:1\":\n"
               << "      vehicle_id: \"\"\n"
               << "      scale: -2\n"
               << "      heading_source: sideways\n"
               << "      altitude_placement: pressure\n";
    }

    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.status, animus::app::AppConfigLoadStatus::Loaded);
    EXPECT_EQ(load.config.window_width, 1280);
    EXPECT_FLOAT_EQ(load.config.overlay_opacity, 0.65F);
    EXPECT_EQ(load.config.telemetry_live_udp_port, 14550U);
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "width"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "overlay_opacity"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "geotiff_overlay_opacity"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "bathymetry_opacity"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "unknown config key: layers.mystery_layer"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "live_udp_port"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "unknown config key: vehicle_visuals.unknown"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "vehicle_visuals.defaults_by_type"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "bad-key"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "heading_source"));
    EXPECT_TRUE(has_diagnostic(load.diagnostics, "altitude_placement"));

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
