#pragma once

#include "selected_vehicle_card.hpp"
#include "status_ribbon.hpp"
#include "timeline_review.hpp"

#include <imgui.h>

namespace animus::app
{

struct UiTheme
{
    ImVec4 chrome_bg;
    ImVec4 panel_bg;
    ImVec4 panel_border;
    ImVec4 panel_hover;
    ImVec4 panel_selected;
    ImVec4 text_primary;
    ImVec4 text_muted;
    ImVec4 accent;
    ImVec4 ok;
    ImVec4 caution;
    ImVec4 warning;
    ImVec4 inactive;

    ImVec4 timeline_bg;
    ImVec4 timeline_border;
    ImVec4 timeline_lane;
    ImVec4 timeline_tick;
    ImVec4 timeline_loaded_live;
    ImVec4 timeline_loaded_review;
    ImVec4 timeline_now;

    float status_bar_height;
    float chrome_margin;
    float nav_width;
    float panel_gap;
    float inspector_width;
    float window_rounding;
    float control_rounding;
    float pill_rounding;
};

[[nodiscard]] const UiTheme &ui_theme();
void apply_animus_imgui_theme();

[[nodiscard]] ImVec4 status_level_color(StatusRibbonLevel level);
[[nodiscard]] ImVec4 selected_vehicle_status_color(SelectedVehicleCardStatus status);
[[nodiscard]] ImVec4 timeline_review_severity_color(TimelineReviewSeverity severity);
[[nodiscard]] ImU32 timeline_marker_color(const TimelineReviewMarker &marker);

} // namespace animus::app
