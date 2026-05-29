#pragma once

#include "app_config.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace animus::app
{

enum class LayerWarningLevel
{
    None,
    Info,
    Warning,
};

struct LayerStackContext
{
    bool telemetry_loaded = false;
    bool telemetry_live = false;
    bool plan_loaded = false;
    bool plan_error = false;
    std::size_t plan_diagnostic_count = 0;
    std::size_t plan_route_points = 0;
    std::size_t plan_geofence_items = 0;
    std::size_t plan_rally_points = 0;
    bool geotiff_configured = false;
    bool geotiff_missing = false;
    bool bathymetry_configured = false;
    bool bathymetry_missing = false;
    bool bathymetry_runtime_enabled = false;
    bool terrain_tiles_loaded = false;
    std::size_t failed_tiles = 0;
    std::size_t fallback_tiles = 0;
    std::size_t synthetic_tiles = 0;
    bool terrain_confidence_available = false;
    bool terrain_clearance_available = false;
};

struct LayerStackRow
{
    std::string id;
    std::string label;
    bool visible = false;
    bool available = true;
    bool has_opacity = false;
    float opacity = 1.0F;
    bool has_draw_order = false;
    int draw_order = 0;
    std::string order_label;
    std::string source;
    std::string status;
    LayerWarningLevel warning = LayerWarningLevel::None;
    std::string warning_badge;
    std::string details;
};

[[nodiscard]] std::vector<LayerStackRow> build_layer_stack_rows(const AppLayerSettings &settings,
                                                                const LayerStackContext &context);
[[nodiscard]] AppLayerSettings layer_preset_operator_clean(const AppLayerSettings &current,
                                                           bool plan_loaded);
[[nodiscard]] AppLayerSettings layer_preset_terrain_analysis(const AppLayerSettings &current,
                                                             bool bathymetry_configured);
[[nodiscard]] AppLayerSettings layer_preset_mission_review(const AppLayerSettings &current,
                                                           bool plan_loaded);
[[nodiscard]] AppLayerSettings layer_preset_debug_tiles(const AppLayerSettings &current);
[[nodiscard]] AppLayerSettings layer_preset_capture_mode(const AppLayerSettings &current,
                                                         bool plan_loaded);

} // namespace animus::app
