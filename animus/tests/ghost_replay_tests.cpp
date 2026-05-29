#include "ghost_replay.hpp"

#include <gtest/gtest.h>

namespace
{

animus::telemetry_core::EntityId entity_id()
{
    return {1U, 1U};
}

animus::telemetry_core::TelemetrySample sample(double time_s, double lat_deg, double lon_deg)
{
    animus::telemetry_core::TelemetrySample value;
    value.time_s = time_s;
    value.entity_id = entity_id();
    value.lat_deg = lat_deg;
    value.lon_deg = lon_deg;
    value.ground_speed_mps = 10.0 + time_s;
    value.fields.position = true;
    value.fields.velocity = true;
    return value;
}

animus::telemetry_core::Timeline timeline(double lon_delta)
{
    animus::telemetry_core::Timeline value;
    value.samples = {sample(0.0, 39.0, -120.0), sample(1.0, 39.0, -120.0 + lon_delta)};
    value.tracks.push_back({entity_id(), value.samples});
    value.entities.push_back({entity_id(), value.samples.back()});
    value.start_time_s = 0.0;
    value.end_time_s = 1.0;
    return value;
}

} // namespace

TEST(GhostReplay, SummarizesAndComparesCurrentToBaseline)
{
    const auto current = timeline(0.002);
    const auto baseline = timeline(0.001);

    const auto comparison = animus::app::compare_ghost_replay(current, baseline, entity_id());

    EXPECT_TRUE(comparison.baseline_loaded);
    EXPECT_TRUE(comparison.current.available);
    EXPECT_TRUE(comparison.baseline.available);
    EXPECT_GT(comparison.current.distance_m, comparison.baseline.distance_m);
    EXPECT_GT(comparison.distance_delta_m, 0.0);
    ASSERT_TRUE(comparison.current.max_speed_mps);
    EXPECT_DOUBLE_EQ(*comparison.current.max_speed_mps, 11.0);
}

TEST(GhostReplay, MissingSelectedTrackIsDiagnostic)
{
    animus::telemetry_core::Timeline current = timeline(0.001);
    animus::telemetry_core::Timeline baseline;

    const auto comparison = animus::app::compare_ghost_replay(current, baseline, entity_id());

    EXPECT_FALSE(comparison.baseline.available);
    EXPECT_NE(comparison.diagnostic.find("baseline telemetry"), std::string::npos);
}
