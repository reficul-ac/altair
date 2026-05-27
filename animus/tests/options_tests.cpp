#include "options.hpp"

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
    EXPECT_THROW(parse({"animus", "--telemetry", "flight.tlog", "--telemetry-live-udp", "127.0.0.1:14550"}),
                 std::invalid_argument);
}
