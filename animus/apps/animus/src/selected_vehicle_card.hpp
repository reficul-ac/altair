#pragma once

#include "app_config.hpp"
#include "ui.hpp"

#include <string>
#include <vector>

namespace animus::app
{

enum class SelectedVehicleCardStatus
{
    Ok,
    Caution,
    Warning,
    Unknown,
};

struct SelectedVehicleCardMetric
{
    std::string label;
    std::string value = "--";
};

struct SelectedVehicleCardModel
{
    SelectedVehicleCardStatus status = SelectedVehicleCardStatus::Unknown;
    std::string status_label = "Unknown";
    std::string entity_label = "--";
    std::string mode = "--";
    std::string telemetry_state = "--";
    std::string telemetry_age = "--";
    std::string detected_type = "--";
    std::string visual_assignment = "--";
    std::string visual_status = "--";
    std::string visual_fallback = "--";
    std::string heading_source = "--";
    std::string altitude_placement = "--";
    float visual_scale = 1.0F;
    bool force_icon_only = false;
    std::string test = "--";
    std::string phase = "--";
    std::string target = "--";
    std::string terrain_confidence = "--";
    std::string forward_clearance_summary = "--";
    std::vector<SelectedVehicleCardMetric> position_metrics;
    std::vector<SelectedVehicleCardMetric> motion_metrics;
    std::vector<std::string> warnings;
};

[[nodiscard]] const char *selected_vehicle_card_status_label(SelectedVehicleCardStatus status);
[[nodiscard]] SelectedVehicleCardModel
build_selected_vehicle_card_model(const TelemetryPlaybackState &playback,
                                  const UiState &ui_state,
                                  const VehicleRuntimeStatus &vehicle_status,
                                  const AppConfigStatusThresholds &thresholds);

} // namespace animus::app
