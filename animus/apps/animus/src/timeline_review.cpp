#include "timeline_review.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

double selected_track_gap_threshold_s(const animus::telemetry_core::Track &track)
{
    std::vector<double> deltas;
    deltas.reserve(track.samples.size());
    for (std::size_t index = 1U; index < track.samples.size(); ++index)
    {
        const double dt = track.samples[index].time_s - track.samples[index - 1U].time_s;
        if (dt > 0.0 && std::isfinite(dt))
        {
            deltas.push_back(dt);
        }
    }
    if (deltas.empty())
    {
        return 2.0;
    }
    std::sort(deltas.begin(), deltas.end());
    const double median =
        deltas.size() % 2U == 1U
            ? deltas[deltas.size() / 2U]
            : (deltas[deltas.size() / 2U - 1U] + deltas[deltas.size() / 2U]) * 0.5;
    return std::max(2.0, median * 3.0);
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
    bookmarks.push_back(bookmark);
    std::stable_sort(bookmarks.begin(),
                     bookmarks.end(),
                     [](const TimelineBookmark &lhs, const TimelineBookmark &rhs)
                     { return lhs.time_s < rhs.time_s; });
    if (bookmarks.size() > max_timeline_bookmarks)
    {
        bookmarks.erase(bookmarks.begin());
    }
}

TimelineReviewData
build_timeline_review(const animus::telemetry_core::Timeline &timeline,
                      const animus::telemetry_core::EntityId selected_entity,
                      const std::vector<TimelineBookmark> &bookmarks,
                      const std::vector<TerrainClearanceSample> &terrain_clearance_samples,
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
        const double gap_threshold_s = selected_track_gap_threshold_s(*track);
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
                marker.time_s = sample.time_s;
                marker.entity_id = selected_entity;
                marker.label = "degraded";
                review.markers.push_back(marker);
            }
            if (index > 0U)
            {
                const double dt = sample.time_s - track->samples[index - 1U].time_s;
                if (dt > gap_threshold_s)
                {
                    TimelineReviewMarker marker;
                    marker.category = TimelineReviewMarkerCategory::Gap;
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
        marker.time_s = event.time_s;
        marker.entity_id = event.entity_id;
        marker.label =
            event.message.empty() ? timeline_review_marker_label(marker.category) : event.message;
        review.markers.push_back(marker);
    }

    for (const auto &bookmark : bookmarks)
    {
        TimelineReviewMarker marker;
        marker.category = TimelineReviewMarkerCategory::Bookmark;
        marker.time_s = bookmark.time_s;
        marker.entity_id = selected_entity;
        marker.label = bookmark.label;
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

std::optional<std::size_t> previous_review_marker(const std::vector<TimelineReviewMarker> &markers,
                                                  const double current_time_s)
{
    constexpr double epsilon_s = 1.0e-6;
    for (std::size_t reverse_index = markers.size(); reverse_index > 0U; --reverse_index)
    {
        const std::size_t index = reverse_index - 1U;
        if (markers[index].time_s < current_time_s - epsilon_s)
        {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> next_review_marker(const std::vector<TimelineReviewMarker> &markers,
                                              const double current_time_s)
{
    constexpr double epsilon_s = 1.0e-6;
    for (std::size_t index = 0U; index < markers.size(); ++index)
    {
        if (markers[index].time_s > current_time_s + epsilon_s)
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
    }
    return "marker";
}

} // namespace animus::app
