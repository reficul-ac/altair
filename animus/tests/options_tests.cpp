#include "options.hpp"
#include "layer_offline.hpp"

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

TEST(AnimusOptions, LoadsAndSavesWorkspaceMode)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_workspace_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\napp:\n  workspace: advanced\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Advanced);

    options.workspace_mode = animus::app::WorkspaceMode::Developer;
    const auto save = animus::app::save_app_config(options);
    EXPECT_TRUE(save.saved);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(restored.workspace_mode, animus::app::WorkspaceMode::Developer);

    std::filesystem::remove(path);
}

TEST(AnimusOptions, LoadsAndSavesViewMode)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_view_mode_test.yaml";
    {
        std::ofstream output(path);
        output << "version: 1\nview:\n  mode: map2d\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(options.view_mode, animus::app::ViewMode::Map2D);

    options.view_mode = animus::app::ViewMode::Terrain3D;
    const auto save = animus::app::save_app_config(options);
    EXPECT_TRUE(save.saved);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(restored.view_mode, animus::app::ViewMode::Terrain3D);

    std::filesystem::remove(path);
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
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Operator);
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
