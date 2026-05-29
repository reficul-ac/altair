#pragma once

#include "animus/telemetry_core/telemetry.hpp"
#include "plan_visualization.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace animus::app
{

enum class TimelineReviewSeverity
{
    Info,
    Caution,
    Warning,
};

enum class TimelineReviewMarkerCategory
{
    Gap,
    Degraded,
    ImportWarning,
    ImportError,
    Bookmark,
    MinClearance,
    MaxSpeed,
    LowClearance,
    Attitude,
    FrameTime,
    PlanDeviation,
    PlanAltitude,
    LowLinkHz,
    TerrainFallback,
    SpeedExcursion,
    ClimbExcursion,
    ModelFallback,
    Capture,
    Geofence,
};

struct TimelineReviewMarker
{
    TimelineReviewMarkerCategory category = TimelineReviewMarkerCategory::Gap;
    TimelineReviewSeverity severity = TimelineReviewSeverity::Info;
    double time_s = 0.0;
    std::optional<double> end_time_s;
    animus::telemetry_core::EntityId entity_id;
    std::string label;
    std::string note;
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
    std::string note;
    TimelineReviewSeverity severity = TimelineReviewSeverity::Info;
    TimelineReviewMarkerCategory category = TimelineReviewMarkerCategory::Bookmark;
};

struct TerrainClearanceSample
{
    double time_s = 0.0;
    double clearance_m = 0.0;
    bool planned_path = false;
    bool fallback = false;
    bool synthetic = false;
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

struct TimelineReviewFilterState
{
    bool show_info = true;
    bool show_caution = true;
    bool show_warning = true;
    bool show_gap = true;
    bool show_degraded = true;
    bool show_import = true;
    bool show_bookmark = true;
    bool show_clearance = true;
    bool show_attitude = true;
    bool show_frame_time = true;
    bool show_plan = true;
    bool show_min_max = true;
};

struct TimelineReviewThresholds
{
    double terrain_clearance_warning_m = 30.0;
    double terrain_clearance_critical_m = 10.0;
    double roll_warning_deg = 45.0;
    double pitch_warning_deg = 30.0;
    double frame_time_warning_ms = 33.0;
    double telemetry_gap_warning_s = 2.0;
    double telemetry_gap_critical_s = 5.0;
    double plan_deviation_warning_m = 50.0;
    double plan_altitude_error_warning_m = 25.0;
    double link_hz_warning = 2.0;
    double speed_warning_mps = 40.0;
    double climb_warning_mps = 8.0;
};

struct TimelineFrameTimeReviewState
{
    bool slow_segment_active = false;
};

constexpr std::size_t max_timeline_bookmarks = 64U;
constexpr std::size_t default_timeline_review_sample_cap = 512U;

[[nodiscard]] std::vector<TimelineReviewPoint>
decimate_review_points(std::vector<TimelineReviewPoint> points, std::size_t max_points);

void add_timeline_bookmark(std::vector<TimelineBookmark> &bookmarks, double time_s);
void add_timeline_bookmark(std::vector<TimelineBookmark> &bookmarks, TimelineBookmark bookmark);

void observe_timeline_frame_time(TimelineFrameTimeReviewState &state,
                                 std::vector<TimelineReviewMarker> &markers,
                                 double time_s,
                                 double frame_time_ms,
                                 const TimelineReviewThresholds &thresholds);

[[nodiscard]] TimelineReviewData
build_timeline_review(const animus::telemetry_core::Timeline &timeline,
                      animus::telemetry_core::EntityId selected_entity,
                      const std::vector<TimelineBookmark> &bookmarks,
                      const std::vector<TerrainClearanceSample> &terrain_clearance_samples,
                      const std::vector<TimelineReviewMarker> &frame_time_markers,
                      const TimelineReviewThresholds &thresholds,
                      const PlanVisualizationData *plan = nullptr,
                      std::size_t max_series_points = default_timeline_review_sample_cap);

[[nodiscard]] bool timeline_review_marker_visible(const TimelineReviewMarker &marker,
                                                  const TimelineReviewFilterState &filters);

[[nodiscard]] std::optional<std::size_t>
previous_review_marker(const std::vector<TimelineReviewMarker> &markers,
                       double current_time_s,
                       const TimelineReviewFilterState &filters = {});

[[nodiscard]] std::optional<std::size_t>
next_review_marker(const std::vector<TimelineReviewMarker> &markers,
                   double current_time_s,
                   const TimelineReviewFilterState &filters = {});

[[nodiscard]] const char *timeline_review_marker_label(TimelineReviewMarkerCategory category);
[[nodiscard]] const char *timeline_review_severity_label(TimelineReviewSeverity severity);

} // namespace animus::app
