#include "timeline_review.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>

namespace animus::app
{
namespace
{

bool has_required_position(const animus::telemetry_core::TelemetrySample &sample)
{
    return sample.fields.position;
}

std::optional<double> review_altitude_m(const animus::telemetry_core::TelemetrySample &sample)
{
    if (sample.altitude_relative_m)
    {
        return sample.altitude_relative_m;
    }
    return sample.altitude_msl_m;
}

double rad_to_deg(const double radians)
{
    constexpr double pi = 3.14159265358979323846;
    return radians * 180.0 / pi;
}

TimelineReviewSeverity warning_or_caution(const double value, const double critical_value)
{
    return value >= critical_value ? TimelineReviewSeverity::Warning
                                   : TimelineReviewSeverity::Caution;
}

TimelineReviewSeverity clearance_severity(const double clearance_m,
                                          const TimelineReviewThresholds &thresholds)
{
    return clearance_m <= thresholds.terrain_clearance_critical_m ? TimelineReviewSeverity::Warning
                                                                  : TimelineReviewSeverity::Caution;
}

TimelineReviewSeverity import_severity(const animus::telemetry_core::EventSeverity severity)
{
    return severity == animus::telemetry_core::EventSeverity::Error
               ? TimelineReviewSeverity::Warning
               : TimelineReviewSeverity::Caution;
}

std::optional<TimelineReviewMarker>
attitude_marker(const animus::telemetry_core::TelemetrySample &sample,
                const animus::telemetry_core::EntityId selected_entity,
                const char *axis,
                const std::optional<double> value_rad,
                const double threshold_deg)
{
    if (!value_rad || threshold_deg <= 0.0)
    {
        return std::nullopt;
    }
    const double value_deg = std::abs(rad_to_deg(*value_rad));
    if (value_deg < threshold_deg)
    {
        return std::nullopt;
    }
    TimelineReviewMarker marker;
    marker.category = TimelineReviewMarkerCategory::Attitude;
    marker.severity = warning_or_caution(value_deg, threshold_deg * 1.5);
    marker.time_s = sample.time_s;
    marker.entity_id = selected_entity;
    marker.label = std::string("high ") + axis;
    marker.value = value_deg;
    return marker;
}

std::string bookmark_label(const std::size_t count)
{
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "B%zu", count);
    return buffer;
}

void sort_markers(std::vector<TimelineReviewMarker> &markers)
{
    std::stable_sort(markers.begin(),
                     markers.end(),
                     [](const TimelineReviewMarker &lhs, const TimelineReviewMarker &rhs)
                     {
                         if (lhs.time_s == rhs.time_s)
                         {
                             return std::string_view(timeline_review_marker_label(lhs.category)) <
                                    std::string_view(timeline_review_marker_label(rhs.category));
                         }
                         return lhs.time_s < rhs.time_s;
                     });
}

void append_low_clearance_markers(std::vector<TimelineReviewMarker> &markers,
                                  const std::vector<TerrainClearanceSample> &samples,
                                  const animus::telemetry_core::EntityId selected_entity,
                                  const TimelineReviewThresholds &thresholds)
{
    std::optional<TimelineReviewMarker> active;
    for (const TerrainClearanceSample &sample : samples)
    {
        if (sample.clearance_m <= thresholds.terrain_clearance_warning_m)
        {
            if (!active)
            {
                TimelineReviewMarker marker;
                marker.category = TimelineReviewMarkerCategory::LowClearance;
                marker.severity = clearance_severity(sample.clearance_m, thresholds);
                marker.time_s = sample.time_s;
                marker.entity_id = selected_entity;
                marker.label = "low clearance";
                marker.value = sample.clearance_m;
                active = marker;
            }
            else
            {
                active->end_time_s = sample.time_s;
                if (sample.clearance_m < *active->value)
                {
                    active->value = sample.clearance_m;
                }
                if (clearance_severity(sample.clearance_m, thresholds) ==
                    TimelineReviewSeverity::Warning)
                {
                    active->severity = TimelineReviewSeverity::Warning;
                }
            }
        }
        else if (active)
        {
            markers.push_back(*active);
            active.reset();
        }
    }
    if (active)
    {
        markers.push_back(*active);
    }
}

} // namespace

