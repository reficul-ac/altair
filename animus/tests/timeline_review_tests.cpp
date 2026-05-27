#include "timeline_review.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace
{

animus::telemetry_core::EntityId entity_id()
{
    return {1U, 42U};
}

animus::telemetry_core::TelemetrySample sample(const double time_s)
{
    animus::telemetry_core::TelemetrySample value;
    value.time_s = time_s;
    value.entity_id = entity_id();
    value.lat_deg = 39.0;
    value.lon_deg = -120.0;
    value.altitude_relative_m = 100.0 + time_s;
    value.ground_speed_mps = 12.0 + time_s;
    value.fields.position = true;
    value.fields.altitude_relative = true;
    value.fields.velocity = true;
    return value;
}

animus::telemetry_core::Timeline
timeline_with_samples(const std::vector<animus::telemetry_core::TelemetrySample> &samples)
{
    animus::telemetry_core::Timeline timeline;
    timeline.samples = samples;
    timeline.tracks.push_back({entity_id(), samples});
    timeline.entities.push_back({entity_id(),
                                 samples.empty()
                                     ? std::optional<animus::telemetry_core::TelemetrySample>{}
                                     : samples.back()});
    if (!samples.empty())
    {
        timeline.start_time_s = samples.front().time_s;
        timeline.end_time_s = samples.back().time_s;
    }
    return timeline;
}

} // namespace

TEST(TimelineReviewTests, DetectsSelectedTrackGapsFromMedianThreshold)
{
    const auto timeline =
        timeline_with_samples({sample(0.0), sample(1.0), sample(2.0), sample(8.5)});

    const auto review = animus::app::build_timeline_review(timeline, entity_id(), {}, {});

    const auto gap =
        std::find_if(review.markers.begin(),
                     review.markers.end(),
                     [](const animus::app::TimelineReviewMarker &marker)
                     { return marker.category == animus::app::TimelineReviewMarkerCategory::Gap; });
    ASSERT_NE(gap, review.markers.end());
    EXPECT_DOUBLE_EQ(gap->time_s, 2.0);
    ASSERT_TRUE(gap->end_time_s);
    EXPECT_DOUBLE_EQ(*gap->end_time_s, 8.5);
    ASSERT_TRUE(gap->value);
    EXPECT_DOUBLE_EQ(*gap->value, 6.5);
}

TEST(TimelineReviewTests, CreatesDegradedMarkersForMissingPosition)
{
    auto degraded = sample(1.0);
    degraded.fields.position = false;
    const auto timeline = timeline_with_samples({sample(0.0), degraded, sample(2.0)});

    const auto review = animus::app::build_timeline_review(timeline, entity_id(), {}, {});

    const auto marker = std::find_if(
        review.markers.begin(),
        review.markers.end(),
        [](const animus::app::TimelineReviewMarker &candidate)
        { return candidate.category == animus::app::TimelineReviewMarkerCategory::Degraded; });
    ASSERT_NE(marker, review.markers.end());
    EXPECT_DOUBLE_EQ(marker->time_s, 1.0);
}

TEST(TimelineReviewTests, SelectsMaxSpeedAndMinClearanceMarkers)
{
    auto first = sample(0.0);
    first.ground_speed_mps = 14.0;
    auto second = sample(1.0);
    second.ground_speed_mps = 32.0;
    auto third = sample(2.0);
    third.ground_speed_mps = 18.0;
    const auto timeline = timeline_with_samples({first, second, third});

    const auto review = animus::app::build_timeline_review(
        timeline, entity_id(), {}, {{0.0, 80.0}, {1.0, 42.0}, {2.0, 55.0}});

    ASSERT_TRUE(review.max_speed_marker);
    EXPECT_DOUBLE_EQ(review.max_speed_marker->time_s, 1.0);
    ASSERT_TRUE(review.max_speed_marker->value);
    EXPECT_DOUBLE_EQ(*review.max_speed_marker->value, 32.0);
    ASSERT_TRUE(review.min_clearance_marker);
    EXPECT_DOUBLE_EQ(review.min_clearance_marker->time_s, 1.0);
    ASSERT_TRUE(review.min_clearance_marker->value);
    EXPECT_DOUBLE_EQ(*review.min_clearance_marker->value, 42.0);
}

