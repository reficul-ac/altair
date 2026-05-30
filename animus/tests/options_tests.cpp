#include "options.hpp"
#include "fpv_camera.hpp"
#include "layer_offline.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace
{

animus::app::Options parse(std::initializer_list<const char *> args)
{
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (const char *arg : args)
    {
        argv.push_back(const_cast<char *>(arg));
    }
    return animus::app::parse_options(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST(AnimusOptions, ParsesLiveUdpEndpointAndBounds)
{
    const auto options = parse({"animus",
                                "--telemetry-live-udp",
                                "127.0.0.1:14551",
                                "--telemetry-live-buffer-s",
                                "12.5",
                                "--telemetry-live-max-samples",
                                "42",
                                "--telemetry-live-render-max-points",
                                "123",
                                "--telemetry-live-debug-csv",
                                "live_debug.csv"});

    EXPECT_TRUE(options.telemetry_live_udp_enabled);
    EXPECT_EQ(options.telemetry_live_udp_host, "127.0.0.1");
    EXPECT_EQ(options.telemetry_live_udp_port, 14551U);
    EXPECT_DOUBLE_EQ(options.telemetry_live_buffer_s, 12.5);
    EXPECT_EQ(options.telemetry_live_max_samples, 42U);
    EXPECT_EQ(options.telemetry_live_render_max_points, 123U);
    EXPECT_EQ(options.telemetry_live_debug_csv.string(), "live_debug.csv");
}

TEST(AnimusOptions, RejectsOfflineAndLiveTelemetryTogether)
{
    EXPECT_THROW(
        parse({"animus", "--telemetry", "flight.tlog", "--telemetry-live-udp", "127.0.0.1:14550"}),
        std::invalid_argument);
}

TEST(AnimusOptions, ParsesDeveloperWorkspace)
{
    const auto options = parse({"animus", "--debug-overlay", "--developer-workspace"});

    EXPECT_TRUE(options.developer_workspace);
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Developer);
}

TEST(AnimusOptions, ParsesViewMode)
{
    const auto options = parse({"animus", "--view-mode", "map2d"});

    EXPECT_EQ(options.view_mode, animus::app::ViewMode::Map2D);

    const auto fpv = parse({"animus", "--view-mode", "fpv"});
    EXPECT_EQ(fpv.view_mode, animus::app::ViewMode::Fpv);
}

TEST(AnimusOptions, ParsesPlanPath)
{
    const auto options = parse({"animus", "--plan", "mission.plan"});

    EXPECT_EQ(options.plan.string(), "mission.plan");
}

TEST(AnimusOptions, RejectsInvalidViewMode)
{
    EXPECT_THROW(parse({"animus", "--view-mode", "browser"}), std::invalid_argument);
    EXPECT_THROW(parse({"animus", "--view-mode", "oblique25d"}), std::invalid_argument);
}

TEST(AnimusOptions, AutoSaveOnExitOnlyForDirtyInteractiveConfig)
{
    animus::app::Options options;
    options.config_path = "animus.yaml";
    options.config_dirty = true;

    EXPECT_TRUE(animus::app::should_auto_save_config_on_exit(options));

    options.config_dirty = false;
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));

    options.config_dirty = true;
    options.smoke = true;
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));

    options.smoke = false;
    options.frames = 1;
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));

    options.frames = 0;
    options.capture_png = "capture.png";
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));

    options.capture_png.clear();
    options.capture_ppm = "capture.ppm";
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));

    options.capture_ppm.clear();
    options.capture_sequence_dir = "frames";
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));

    options.capture_sequence_dir.clear();
    options.config_path.clear();
    EXPECT_FALSE(animus::app::should_auto_save_config_on_exit(options));
}

TEST(AnimusOptions, LoadsAndSavesWorkspaceMode)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_workspace_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\napp:\n  workspace: advanced\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Analyze);

    options.workspace_mode = animus::app::WorkspaceMode::Developer;
    const auto save = animus::app::save_app_config(options);
    EXPECT_TRUE(save.saved);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(restored.workspace_mode, animus::app::WorkspaceMode::Developer);
    const auto load = animus::app::load_app_config_file(path);
    EXPECT_EQ(load.config.workspace_mode, "developer");

    std::filesystem::remove(path);
}

