#pragma once

#include "app_config.hpp"
#include "telemetry_signal_catalog.hpp"
#include "ui.hpp"

#include "animus/terrain_core/terrain_stream.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace animus::app
{

enum class StatusRibbonLevel
{
    Ok,
    Caution,
    Warning,
    Unknown,
};

struct StatusRibbonPill
{
    std::string id;
    std::string label;
    StatusRibbonLevel level = StatusRibbonLevel::Unknown;
    std::string summary;
    std::vector<std::string> details;
    std::string action;
};

using StatusRibbonModel = std::array<StatusRibbonPill, 8>;

[[nodiscard]] const char *status_ribbon_level_label(StatusRibbonLevel level);

[[nodiscard]] StatusRibbonModel
build_status_ribbon_model(const Options &options,
                          const AppConfigStatusThresholds &thresholds,
                          const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                          const RuntimeSignalInputs &runtime,
                          const TelemetryPlaybackState &playback,
                          const UiState &ui_state,
                          const PlanVisualizationState &plan_state,
                          const VehicleRuntimeStatus &vehicle_status,
                          const ScreenshotToolState &screenshot_tool,
                          const Mp4RecorderState &recorder,
                          std::size_t resident_gpu_bytes);

} // namespace animus::app
