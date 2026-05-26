#include "animus/telemetry_core/telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace animus::telemetry_core
{
namespace
{

bool same_entity(const EntityId a, const EntityId b)
{
    return a.system_id == b.system_id && a.component_id == b.component_id;
}

std::optional<double>
lerp_optional(const std::optional<double> a, const std::optional<double> b, const double factor)
{
    if (!a && !b)
    {
        return std::nullopt;
    }
    if (!a)
    {
        return b;
    }
    if (!b)
    {
        return a;
    }
    return *a + (*b - *a) * factor;
}

TelemetrySample interpolate(const TelemetrySample &a, const TelemetrySample &b, const double time_s)
{
    const double span = b.time_s - a.time_s;
    const double factor = span > 0.0 ? std::clamp((time_s - a.time_s) / span, 0.0, 1.0) : 0.0;
    TelemetrySample sample = a;
    sample.time_s = time_s;
    sample.lat_deg = a.lat_deg + (b.lat_deg - a.lat_deg) * factor;
    sample.lon_deg = a.lon_deg + (b.lon_deg - a.lon_deg) * factor;
    sample.altitude_msl_m = lerp_optional(a.altitude_msl_m, b.altitude_msl_m, factor);
    sample.altitude_relative_m =
        lerp_optional(a.altitude_relative_m, b.altitude_relative_m, factor);
    sample.roll_rad = lerp_optional(a.roll_rad, b.roll_rad, factor);
    sample.pitch_rad = lerp_optional(a.pitch_rad, b.pitch_rad, factor);
    sample.yaw_rad = lerp_optional(a.yaw_rad, b.yaw_rad, factor);
    sample.ground_speed_mps = lerp_optional(a.ground_speed_mps, b.ground_speed_mps, factor);
    sample.climb_rate_mps = lerp_optional(a.climb_rate_mps, b.climb_rate_mps, factor);
    sample.heading_deg = lerp_optional(a.heading_deg, b.heading_deg, factor);
    sample.fields.position = a.fields.position || b.fields.position;
    sample.fields.altitude_msl = a.fields.altitude_msl || b.fields.altitude_msl;
    sample.fields.altitude_relative = a.fields.altitude_relative || b.fields.altitude_relative;
    sample.fields.attitude = a.fields.attitude || b.fields.attitude;
    sample.fields.velocity = a.fields.velocity || b.fields.velocity;
    sample.fields.heading = a.fields.heading || b.fields.heading;
    return sample;
}

} // namespace

const Track *Timeline::track_for(const EntityId id) const
{
    const auto it =
        std::find_if(tracks.begin(),
                     tracks.end(),
                     [id](const Track &track) { return same_entity(track.entity_id, id); });
    return it == tracks.end() ? nullptr : &*it;
}

std::optional<TelemetrySample> Timeline::sample_at(const EntityId id, const double time_s) const
{
    const Track *track = track_for(id);
    if (track == nullptr || track->samples.empty())
    {
        return std::nullopt;
    }
    const auto &track_samples = track->samples;
    if (time_s <= track_samples.front().time_s)
    {
        return track_samples.front();
    }
    if (time_s >= track_samples.back().time_s)
    {
        return track_samples.back();
    }
    const auto upper = std::upper_bound(track_samples.begin(),
                                        track_samples.end(),
                                        time_s,
                                        [](const double value, const TelemetrySample &sample)
                                        { return value < sample.time_s; });
    if (upper == track_samples.begin())
    {
        return *upper;
    }
    return interpolate(*(upper - 1), *upper, time_s);
}

void PlaybackClock::set_range(const double start_time_s, const double end_time_s)
{
    if (!std::isfinite(start_time_s) || !std::isfinite(end_time_s) || end_time_s < start_time_s)
    {
        throw std::invalid_argument("Playback range must be finite and ordered");
    }
    start_time_s_ = start_time_s;
    end_time_s_ = end_time_s;
    seek(start_time_s_);
}

void PlaybackClock::set_rate(const double rate)
{
    if (!std::isfinite(rate) || rate <= 0.0)
    {
        throw std::invalid_argument("Playback rate must be positive and finite");
    }
    rate_ = rate;
}

void PlaybackClock::set_paused(const bool paused)
{
    paused_ = paused;
}

void PlaybackClock::set_looping(const bool looping)
{
    looping_ = looping;
}

void PlaybackClock::seek(const double time_s)
{
    if (!std::isfinite(time_s))
    {
        throw std::invalid_argument("Playback seek time must be finite");
    }
    time_s_ = std::clamp(time_s, start_time_s_, end_time_s_);
}

void PlaybackClock::advance(const double delta_wall_s)
{
    if (paused_ || delta_wall_s <= 0.0 || !std::isfinite(delta_wall_s))
    {
        return;
    }
    time_s_ += delta_wall_s * rate_;
    if (time_s_ > end_time_s_)
    {
        if (looping_ && end_time_s_ > start_time_s_)
        {
            time_s_ =
                start_time_s_ + std::fmod(time_s_ - start_time_s_, end_time_s_ - start_time_s_);
        }
        else
        {
            time_s_ = end_time_s_;
            paused_ = true;
        }
    }
}

double PlaybackClock::time_s() const
{
    return time_s_;
}

double PlaybackClock::start_time_s() const
{
    return start_time_s_;
}

double PlaybackClock::end_time_s() const
{
    return end_time_s_;
}

double PlaybackClock::rate() const
{
    return rate_;
}

bool PlaybackClock::paused() const
{
    return paused_;
}

bool PlaybackClock::looping() const
{
    return looping_;
}

} // namespace animus::telemetry_core