std::vector<TimelineReviewPoint> decimate_review_points(std::vector<TimelineReviewPoint> points,
                                                        const std::size_t max_points)
{
    if (max_points == 0U)
    {
        points.clear();
        return points;
    }
    if (points.size() <= max_points)
    {
        return points;
    }

    std::vector<TimelineReviewPoint> decimated;
    decimated.reserve(max_points);
    const std::size_t input_last = points.size() - 1U;
    const std::size_t output_last = max_points - 1U;
    for (std::size_t index = 0U; index < max_points; ++index)
    {
        const std::size_t source =
            output_last == 0U ? 0U : (index * input_last + output_last / 2U) / output_last;
        decimated.push_back(points[source]);
    }
    return decimated;
}

void add_timeline_bookmark(std::vector<TimelineBookmark> &bookmarks, const double time_s)
{
    TimelineBookmark bookmark;
    bookmark.time_s = time_s;
    bookmark.label = bookmark_label(bookmarks.size() + 1U);
    add_timeline_bookmark(bookmarks, std::move(bookmark));
}

void add_timeline_bookmark(std::vector<TimelineBookmark> &bookmarks, TimelineBookmark bookmark)
{
    if (bookmark.label.empty())
    {
        bookmark.label = bookmark_label(bookmarks.size() + 1U);
    }
    bookmarks.push_back(std::move(bookmark));
    std::stable_sort(bookmarks.begin(),
                     bookmarks.end(),
                     [](const TimelineBookmark &lhs, const TimelineBookmark &rhs)
                     { return lhs.time_s < rhs.time_s; });
    if (bookmarks.size() > max_timeline_bookmarks)
    {
        bookmarks.erase(bookmarks.begin());
    }
}

void observe_timeline_frame_time(TimelineFrameTimeReviewState &state,
                                 std::vector<TimelineReviewMarker> &markers,
                                 const double time_s,
                                 const double frame_time_ms,
                                 const TimelineReviewThresholds &thresholds)
{
    if (frame_time_ms < thresholds.frame_time_warning_ms)
    {
        state.slow_segment_active = false;
        return;
    }

    if (state.slow_segment_active && !markers.empty() &&
        markers.back().category == TimelineReviewMarkerCategory::FrameTime)
    {
        markers.back().end_time_s = time_s;
        if (!markers.back().value || frame_time_ms > *markers.back().value)
        {
            markers.back().value = frame_time_ms;
        }
        return;
    }

    TimelineReviewMarker marker;
    marker.category = TimelineReviewMarkerCategory::FrameTime;
    marker.severity = TimelineReviewSeverity::Caution;
    marker.time_s = time_s;
    marker.entity_id = {};
    marker.label = "slow frame";
    marker.value = frame_time_ms;
    markers.push_back(marker);
    state.slow_segment_active = true;
}

