#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace animus::app
{

enum class TimelineReviewMarkerCategory
{
    Gap,
    Degraded,
    ImportWarning,
    ImportError,
    Bookmark,
    MinClearance,
    MaxSpeed,
};

struct TimelineReviewMarker
{
    TimelineReviewMarkerCategory category = TimelineReviewMarkerCategory::Gap;
    double time_s = 0.0;
    std::optional<double> end_time_s;
    animus::telemetry_core::EntityId entity_id;
    std::string label;
    std::optional<double> value;
};

struct TimelineReviewPoint
{
    double time_s = 0.0;
    double value = 0.0;
};

struct TimelineReviewSeries
{
    std::string label;
    std::string unit;
    std::vector<TimelineReviewPoint> points;
};

struct TimelineBookmark
{
    double time_s = 0.0;
    std::string label;
};

struct TerrainClearanceSample
{
    double time_s = 0.0;
    double clearance_m = 0.0;
};

struct TimelineReviewData
{
    std::vector<TimelineReviewMarker> markers;
    TimelineReviewSeries altitude;
    TimelineReviewSeries ground_speed;
    TimelineReviewSeries terrain_clearance;
    std::optional<TimelineReviewMarker> min_clearance_marker;
    std::optional<TimelineReviewMarker> max_speed_marker;
};

constexpr std::size_t max_timeline_bookmarks = 64U;
constexpr std::size_t default_timeline_review_sample_cap = 512U;

[[nodiscard]] std::vector<TimelineReviewPoint>
decimate_review_points(std::vector<TimelineReviewPoint> points, std::size_t max_points);

void add_timeline_bookmark(std::vector<TimelineBookmark> &bookmarks, double time_s);

[[nodiscard]] TimelineReviewData
build_timeline_review(const animus::telemetry_core::Timeline &timeline,
                      animus::telemetry_core::EntityId selected_entity,
                      const std::vector<TimelineBookmark> &bookmarks,
                      const std::vector<TerrainClearanceSample> &terrain_clearance_samples,
                      std::size_t max_series_points = default_timeline_review_sample_cap);

[[nodiscard]] std::optional<std::size_t>
previous_review_marker(const std::vector<TimelineReviewMarker> &markers, double current_time_s);

[[nodiscard]] std::optional<std::size_t>
next_review_marker(const std::vector<TimelineReviewMarker> &markers, double current_time_s);

[[nodiscard]] const char *timeline_review_marker_label(TimelineReviewMarkerCategory category);

} // namespace animus::app