TEST(TimelineReviewTests, DecimatesSeriesDeterministically)
{
    std::vector<animus::app::TimelineReviewPoint> points;
    for (int index = 0; index < 10; ++index)
    {
        points.push_back({static_cast<double>(index), static_cast<double>(index * 2)});
    }

    const auto decimated = animus::app::decimate_review_points(points, 4U);

    ASSERT_EQ(decimated.size(), 4U);
    EXPECT_DOUBLE_EQ(decimated[0].time_s, 0.0);
    EXPECT_DOUBLE_EQ(decimated[1].time_s, 3.0);
    EXPECT_DOUBLE_EQ(decimated[2].time_s, 6.0);
    EXPECT_DOUBLE_EQ(decimated[3].time_s, 9.0);
}

TEST(TimelineReviewTests, BookmarkCapAndOrderingAreSessionLocal)
{
    std::vector<animus::app::TimelineBookmark> bookmarks;
    for (std::size_t index = 0U; index < animus::app::max_timeline_bookmarks + 2U; ++index)
    {
        animus::app::add_timeline_bookmark(bookmarks, 100.0 - static_cast<double>(index));
    }

    ASSERT_EQ(bookmarks.size(), animus::app::max_timeline_bookmarks);
    EXPECT_LT(bookmarks.front().time_s, bookmarks.back().time_s);
    EXPECT_GE(bookmarks.front().time_s, 37.0);
}

TEST(TimelineReviewTests, FindsPreviousAndNextMarkersAroundCurrentTime)
{
    std::vector<animus::app::TimelineReviewMarker> markers;
    animus::app::TimelineReviewMarker first;
    first.category = animus::app::TimelineReviewMarkerCategory::Gap;
    first.time_s = 1.0;
    markers.push_back(first);
    animus::app::TimelineReviewMarker second;
    second.category = animus::app::TimelineReviewMarkerCategory::Degraded;
    second.time_s = 3.0;
    markers.push_back(second);
    animus::app::TimelineReviewMarker third;
    third.category = animus::app::TimelineReviewMarkerCategory::MaxSpeed;
    third.time_s = 5.0;
    markers.push_back(third);

    const auto previous = animus::app::previous_review_marker(markers, 3.5);
    const auto next = animus::app::next_review_marker(markers, 3.5);

    ASSERT_TRUE(previous);
    EXPECT_EQ(*previous, 1U);
    ASSERT_TRUE(next);
    EXPECT_EQ(*next, 2U);
}

TEST(TimelineReviewTests, IncludesParserWarningsAndErrors)
{
    auto timeline = timeline_with_samples({sample(0.0)});
    timeline.events.push_back(
        {0.25, entity_id(), 1U, animus::telemetry_core::EventSeverity::Warning, "warning"});
    timeline.events.push_back(
        {0.5, entity_id(), 2U, animus::telemetry_core::EventSeverity::Error, "error"});

    const auto review = animus::app::build_timeline_review(timeline, entity_id(), {}, {});

    EXPECT_NE(std::find_if(review.markers.begin(),
                           review.markers.end(),
                           [](const animus::app::TimelineReviewMarker &marker) {
                               return marker.category ==
                                      animus::app::TimelineReviewMarkerCategory::ImportWarning;
                           }),
              review.markers.end());
    EXPECT_NE(std::find_if(review.markers.begin(),
                           review.markers.end(),
                           [](const animus::app::TimelineReviewMarker &marker) {
                               return marker.category ==
                                      animus::app::TimelineReviewMarkerCategory::ImportError;
                           }),
              review.markers.end());
}