TimelineReviewData
build_timeline_review(const animus::telemetry_core::Timeline &timeline,
                      const animus::telemetry_core::EntityId selected_entity,
                      const std::vector<TimelineBookmark> &bookmarks,
                      const std::vector<TerrainClearanceSample> &terrain_clearance_samples,
                      const std::vector<TimelineReviewMarker> &frame_time_markers,
                      const TimelineReviewThresholds &thresholds,
                      const std::size_t max_series_points)
{
    TimelineReviewData review;
    review.altitude.label = "Altitude";
    review.altitude.unit = "m";
    review.ground_speed.label = "Ground speed";
    review.ground_speed.unit = "m/s";
    review.terrain_clearance.label = "Terrain clearance";
    review.terrain_clearance.unit = "m";

    const auto *track = timeline.track_for(selected_entity);
    if (track != nullptr)
    {
        std::optional<TimelineReviewMarker> max_speed;
        for (std::size_t index = 0U; index < track->samples.size(); ++index)
        {
            const auto &sample = track->samples[index];
            if (const auto altitude = review_altitude_m(sample))
            {
                review.altitude.points.push_back({sample.time_s, *altitude});
            }
            if (sample.ground_speed_mps)
            {
                review.ground_speed.points.push_back({sample.time_s, *sample.ground_speed_mps});
                if (!max_speed || *sample.ground_speed_mps > *max_speed->value)
                {
                    TimelineReviewMarker marker;
                    marker.category = TimelineReviewMarkerCategory::MaxSpeed;
                    marker.severity = TimelineReviewSeverity::Info;
                    marker.time_s = sample.time_s;
                    marker.entity_id = selected_entity;
                    marker.label = "max speed";
                    marker.value = *sample.ground_speed_mps;
                    max_speed = marker;
                }
            }
            if (!has_required_position(sample))
            {
                TimelineReviewMarker marker;
                marker.category = TimelineReviewMarkerCategory::Degraded;
                marker.severity = TimelineReviewSeverity::Caution;
                marker.time_s = sample.time_s;
                marker.entity_id = selected_entity;
                marker.label = "degraded";
                review.markers.push_back(marker);
            }
            if (const auto marker = attitude_marker(
                    sample, selected_entity, "roll", sample.roll_rad, thresholds.roll_warning_deg))
            {
                review.markers.push_back(*marker);
            }
            if (const auto marker = attitude_marker(sample,
                                                    selected_entity,
                                                    "pitch",
                                                    sample.pitch_rad,
                                                    thresholds.pitch_warning_deg))
            {
                review.markers.push_back(*marker);
            }
            if (index > 0U)
            {
                const double dt = sample.time_s - track->samples[index - 1U].time_s;
                if (dt >= thresholds.telemetry_gap_warning_s)
                {
                    TimelineReviewMarker marker;
                    marker.category = TimelineReviewMarkerCategory::Gap;
                    marker.severity = warning_or_caution(dt, thresholds.telemetry_gap_critical_s);
                    marker.time_s = track->samples[index - 1U].time_s;
                    marker.end_time_s = sample.time_s;
                    marker.entity_id = selected_entity;
                    marker.label = "telemetry gap";
                    marker.value = dt;
                    review.markers.push_back(marker);
                }
            }
        }
        if (max_speed)
        {
            review.max_speed_marker = max_speed;
            review.markers.push_back(*max_speed);
        }
    }

    for (const auto &clearance : terrain_clearance_samples)
    {
        review.terrain_clearance.points.push_back({clearance.time_s, clearance.clearance_m});
        if (!review.min_clearance_marker ||
            clearance.clearance_m < *review.min_clearance_marker->value)
        {
            TimelineReviewMarker marker;
            marker.category = TimelineReviewMarkerCategory::MinClearance;
            marker.severity = TimelineReviewSeverity::Info;
            marker.time_s = clearance.time_s;
            marker.entity_id = selected_entity;
            marker.label = "min clearance";
            marker.value = clearance.clearance_m;
            review.min_clearance_marker = marker;
        }
    }
    if (review.min_clearance_marker)
    {
        review.markers.push_back(*review.min_clearance_marker);
    }
    append_low_clearance_markers(
        review.markers, terrain_clearance_samples, selected_entity, thresholds);

    for (const auto &event : timeline.events)
    {
        if (event.severity == animus::telemetry_core::EventSeverity::Info)
        {
            continue;
        }
        TimelineReviewMarker marker;
        marker.category = event.severity == animus::telemetry_core::EventSeverity::Error
                              ? TimelineReviewMarkerCategory::ImportError
                              : TimelineReviewMarkerCategory::ImportWarning;
        marker.severity = import_severity(event.severity);
        marker.time_s = event.time_s;
        marker.entity_id = event.entity_id;
        marker.label =
            event.message.empty() ? timeline_review_marker_label(marker.category) : event.message;
        review.markers.push_back(marker);
    }

    for (const auto &bookmark : bookmarks)
    {
        TimelineReviewMarker marker;
        marker.category = bookmark.category;
        marker.severity = bookmark.severity;
        marker.time_s = bookmark.time_s;
        marker.entity_id = selected_entity;
        marker.label = bookmark.label;
        marker.note = bookmark.note;
        review.markers.push_back(marker);
    }
    for (const TimelineReviewMarker &marker : frame_time_markers)
    {
        review.markers.push_back(marker);
    }

    review.altitude.points =
        decimate_review_points(std::move(review.altitude.points), max_series_points);
    review.ground_speed.points =
        decimate_review_points(std::move(review.ground_speed.points), max_series_points);
    review.terrain_clearance.points =
        decimate_review_points(std::move(review.terrain_clearance.points), max_series_points);
    sort_markers(review.markers);
    return review;
}

