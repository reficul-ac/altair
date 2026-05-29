#include "timeline_review.hpp"

#include <algorithm>
#include <iterator>
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

animus::app::TimelineReviewThresholds thresholds()
{
    animus::app::TimelineReviewThresholds value;
    value.telemetry_gap_warning_s = 2.0;
    value.telemetry_gap_critical_s = 5.0;
    value.terrain_clearance_warning_m = 30.0;
    value.terrain_clearance_critical_m = 10.0;
    value.roll_warning_deg = 45.0;
    value.pitch_warning_deg = 30.0;
    value.frame_time_warning_ms = 33.0;
    return value;
}

animus::app::TimelineReviewData
build_review(const animus::telemetry_core::Timeline &timeline,
             const std::vector<animus::app::TimelineBookmark> &bookmarks = {},
             const std::vector<animus::app::TerrainClearanceSample> &clearance = {},
             const std::vector<animus::app::TimelineReviewMarker> &frame_time_markers = {},
             const animus::app::TimelineReviewThresholds &status_thresholds = thresholds(),
             const animus::app::PlanVisualizationData *plan = nullptr)
{
    return animus::app::build_timeline_review(
        timeline, entity_id(), bookmarks, clearance, frame_time_markers, status_thresholds, plan);
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

animus::app::PlanVisualizationData plan_for_review()
{
    animus::app::PlanVisualizationData plan;
    plan.mission_waypoints.push_back({{39.0, -120.0, 100.0}, "1"});
    plan.mission_waypoints.push_back({{39.001, -120.0, 110.0}, "2"});
    plan.route_distance_m = animus::app::plan_route_distance_m(plan.mission_waypoints);
    return plan;
}

} // namespace

TEST(TimelineReviewTests, DetectsSelectedTrackGapsFromMedianThreshold)
{
    const auto timeline =
        timeline_with_samples({sample(0.0), sample(1.0), sample(2.0), sample(8.5)});

    const auto review = build_review(timeline);

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
    EXPECT_EQ(gap->severity, animus::app::TimelineReviewSeverity::Warning);
}

TEST(TimelineReviewTests, CreatesDegradedMarkersForMissingPosition)
{
    auto degraded = sample(1.0);
    degraded.fields.position = false;
    const auto timeline = timeline_with_samples({sample(0.0), degraded, sample(2.0)});

    const auto review = build_review(timeline);

    const auto marker = std::find_if(
        review.markers.begin(),
        review.markers.end(),
        [](const animus::app::TimelineReviewMarker &candidate)
        { return candidate.category == animus::app::TimelineReviewMarkerCategory::Degraded; });
    ASSERT_NE(marker, review.markers.end());
    EXPECT_DOUBLE_EQ(marker->time_s, 1.0);
}

TEST(TimelineReviewTests, UsesConfigDrivenTelemetryGapWarningAndCriticalSeverity)
{
    const auto timeline =
        timeline_with_samples({sample(0.0), sample(1.0), sample(3.5), sample(9.0)});
    auto status_thresholds = thresholds();
    status_thresholds.telemetry_gap_warning_s = 2.0;
    status_thresholds.telemetry_gap_critical_s = 5.0;

    const auto review = build_review(timeline, {}, {}, {}, status_thresholds);

    std::vector<animus::app::TimelineReviewMarker> gaps;
    std::copy_if(review.markers.begin(),
                 review.markers.end(),
                 std::back_inserter(gaps),
                 [](const animus::app::TimelineReviewMarker &marker)
                 { return marker.category == animus::app::TimelineReviewMarkerCategory::Gap; });
    ASSERT_EQ(gaps.size(), 2U);
    EXPECT_DOUBLE_EQ(gaps[0].time_s, 1.0);
    EXPECT_EQ(gaps[0].severity, animus::app::TimelineReviewSeverity::Caution);
    EXPECT_DOUBLE_EQ(gaps[1].time_s, 3.5);
    EXPECT_EQ(gaps[1].severity, animus::app::TimelineReviewSeverity::Warning);
}

TEST(TimelineReviewTests, CreatesLowClearanceSegmentsWithWarningPromotion)
{
    const auto timeline = timeline_with_samples({sample(0.0)});
    const auto review = build_review(
        timeline, {}, {{0.0, 40.0}, {1.0, 28.0}, {2.0, 8.0}, {3.0, 35.0}, {4.0, 25.0}});

    std::vector<animus::app::TimelineReviewMarker> low_clearance;
    std::copy_if(
        review.markers.begin(),
        review.markers.end(),
        std::back_inserter(low_clearance),
        [](const animus::app::TimelineReviewMarker &marker)
        { return marker.category == animus::app::TimelineReviewMarkerCategory::LowClearance; });
    ASSERT_EQ(low_clearance.size(), 2U);
    EXPECT_DOUBLE_EQ(low_clearance[0].time_s, 1.0);
    ASSERT_TRUE(low_clearance[0].end_time_s);
    EXPECT_DOUBLE_EQ(*low_clearance[0].end_time_s, 2.0);
    EXPECT_EQ(low_clearance[0].severity, animus::app::TimelineReviewSeverity::Warning);
    EXPECT_DOUBLE_EQ(low_clearance[1].time_s, 4.0);
    EXPECT_FALSE(low_clearance[1].end_time_s);
    EXPECT_EQ(low_clearance[1].severity, animus::app::TimelineReviewSeverity::Caution);
}