TEST(AnimusOptions, LoadsLegacyWorkspaceAliasesAndSavesCanonicalValues)
{
    const std::filesystem::path operator_path =
        std::filesystem::temp_directory_path() / "animus_options_operator_workspace_test.yaml";
    const std::filesystem::path advanced_path =
        std::filesystem::temp_directory_path() / "animus_options_advanced_workspace_test.yaml";
    {
        std::ofstream output(operator_path);
        output << "version: 1\napp:\n  workspace: operator\n";
    }
    {
        std::ofstream output(advanced_path);
        output << "version: 1\napp:\n  workspace: advanced\n";
    }

    auto operator_options = parse({"animus", "--config", operator_path.string().c_str()});
    auto advanced_options = parse({"animus", "--config", advanced_path.string().c_str()});

    EXPECT_EQ(operator_options.workspace_mode, animus::app::WorkspaceMode::FlyTest);
    EXPECT_EQ(advanced_options.workspace_mode, animus::app::WorkspaceMode::Analyze);
    EXPECT_TRUE(animus::app::save_app_config(operator_options).saved);
    EXPECT_TRUE(animus::app::save_app_config(advanced_options).saved);
    EXPECT_EQ(animus::app::load_app_config_file(operator_path).config.workspace_mode, "fly_test");
    EXPECT_EQ(animus::app::load_app_config_file(advanced_path).config.workspace_mode, "analyze");

    std::filesystem::remove(operator_path);
    std::filesystem::remove(advanced_path);
}

TEST(AnimusOptions, LoadsExportWorkspaceAlias)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_export_workspace_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\napp:\n  workspace: report\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});

    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Export);
    EXPECT_TRUE(animus::app::save_app_config(options).saved);
    EXPECT_EQ(animus::app::load_app_config_file(path).config.workspace_mode, "export");

    std::filesystem::remove(path);
}

TEST(AnimusOptions, LoadsAndSavesViewMode)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_view_mode_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\nview:\n  mode: fpv\n"
                  "  fpv_fov_deg: 88\n"
                  "  fpv_forward_offset_m: 1.2\n"
                  "  fpv_height_offset_m: 0.4\n"
                  "  fpv_smoothing_s: 0.6\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(options.view_mode, animus::app::ViewMode::Fpv);
    EXPECT_FLOAT_EQ(options.fpv.fov_deg, 88.0F);
    EXPECT_FLOAT_EQ(options.fpv.forward_offset_m, 1.2F);
    EXPECT_FLOAT_EQ(options.fpv.height_offset_m, 0.4F);
    EXPECT_FLOAT_EQ(options.fpv.smoothing_s, 0.6F);

    options.view_mode = animus::app::ViewMode::Terrain3D;
    options.fpv.fov_deg = 65.0F;
    options.fpv.forward_offset_m = 0.9F;
    options.fpv.height_offset_m = 0.1F;
    options.fpv.smoothing_s = 0.0F;
    const auto save = animus::app::save_app_config(options);
    EXPECT_TRUE(save.saved);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(restored.view_mode, animus::app::ViewMode::Terrain3D);
    EXPECT_FLOAT_EQ(restored.fpv.fov_deg, 65.0F);
    EXPECT_FLOAT_EQ(restored.fpv.forward_offset_m, 0.9F);
    EXPECT_FLOAT_EQ(restored.fpv.height_offset_m, 0.1F);
    EXPECT_FLOAT_EQ(restored.fpv.smoothing_s, 0.0F);

    std::filesystem::remove(path);
}

