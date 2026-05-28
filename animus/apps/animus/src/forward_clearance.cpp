#include "forward_clearance.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace animus::app
{
namespace
{

constexpr double pi = 3.14159265358979323846;
constexpr double deg_to_rad = pi / 180.0;
constexpr double rad_to_deg = 180.0 / pi;
constexpr double earth_radius_m = 6371008.8;

int terrain_status_rank(const TelemetryPlaybackState::TerrainClearanceStatus status)
{
    switch (status)
    {
    case TelemetryPlaybackState::TerrainClearanceStatus::Warning:
        return 3;
    case TelemetryPlaybackState::TerrainClearanceStatus::Caution:
        return 2;
    case TelemetryPlaybackState::TerrainClearanceStatus::Unknown:
        return 1;
    case TelemetryPlaybackState::TerrainClearanceStatus::Ok:
        return 0;
    }
    return 1;
}

double normalize_degrees(double value)
{
    value = std::fmod(value, 360.0);
    if (value < 0.0)
    {
        value += 360.0;
    }
    return value;
}

std::pair<double, double> project_lat_lon(const double lat_deg,
                                          const double lon_deg,
                                          const double heading_deg,
                                          const double distance_m)
{
    const double angular_distance = distance_m / earth_radius_m;
    const double bearing = heading_deg * deg_to_rad;
    const double lat1 = lat_deg * deg_to_rad;
    const double lon1 = lon_deg * deg_to_rad;
    const double sin_lat1 = std::sin(lat1);
    const double cos_lat1 = std::cos(lat1);
    const double sin_distance = std::sin(angular_distance);
    const double cos_distance = std::cos(angular_distance);

    const double lat2 =
        std::asin(sin_lat1 * cos_distance + cos_lat1 * sin_distance * std::cos(bearing));
    const double lon2 = lon1 + std::atan2(std::sin(bearing) * sin_distance * cos_lat1,
                                          cos_distance - sin_lat1 * std::sin(lat2));
    return {lat2 * rad_to_deg, normalize_degrees(lon2 * rad_to_deg + 180.0) - 180.0};
}

} // namespace

const std::vector<double> &forward_clearance_horizons_s()
{
    static const std::vector<double> horizons{5.0, 10.0, 20.0, 30.0};
    return horizons;
}

const char *terrain_confidence_label(const TelemetryPlaybackState::TerrainConfidence confidence)
{
    switch (confidence)
    {
    case TelemetryPlaybackState::TerrainConfidence::ExactResidentTile:
        return "exact resident tile";
    case TelemetryPlaybackState::TerrainConfidence::FallbackResidentTile:
        return "fallback resident tile";
    case TelemetryPlaybackState::TerrainConfidence::SyntheticResidentTile:
        return "synthetic resident tile";
    case TelemetryPlaybackState::TerrainConfidence::Unavailable:
        return "unavailable";
    case TelemetryPlaybackState::TerrainConfidence::DatumUncertain:
        return "datum uncertain";
    }
    return "unavailable";
}

const char *
terrain_clearance_status_label(const TelemetryPlaybackState::TerrainClearanceStatus status)
{
    switch (status)
    {
    case TelemetryPlaybackState::TerrainClearanceStatus::Ok:
        return "OK";
    case TelemetryPlaybackState::TerrainClearanceStatus::Caution:
        return "Caution";
    case TelemetryPlaybackState::TerrainClearanceStatus::Warning:
        return "Warning";
    case TelemetryPlaybackState::TerrainClearanceStatus::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

TelemetryPlaybackState::TerrainClearanceStatus
terrain_confidence_status(const TelemetryPlaybackState::TerrainConfidence confidence)
{
    switch (confidence)
    {
    case TelemetryPlaybackState::TerrainConfidence::DatumUncertain:
        return TelemetryPlaybackState::TerrainClearanceStatus::Warning;
    case TelemetryPlaybackState::TerrainConfidence::FallbackResidentTile:
    case TelemetryPlaybackState::TerrainConfidence::SyntheticResidentTile:
        return TelemetryPlaybackState::TerrainClearanceStatus::Caution;
    case TelemetryPlaybackState::TerrainConfidence::ExactResidentTile:
        return TelemetryPlaybackState::TerrainClearanceStatus::Ok;
    case TelemetryPlaybackState::TerrainConfidence::Unavailable:
        return TelemetryPlaybackState::TerrainClearanceStatus::Unknown;
    }
    return TelemetryPlaybackState::TerrainClearanceStatus::Unknown;
}

TelemetryPlaybackState::TerrainClearanceStatus
terrain_clearance_status(const std::optional<double> clearance_m,
                         const TelemetryPlaybackState::TerrainConfidence confidence,
                         const AppConfigStatusThresholds &thresholds)
{
    if (!clearance_m)
    {
        return TelemetryPlaybackState::TerrainClearanceStatus::Unknown;
    }
    TelemetryPlaybackState::TerrainClearanceStatus status = terrain_confidence_status(confidence);
    if (*clearance_m <= thresholds.terrain_clearance_critical_m)
    {
        status = worst_terrain_clearance_status(
            status, TelemetryPlaybackState::TerrainClearanceStatus::Warning);
    }
    else if (*clearance_m <= thresholds.terrain_clearance_warning_m)
    {
        status = worst_terrain_clearance_status(
            status, TelemetryPlaybackState::TerrainClearanceStatus::Caution);
    }
    return status;
}

TelemetryPlaybackState::TerrainClearanceStatus
worst_terrain_clearance_status(const TelemetryPlaybackState::TerrainClearanceStatus lhs,
                               const TelemetryPlaybackState::TerrainClearanceStatus rhs)
{
    return terrain_status_rank(rhs) > terrain_status_rank(lhs) ? rhs : lhs;
}

TelemetryPlaybackState::TerrainClearanceStatus worst_forward_clearance_status(
    const std::vector<TelemetryPlaybackState::ForwardClearanceSample> &samples)
{
    if (samples.empty())
    {
        return TelemetryPlaybackState::TerrainClearanceStatus::Unknown;
    }
    TelemetryPlaybackState::TerrainClearanceStatus status =
        TelemetryPlaybackState::TerrainClearanceStatus::Ok;
    for (const auto &sample : samples)
    {
        status = worst_terrain_clearance_status(status, sample.status);
    }
    return status;
}

std::optional<double>
selected_sample_forward_heading_deg(const animus::telemetry_core::TelemetrySample &sample)
{
    if (sample.heading_deg)
    {
        return normalize_degrees(*sample.heading_deg);
    }
    if (sample.yaw_rad)
    {
        return normalize_degrees(*sample.yaw_rad * rad_to_deg);
    }
    return std::nullopt;
}

std::vector<TelemetryPlaybackState::ForwardClearanceSample>
build_forward_clearance_samples(const animus::telemetry_core::TelemetrySample &sample,
                                const ForwardClearanceTerrainSampler &terrain_sampler,
                                const ForwardClearanceCalculator &clearance_calculator,
                                const AppConfigStatusThresholds &thresholds)
{
    std::vector<TelemetryPlaybackState::ForwardClearanceSample> samples;
    const auto heading_deg = selected_sample_forward_heading_deg(sample);
    if (!heading_deg || !sample.ground_speed_mps || *sample.ground_speed_mps <= 0.0)
    {
        return samples;
    }

    samples.reserve(forward_clearance_horizons_s().size());
    for (const double horizon_s : forward_clearance_horizons_s())
    {
        const auto [lat_deg, lon_deg] = project_lat_lon(
            sample.lat_deg, sample.lon_deg, *heading_deg, *sample.ground_speed_mps * horizon_s);
        TelemetryPlaybackState::ForwardClearanceSample forward_sample;
        forward_sample.horizon_s = horizon_s;
        forward_sample.lat_deg = lat_deg;
        forward_sample.lon_deg = lon_deg;

        const auto terrain = terrain_sampler(lat_deg, lon_deg);
        if (terrain)
        {
            bool datum_uncertain = false;
            animus::telemetry_core::TelemetrySample predicted_sample = sample;
            predicted_sample.lat_deg = lat_deg;
            predicted_sample.lon_deg = lon_deg;
            forward_sample.terrain_elevation_m = terrain->terrain_elevation_m;
            forward_sample.terrain_clearance_m = clearance_calculator(
                predicted_sample, terrain->terrain_elevation_m, datum_uncertain);
            forward_sample.confidence =
                datum_uncertain ? TelemetryPlaybackState::TerrainConfidence::DatumUncertain
                                : terrain->confidence;
        }
        forward_sample.status = terrain_clearance_status(
            forward_sample.terrain_clearance_m, forward_sample.confidence, thresholds);
        samples.push_back(forward_sample);
    }
    return samples;
}

} // namespace animus::app