bool timeline_review_marker_visible(const TimelineReviewMarker &marker,
                                    const TimelineReviewFilterState &filters)
{
    switch (marker.severity)
    {
    case TimelineReviewSeverity::Info:
        if (!filters.show_info)
        {
            return false;
        }
        break;
    case TimelineReviewSeverity::Caution:
        if (!filters.show_caution)
        {
            return false;
        }
        break;
    case TimelineReviewSeverity::Warning:
        if (!filters.show_warning)
        {
            return false;
        }
        break;
    }

    switch (marker.category)
    {
    case TimelineReviewMarkerCategory::Gap:
        return filters.show_gap;
    case TimelineReviewMarkerCategory::Degraded:
        return filters.show_degraded;
    case TimelineReviewMarkerCategory::ImportWarning:
    case TimelineReviewMarkerCategory::ImportError:
        return filters.show_import;
    case TimelineReviewMarkerCategory::Bookmark:
        return filters.show_bookmark;
    case TimelineReviewMarkerCategory::MinClearance:
    case TimelineReviewMarkerCategory::MaxSpeed:
        return filters.show_min_max;
    case TimelineReviewMarkerCategory::LowClearance:
        return filters.show_clearance;
    case TimelineReviewMarkerCategory::Attitude:
        return filters.show_attitude;
    case TimelineReviewMarkerCategory::FrameTime:
        return filters.show_frame_time;
    }
    return true;
}

std::optional<std::size_t> previous_review_marker(const std::vector<TimelineReviewMarker> &markers,
                                                  const double current_time_s,
                                                  const TimelineReviewFilterState &filters)
{
    constexpr double epsilon_s = 1.0e-6;
    for (std::size_t reverse_index = markers.size(); reverse_index > 0U; --reverse_index)
    {
        const std::size_t index = reverse_index - 1U;
        if (timeline_review_marker_visible(markers[index], filters) &&
            markers[index].time_s < current_time_s - epsilon_s)
        {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> next_review_marker(const std::vector<TimelineReviewMarker> &markers,
                                              const double current_time_s,
                                              const TimelineReviewFilterState &filters)
{
    constexpr double epsilon_s = 1.0e-6;
    for (std::size_t index = 0U; index < markers.size(); ++index)
    {
        if (timeline_review_marker_visible(markers[index], filters) &&
            markers[index].time_s > current_time_s + epsilon_s)
        {
            return index;
        }
    }
    return std::nullopt;
}

const char *timeline_review_marker_label(const TimelineReviewMarkerCategory category)
{
    switch (category)
    {
    case TimelineReviewMarkerCategory::Gap:
        return "gap";
    case TimelineReviewMarkerCategory::Degraded:
        return "degraded";
    case TimelineReviewMarkerCategory::ImportWarning:
        return "warning";
    case TimelineReviewMarkerCategory::ImportError:
        return "error";
    case TimelineReviewMarkerCategory::Bookmark:
        return "bookmark";
    case TimelineReviewMarkerCategory::MinClearance:
        return "min clearance";
    case TimelineReviewMarkerCategory::MaxSpeed:
        return "max speed";
    case TimelineReviewMarkerCategory::LowClearance:
        return "low clearance";
    case TimelineReviewMarkerCategory::Attitude:
        return "attitude";
    case TimelineReviewMarkerCategory::FrameTime:
        return "frame time";
    }
    return "marker";
}

const char *timeline_review_severity_label(const TimelineReviewSeverity severity)
{
    switch (severity)
    {
    case TimelineReviewSeverity::Info:
        return "info";
    case TimelineReviewSeverity::Caution:
        return "caution";
    case TimelineReviewSeverity::Warning:
        return "warning";
    }
    return "info";
}

} // namespace animus::app