TEST(AnimusOptions, LoadsAndSavesWindowPlacement)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_window_placement_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\nwindow:\n"
                  "  x: 44\n"
                  "  y: 55\n"
                  "  width: 1366\n"
                  "  height: 768\n"
                  "  maximized: true\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    ASSERT_TRUE(options.window_x);
    ASSERT_TRUE(options.window_y);
    EXPECT_EQ(*options.window_x, 44);
    EXPECT_EQ(*options.window_y, 55);
    EXPECT_EQ(options.width, 1366);
    EXPECT_EQ(options.height, 768);
    EXPECT_TRUE(options.window_maximized);

    options.window_x = 88;
    options.window_y = 99;
    options.width = 1280;
    options.height = 720;
    options.window_maximized = false;
    const auto save = animus::app::save_app_config(options);
    EXPECT_TRUE(save.saved);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    ASSERT_TRUE(restored.window_x);
    ASSERT_TRUE(restored.window_y);
    EXPECT_EQ(*restored.window_x, 88);
    EXPECT_EQ(*restored.window_y, 99);
    EXPECT_EQ(restored.width, 1280);
    EXPECT_EQ(restored.height, 720);
    EXPECT_FALSE(restored.window_maximized);

    std::filesystem::remove(path);
}

TEST(AnimusOptions, CliWindowSizeOverridesSuppressSavedMaximizedState)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_window_cli_override_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\nwindow:\n"
                  "  x: 10\n"
                  "  y: 20\n"
                  "  width: 1600\n"
                  "  height: 900\n"
                  "  maximized: true\n";
    }

    const auto options = parse({"animus", "--config", path.string().c_str(), "--width", "1024"});
    ASSERT_TRUE(options.window_x);
    ASSERT_TRUE(options.window_y);
    EXPECT_EQ(*options.window_x, 10);
    EXPECT_EQ(*options.window_y, 20);
    EXPECT_EQ(options.width, 1024);
    EXPECT_EQ(options.height, 900);
    EXPECT_FALSE(options.window_maximized);
    EXPECT_TRUE(options.window_size_overridden);

    std::filesystem::remove(path);
}

TEST(AnimusFpvCamera, BuildsPoseFromHeadingAndYawFallback)
{
    animus::telemetry_core::TelemetrySample sample;
    sample.fields.position = true;
    sample.lat_deg = 0.0;
    sample.lon_deg = 0.0;
    sample.altitude_msl_m = 100.0;
    sample.heading_deg = 90.0;

    const auto pose = animus::app::build_fpv_pose(sample, {}, {12, 2048, 2048, 0.01F});
    ASSERT_TRUE(pose);
    EXPECT_GT(pose->forward.x, 0.99F);
    EXPECT_NEAR(pose->forward.z, 0.0F, 0.01F);

    sample.heading_deg.reset();
    sample.yaw_rad = 3.14159265358979323846 / 2.0;
    const auto yaw_pose = animus::app::build_fpv_pose(sample, {}, {12, 2048, 2048, 0.01F});
    ASSERT_TRUE(yaw_pose);
    EXPECT_GT(yaw_pose->forward.x, 0.99F);
}

TEST(AnimusFpvCamera, DefaultsMissingPitchRollAndRollTiltsUpVector)
{
    animus::telemetry_core::TelemetrySample sample;
    sample.fields.position = true;
    sample.lat_deg = 0.0;
    sample.lon_deg = 0.0;
    sample.yaw_rad = 0.0;

    const auto level = animus::app::build_fpv_pose(sample, {}, {12, 2048, 2048, 0.01F});
    ASSERT_TRUE(level);
    EXPECT_NEAR(level->up.x, 0.0F, 0.001F);
    EXPECT_GT(level->up.y, 0.99F);

    sample.roll_rad = 0.5;
    const auto rolled = animus::app::build_fpv_pose(sample, {}, {12, 2048, 2048, 0.01F});
    ASSERT_TRUE(rolled);
    EXPECT_GT(std::abs(rolled->up.x), 0.001F);
}

