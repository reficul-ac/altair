#pragma once

#include "app_config.hpp"
#include "ui.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace animus::app
{

struct ForwardClearanceTerrainSample
{
    double terrain_elevation_m = 0.0;
    TelemetryPlaybackState::TerrainConfidence confidence =
        TelemetryPlaybackState::TerrainConfidence::Unavailable;
};

using ForwardClearanceTerrainSampler =
    std::function<std::optional<ForwardClearanceTerrainSample>(double lat_deg, double lon_deg)>;
using ForwardClearanceCalculator =
    std::function<std::optional<double>(const animus::telemetry_core::TelemetrySample &sample,
                                        double terrain_elevation_m,
                                        bool &datum_uncertain)>;

[[nodiscard]] const std::vector<double> &forward_clearance_horizons_s();
[[nodiscard]] const char *
terrain_confidence_label(TelemetryPlaybackState::TerrainConfidence confidence);
[[nodiscard]] const char *
terrain_clearance_status_label(TelemetryPlaybackState::TerrainClearanceStatus status);
[[nodiscard]] TelemetryPlaybackState::TerrainClearanceStatus
terrain_confidence_status(TelemetryPlaybackState::TerrainConfidence confidence);
[[nodiscard]] TelemetryPlaybackState::TerrainClearanceStatus
terrain_clearance_status(std::optional<double> clearance_m,
                         TelemetryPlaybackState::TerrainConfidence confidence,
                         const AppConfigStatusThresholds &thresholds);
[[nodiscard]] TelemetryPlaybackState::TerrainClearanceStatus
worst_terrain_clearance_status(TelemetryPlaybackState::TerrainClearanceStatus lhs,
                               TelemetryPlaybackState::TerrainClearanceStatus rhs);
[[nodiscard]] TelemetryPlaybackState::TerrainClearanceStatus worst_forward_clearance_status(
    const std::vector<TelemetryPlaybackState::ForwardClearanceSample> &samples);
[[nodiscard]] std::optional<double>
selected_sample_forward_heading_deg(const animus::telemetry_core::TelemetrySample &sample);
[[nodiscard]] std::vector<TelemetryPlaybackState::ForwardClearanceSample>
build_forward_clearance_samples(const animus::telemetry_core::TelemetrySample &sample,
                                const ForwardClearanceTerrainSampler &terrain_sampler,
                                const ForwardClearanceCalculator &clearance_calculator,
                                const AppConfigStatusThresholds &thresholds);

} // namespace animus::app