TEST(TimelineReviewTests, LabelsPlannedPathLowClearanceMarkers)
{
    const auto timeline = timeline_with_samples({sample(0.0)});
    const auto review = build_review(timeline, {}, {{0.0, 8.0, true}});

    const auto marker = std::find_if(
        review.markers.begin(),
        review.markers.end(),
        [](const animus::app::TimelineReviewMarker &candidate)
        { return candidate.category == animus::app::TimelineReviewMarkerCategory::LowClearance; });

    ASSERT_NE(marker, review.markers.end());
    EXPECT_EQ(marker->label, "low planned clearance");
}

TEST(TimelineReviewTests, CreatesHighRollAndPitchMarkersWithDegreeThresholds)
{
    auto roll = sample(1.0);
    roll.roll_rad = 45.0 * 3.14159265358979323846 / 180.0;
    auto pitch = sample(2.0);
    pitch.pitch_rad = 46.0 * 3.14159265358979323846 / 180.0;
    const auto timeline = timeline_with_samples({sample(0.0), roll, pitch});
    auto status_thresholds = thresholds();
    status_thresholds.roll_warning_deg = 30.0;
    status_thresholds.pitch_warning_deg = 30.0;

    const auto review = build_review(timeline, {}, {}, {}, status_thresholds);

    std::vector<animus::app::TimelineReviewMarker> attitude;
    std::copy_if(review.markers.begin(),
                 review.markers.end(),
                 std::back_inserter(attitude),
                 [](const animus::app::TimelineReviewMarker &marker) {
                     return marker.category == animus::app::TimelineReviewMarkerCategory::Attitude;
                 });
    ASSERT_EQ(attitude.size(), 2U);
    EXPECT_DOUBLE_EQ(attitude[0].time_s, 1.0);
    EXPECT_EQ(attitude[0].severity, animus::app::TimelineReviewSeverity::Warning);
    EXPECT_DOUBLE_EQ(attitude[1].time_s, 2.0);
    EXPECT_EQ(attitude[1].severity, animus::app::TimelineReviewSeverity::Warning);
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

    const auto review = build_review(timeline, {}, {{0.0, 80.0}, {1.0, 42.0}, {2.0, 55.0}});

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

TEST(TimelineReviewTests, BookmarkNoteSeverityAndCategoryArePreserved)
{
    std::vector<animus::app::TimelineBookmark> bookmarks;
    animus::app::TimelineBookmark bookmark;
    bookmark.time_s = 4.0;
    bookmark.label = "pilot note";
    bookmark.note = "check pass";
    bookmark.severity = animus::app::TimelineReviewSeverity::Warning;
    bookmark.category = animus::app::TimelineReviewMarkerCategory::Attitude;
    animus::app::add_timeline_bookmark(bookmarks, bookmark);
    const auto review = build_review(timeline_with_samples({sample(0.0)}), bookmarks);

    const auto marker = std::find_if(review.markers.begin(),
                                     review.markers.end(),
                                     [](const animus::app::TimelineReviewMarker &candidate)
                                     { return candidate.label == "pilot note"; });
    ASSERT_NE(marker, review.markers.end());
    EXPECT_EQ(marker->note, "check pass");
    EXPECT_EQ(marker->severity, animus::app::TimelineReviewSeverity::Warning);
    EXPECT_EQ(marker->category, animus::app::TimelineReviewMarkerCategory::Attitude);
}

TEST(TimelineReviewTests, DebouncesFrameTimeMarkers)
{
    std::vector<animus::app::TimelineReviewMarker> markers;
    animus::app::TimelineFrameTimeReviewState state;
    const auto status_thresholds = thresholds();

    observe_timeline_frame_time(state, markers, 1.0, 40.0, status_thresholds);
    observe_timeline_frame_time(state, markers, 2.0, 45.0, status_thresholds);
    observe_timeline_frame_time(state, markers, 3.0, 20.0, status_thresholds);
    observe_timeline_frame_time(state, markers, 4.0, 50.0, status_thresholds);

    ASSERT_EQ(markers.size(), 2U);
    EXPECT_DOUBLE_EQ(markers[0].time_s, 1.0);
    ASSERT_TRUE(markers[0].end_time_s);
    EXPECT_DOUBLE_EQ(*markers[0].end_time_s, 2.0);
    ASSERT_TRUE(markers[0].value);
    EXPECT_DOUBLE_EQ(*markers[0].value, 45.0);
    EXPECT_DOUBLE_EQ(markers[1].time_s, 4.0);
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

TEST(TimelineReviewTests, PreviousAndNextMarkersRespectVisibleFilters)
{
    std::vector<animus::app::TimelineReviewMarker> markers;
    animus::app::TimelineReviewMarker info;
    info.category = animus::app::TimelineReviewMarkerCategory::Bookmark;
    info.severity = animus::app::TimelineReviewSeverity::Info;
    info.time_s = 1.0;
    markers.push_back(info);
    animus::app::TimelineReviewMarker warning;
    warning.category = animus::app::TimelineReviewMarkerCategory::Gap;
    warning.severity = animus::app::TimelineReviewSeverity::Warning;
    warning.time_s = 3.0;
    markers.push_back(warning);
    animus::app::TimelineReviewMarker caution;
    caution.category = animus::app::TimelineReviewMarkerCategory::Attitude;
    caution.severity = animus::app::TimelineReviewSeverity::Caution;
    caution.time_s = 5.0;
    markers.push_back(caution);
    animus::app::TimelineReviewFilterState filters;
    filters.show_info = false;
    filters.show_caution = false;

    const auto previous = previous_review_marker(markers, 4.0, filters);
    const auto next = next_review_marker(markers, 4.0, filters);

    ASSERT_TRUE(previous);
    EXPECT_EQ(*previous, 1U);
    EXPECT_FALSE(next);
}

TEST(TimelineReviewTests, IncludesParserWarningsAndErrors)
{
    auto timeline = timeline_with_samples({sample(0.0)});
    timeline.events.push_back(
        {0.25, entity_id(), 1U, animus::telemetry_core::EventSeverity::Warning, "warning"});
    timeline.events.push_back(
        {0.5, entity_id(), 2U, animus::telemetry_core::EventSeverity::Error, "error"});

    const auto review = build_review(timeline);

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

TEST(TimelineReviewTests, CreatesPlanDeviationAndAltitudeMarkersAboveThreshold)
{
    auto off_route = sample(1.0);
    off_route.lon_deg = -119.998;
    off_route.altitude_relative_m = 160.0;
    const auto timeline = timeline_with_samples({sample(0.0), off_route, sample(2.0)});
    auto status_thresholds = thresholds();
    status_thresholds.plan_deviation_warning_m = 50.0;
    status_thresholds.plan_altitude_error_warning_m = 20.0;
    const auto plan = plan_for_review();

    const auto review = build_review(timeline, {}, {}, {}, status_thresholds, &plan);

    EXPECT_NE(std::find_if(review.markers.begin(),
                           review.markers.end(),
                           [](const animus::app::TimelineReviewMarker &marker) {
                               return marker.category ==
                                      animus::app::TimelineReviewMarkerCategory::PlanDeviation;
                           }),
              review.markers.end());
    EXPECT_NE(std::find_if(review.markers.begin(),
                           review.markers.end(),
                           [](const animus::app::TimelineReviewMarker &marker) {
                               return marker.category ==
                                      animus::app::TimelineReviewMarkerCategory::PlanAltitude;
                           }),
              review.markers.end());
}

TEST(TimelineReviewTests, OmitsPlanMarkersWhenGeometryUnavailable)
{
    const auto timeline = timeline_with_samples({sample(0.0), sample(1.0)});
    auto status_thresholds = thresholds();
    status_thresholds.plan_deviation_warning_m = 1.0;
    animus::app::PlanVisualizationData plan;
    plan.mission_waypoints.push_back({{39.0, -120.0, std::nullopt}, "1"});

    const auto review = build_review(timeline, {}, {}, {}, status_thresholds, &plan);

    EXPECT_EQ(std::find_if(review.markers.begin(),
                           review.markers.end(),
                           [](const animus::app::TimelineReviewMarker &marker) {
                               return marker.category ==
                                      animus::app::TimelineReviewMarkerCategory::PlanDeviation;
                           }),
              review.markers.end());
}

TEST(TimelineReviewTests, PlanMarkersRespectLabelsAndFilters)
{
    animus::app::TimelineReviewMarker deviation;
    deviation.category = animus::app::TimelineReviewMarkerCategory::PlanDeviation;
    deviation.severity = animus::app::TimelineReviewSeverity::Caution;

    EXPECT_STREQ(animus::app::timeline_review_marker_label(
                     animus::app::TimelineReviewMarkerCategory::PlanDeviation),
                 "plan deviation");
    EXPECT_TRUE(animus::app::timeline_review_marker_visible(deviation, {}));

    animus::app::TimelineReviewFilterState filters;
    filters.show_plan = false;
    EXPECT_FALSE(animus::app::timeline_review_marker_visible(deviation, filters));
}