TEST(AnimusFpvCamera, ClampsOffsetsAndSmoothingHolds)
{
    animus::app::FpvSettings settings;
    settings.fov_deg = 5.0F;
    settings.forward_offset_m = 9.0F;
    settings.height_offset_m = -9.0F;
    settings.smoothing_s = 9.0F;
    const auto clamped = animus::app::clamped_fpv_settings(settings);
    EXPECT_FLOAT_EQ(clamped.fov_deg, 35.0F);
    EXPECT_FLOAT_EQ(clamped.forward_offset_m, 3.0F);
    EXPECT_FLOAT_EQ(clamped.height_offset_m, -1.0F);
    EXPECT_FLOAT_EQ(clamped.smoothing_s, 2.0F);

    animus::telemetry_core::TelemetrySample sample;
    sample.fields.position = true;
    sample.lat_deg = 0.0;
    sample.lon_deg = 0.0;
    sample.heading_deg = 0.0;

    animus::app::FpvCameraState state;
    animus::app::update_fpv_camera(state, sample, {}, {12, 2048, 2048, 0.01F}, 0.0);
    EXPECT_EQ(state.status, "live");
    const float first_z = state.pose->eye.z;
    animus::app::update_fpv_camera(state, std::nullopt, {}, {12, 2048, 2048, 0.01F}, 1.0);
    EXPECT_EQ(state.status, "held");
    EXPECT_FLOAT_EQ(state.pose->eye.z, first_z);

    sample.lon_deg = 0.01;
    settings = {};
    settings.smoothing_s = 1.0F;
    animus::app::update_fpv_camera(state, sample, settings, {12, 2048, 2048, 0.01F}, 0.1);
    EXPECT_EQ(state.status, "live");
    EXPECT_GT(state.pose->eye.x, 0.0F);
}

TEST(AnimusFpvCamera, UsesResolvedWorldHeightForRuntimePose)
{
    animus::telemetry_core::TelemetrySample sample;
    sample.fields.position = true;
    sample.lat_deg = 39.0;
    sample.lon_deg = -120.0;
    sample.altitude_msl_m = 100.0;
    sample.altitude_relative_m = 20.0;
    sample.heading_deg = 0.0;

    animus::app::FpvSettings settings;
    settings.forward_offset_m = 0.0F;
    settings.height_offset_m = 0.0F;
    const animus::app::Vec3 resolved_world{1.0F, 3.5F, 2.0F};
    const auto pose =
        animus::app::build_fpv_pose_from_world(sample, resolved_world, settings, {12, 0, 0, 0.01F});

    ASSERT_TRUE(pose);
    EXPECT_FLOAT_EQ(pose->eye.x, resolved_world.x);
    EXPECT_FLOAT_EQ(pose->eye.y, resolved_world.y);
    EXPECT_FLOAT_EQ(pose->eye.z, resolved_world.z);
}

TEST(AnimusOptions, LoadsAndSavesStatusThresholds)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_status_thresholds_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\nstatus_thresholds:\n  frame_time_warning_ms: 41\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_DOUBLE_EQ(options.status_thresholds.frame_time_warning_ms, 41.0);

    options.status_thresholds.frame_time_warning_ms = 25.0;
    const auto save = animus::app::save_app_config(options);
    EXPECT_TRUE(save.saved);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_DOUBLE_EQ(restored.status_thresholds.frame_time_warning_ms, 25.0);

    std::filesystem::remove(path);
}

TEST(AnimusOptions, ResolvesDefaultConfigPathFromXdg)
{
    setenv("XDG_CONFIG_HOME", "/tmp/animus-xdg", 1);

    const auto options = parse({"animus", "--no-load-config"});

    EXPECT_EQ(options.config_path.string(), "/tmp/animus-xdg/animus/animus.yaml");
    EXPECT_FALSE(options.load_config);
    EXPECT_EQ(options.config_load_status, "skipped");
}

TEST(AnimusOptions, NoLoadConfigSkipsExistingFileButKeepsSavePath)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_no_load.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\napp:\n  workspace: developer\n";
    }

    const auto options = parse({"animus", "--config", path.string().c_str(), "--no-load-config"});

    EXPECT_EQ(options.config_path, path);
    EXPECT_FALSE(options.load_config);
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::FlyTest);
    EXPECT_EQ(options.config_load_status, "skipped");

    std::filesystem::remove(path);
}

