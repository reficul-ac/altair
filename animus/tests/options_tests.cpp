#include "options.hpp"

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

TEST(AnimusOptions, RejectsInvalidViewMode)
{
    EXPECT_THROW(parse({"animus", "--view-mode", "browser"}), std::invalid_argument);
    EXPECT_THROW(parse({"animus", "--view-mode", "oblique25d"}), std::invalid_argument);
}

TEST(AnimusOptions, LoadsAndSavesWorkspaceMode)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_workspace_test.json";
    {
        std::ofstream output(path);
        output << "{\n"
               << "  \"schema\": \"animus.app_config.v1\",\n"
               << "  \"workspace_mode\": \"advanced\"\n"
               << "}\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(options.workspace_mode, animus::app::WorkspaceMode::Advanced);

    options.workspace_mode = animus::app::WorkspaceMode::Developer;
    animus::app::save_app_config(options);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(restored.workspace_mode, animus::app::WorkspaceMode::Developer);

    std::filesystem::remove(path);
}

TEST(AnimusOptions, LoadsAndSavesViewMode)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "animus_options_view_mode_test.json";
    {
        std::ofstream output(path);
        output << "{\n"
               << "  \"schema\": \"animus.app_config.v1\",\n"
               << "  \"view_mode\": \"map2d\"\n"
               << "}\n";
    }

    auto options = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(options.view_mode, animus::app::ViewMode::Map2D);

    options.view_mode = animus::app::ViewMode::Terrain3D;
    animus::app::save_app_config(options);

    const auto restored = parse({"animus", "--config", path.string().c_str()});
    EXPECT_EQ(restored.view_mode, animus::app::ViewMode::Terrain3D);

    std::filesystem::remove(path);
}
