#include "ui_theme.hpp"

namespace animus::app
{
namespace
{

constexpr ImVec4 color(float r, float g, float b, float a)
{
    return ImVec4(r, g, b, a);
}

[[nodiscard]] ImU32 u32(const ImVec4 color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

} // namespace

const UiTheme &ui_theme()
{
    static const UiTheme theme{
        color(0.048F, 0.057F, 0.064F, 0.95F),
        color(0.066F, 0.077F, 0.086F, 0.93F),
        color(0.19F, 0.22F, 0.24F, 0.92F),
        color(0.145F, 0.165F, 0.178F, 0.90F),
        color(0.105F, 0.205F, 0.245F, 0.96F),
        color(0.88F, 0.92F, 0.95F, 1.0F),
        color(0.62F, 0.67F, 0.70F, 1.0F),
        color(0.31F, 0.65F, 0.84F, 1.0F),
        color(0.30F, 0.74F, 0.48F, 1.0F),
        color(0.92F, 0.63F, 0.25F, 1.0F),
        color(0.91F, 0.26F, 0.24F, 1.0F),
        color(0.46F, 0.50F, 0.53F, 1.0F),
        color(0.045F, 0.052F, 0.058F, 0.94F),
        color(0.24F, 0.28F, 0.31F, 0.92F),
        color(0.14F, 0.16F, 0.18F, 0.78F),
        color(0.24F, 0.28F, 0.31F, 0.70F),
        color(0.30F, 0.70F, 0.47F, 0.84F),
        color(0.33F, 0.58F, 0.78F, 0.84F),
        color(0.93F, 0.96F, 0.98F, 1.0F),
        38.0F,
        12.0F,
        128.0F,
        10.0F,
        316.0F,
        8.0F,
        6.0F,
        6.0F,
    };
    return theme;
}

void apply_animus_imgui_theme()
{
    const UiTheme &theme = ui_theme();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = theme.window_rounding;
    style.ChildRounding = theme.window_rounding;
    style.FrameRounding = theme.control_rounding;
    style.GrabRounding = theme.control_rounding;
    style.PopupRounding = theme.control_rounding;
    style.ScrollbarRounding = theme.control_rounding;
    style.TabRounding = theme.control_rounding;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.WindowPadding = ImVec2(12.0F, 10.0F);
    style.FramePadding = ImVec2(8.0F, 5.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = theme.text_primary;
    colors[ImGuiCol_TextDisabled] = theme.text_muted;
    colors[ImGuiCol_WindowBg] = theme.panel_bg;
    colors[ImGuiCol_ChildBg] = color(0.052F, 0.060F, 0.067F, 0.84F);
    colors[ImGuiCol_PopupBg] = color(0.060F, 0.070F, 0.078F, 0.98F);
    colors[ImGuiCol_Border] = theme.panel_border;
    colors[ImGuiCol_FrameBg] = color(0.095F, 0.110F, 0.122F, 0.96F);
    colors[ImGuiCol_FrameBgHovered] = color(0.130F, 0.154F, 0.170F, 1.0F);
    colors[ImGuiCol_FrameBgActive] = color(0.150F, 0.205F, 0.235F, 1.0F);
    colors[ImGuiCol_TitleBg] = theme.panel_bg;
    colors[ImGuiCol_TitleBgActive] = color(0.075F, 0.090F, 0.100F, 0.98F);
    colors[ImGuiCol_Button] = color(0.105F, 0.125F, 0.138F, 0.92F);
    colors[ImGuiCol_ButtonHovered] = theme.panel_hover;
    colors[ImGuiCol_ButtonActive] = theme.panel_selected;
    colors[ImGuiCol_Header] = theme.panel_selected;
    colors[ImGuiCol_HeaderHovered] = color(0.135F, 0.225F, 0.260F, 0.95F);
    colors[ImGuiCol_HeaderActive] = color(0.155F, 0.265F, 0.305F, 1.0F);
    colors[ImGuiCol_CheckMark] = theme.accent;
    colors[ImGuiCol_SliderGrab] = theme.accent;
    colors[ImGuiCol_SliderGrabActive] = color(0.44F, 0.76F, 0.92F, 1.0F);
    colors[ImGuiCol_Separator] = color(0.20F, 0.23F, 0.25F, 0.88F);
    colors[ImGuiCol_SeparatorHovered] = color(0.28F, 0.34F, 0.37F, 0.95F);
    colors[ImGuiCol_SeparatorActive] = theme.accent;
    colors[ImGuiCol_TableHeaderBg] = color(0.08F, 0.095F, 0.105F, 0.95F);
    colors[ImGuiCol_TableBorderStrong] = color(0.22F, 0.25F, 0.27F, 0.95F);
    colors[ImGuiCol_TableBorderLight] = color(0.16F, 0.18F, 0.20F, 0.90F);
    colors[ImGuiCol_Tab] = color(0.090F, 0.105F, 0.116F, 0.92F);
    colors[ImGuiCol_TabHovered] = theme.panel_hover;
    colors[ImGuiCol_TabActive] = theme.panel_selected;
}

ImVec4 status_level_color(const StatusRibbonLevel level)
{
    const UiTheme &theme = ui_theme();
    switch (level)
    {
    case StatusRibbonLevel::Ok:
        return theme.ok;
    case StatusRibbonLevel::Caution:
        return theme.caution;
    case StatusRibbonLevel::Warning:
        return theme.warning;
    case StatusRibbonLevel::Unknown:
        return theme.inactive;
    }
    return theme.inactive;
}

ImVec4 selected_vehicle_status_color(const SelectedVehicleCardStatus status)
{
    const UiTheme &theme = ui_theme();
    switch (status)
    {
    case SelectedVehicleCardStatus::Ok:
        return theme.ok;
    case SelectedVehicleCardStatus::Caution:
        return theme.caution;
    case SelectedVehicleCardStatus::Warning:
        return theme.warning;
    case SelectedVehicleCardStatus::Unknown:
        return theme.inactive;
    }
    return theme.inactive;
}

ImVec4 timeline_review_severity_color(const TimelineReviewSeverity severity)
{
    const UiTheme &theme = ui_theme();
    switch (severity)
    {
    case TimelineReviewSeverity::Info:
        return theme.accent;
    case TimelineReviewSeverity::Caution:
        return theme.caution;
    case TimelineReviewSeverity::Warning:
        return theme.warning;
    }
    return theme.inactive;
}

ImU32 timeline_marker_color(const TimelineReviewMarker &marker)
{
    const UiTheme &theme = ui_theme();
    switch (marker.category)
    {
    case TimelineReviewMarkerCategory::Bookmark:
    case TimelineReviewMarkerCategory::Capture:
    case TimelineReviewMarkerCategory::MaxSpeed:
    case TimelineReviewMarkerCategory::MinClearance:
        return u32(theme.accent);
    case TimelineReviewMarkerCategory::TerrainFallback:
    case TimelineReviewMarkerCategory::ModelFallback:
        return u32(theme.inactive);
    default:
        return u32(timeline_review_severity_color(marker.severity));
    }
}

} // namespace animus::app