TEST(AnimusOptions, DuplicateConfigUsesLastPath)
{
    const std::filesystem::path first =
        std::filesystem::temp_directory_path() / "animus_options_first.yaml";
    const std::filesystem::path second =
        std::filesystem::temp_directory_path() / "animus_options_second.yaml";
    {
        std::ofstream output(first);
        output << "version: 1\napp:\n  workspace: advanced\n";
    }
    {
        std::ofstream output(second);
        output << "version: 1\napp:\n  workspace: developer\n";
    }

    const auto options =
        parse({"animus", "--config", first.string().c_str(), "--config", second.string().c_str()});

    EXPECT_EQ(options.config_path, second);
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Developer);

    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST(AnimusOptions, CliOverridesLoadedConfig)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_cli_precedence.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\nview:\n  mode: map2d\ntelemetry:\n  live_udp_port: 14560\n";
    }

    const auto options = parse({"animus",
                                "--config",
                                path.string().c_str(),
                                "--view-mode",
                                "terrain3d",
                                "--telemetry-live-udp",
                                "127.0.0.1:14570"});

    EXPECT_EQ(options.view_mode, animus::app::ViewMode::Terrain3D);
    EXPECT_TRUE(options.telemetry_live_udp_enabled);
    EXPECT_EQ(options.telemetry_live_udp_port, 14570U);

    std::filesystem::remove(path);
}

TEST(AnimusOfflinePreview, BuildsCurrentViewPrewarmCommand)
{
    animus::app::Options options;
    options.min_z = 2;
    options.max_z = 3;
    options.z = 2;
    options.center_x = 1;
    options.center_y = 1;
    options.cache_root = "cache root";
    options.imagery_mbtiles = "imagery.mbtiles";
    options.remote_imagery_url_template = "https://tiles.example/{z}/{x}/{y}.png";
    options.remote_imagery_user_agent = "Animus Test";

    const std::vector<animus::terrain_core::TileRenderDecision> visible_tiles = {
        {animus::geo_core::TileCoord{2, 1, 1}, false},
        {animus::geo_core::TileCoord{2, 2, 1}, false},
    };

    const auto preview = animus::app::build_prewarm_preview(options, "pack root", visible_tiles);

    EXPECT_EQ(preview.tile_count, 29U);
    EXPECT_NE(preview.command.find("python3 animus/tools/prewarm_cache.py --bbox"),
              std::string::npos);
    EXPECT_NE(preview.command.find("--min-z 2 --max-z 3"), std::string::npos);
    EXPECT_NE(preview.command.find("--pack-root 'pack root'"), std::string::npos);
    EXPECT_NE(preview.command.find("--cache-root 'cache root'"), std::string::npos);
    EXPECT_NE(preview.command.find("--mbtiles imagery.mbtiles"), std::string::npos);
    EXPECT_NE(preview.command.find("--remote-url https://tiles.example/{z}/{x}/{y}.png"),
              std::string::npos);
    EXPECT_NE(preview.command.find("--remote-user-agent 'Animus Test'"), std::string::npos);
}

TEST(AnimusOfflinePreview, OmitsAbsentOptionalSources)
{
    animus::app::Options options;
    options.min_z = 4;
    options.max_z = 4;
    options.z = 4;
    options.center_x = 3;
    options.center_y = 5;

    const auto preview = animus::app::build_prewarm_preview(options, options.pack_root, {});

    EXPECT_EQ(preview.layers, "imagery,elevation");
    EXPECT_EQ(preview.tile_count, 4U);
    EXPECT_EQ(preview.command.find("--mbtiles"), std::string::npos);
    EXPECT_EQ(preview.command.find("--remote-url"), std::string::npos);
    EXPECT_EQ(preview.command.find("--layers"), std::string::npos);
}
