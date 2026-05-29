#include "vehicle_visual_style.hpp"

#include <algorithm>
#include <charconv>
#include <string>
#include <system_error>

namespace animus::app
{
namespace
{

constexpr VehicleVisualColor
rgba(const std::uint8_t r, const std::uint8_t g, const std::uint8_t b, const std::uint8_t a)
{
    return {r, g, b, a};
}

VehicleVisualVariant variant(const VehicleVisualColor fill,
                             const VehicleVisualColor stroke,
                             const VehicleVisualColor heading,
                             const VehicleVisualColor label_text,
                             const VehicleVisualColor label_background,
                             const VehicleVisualColor label_stroke,
                             const float scale,
                             const float stroke_thickness,
                             const bool selected,
                             const bool stale,
                             const bool degraded)
{
    return {
        fill,
        stroke,
        rgba(10, 16, 18, degraded ? 150 : 128),
        heading,
        label_text,
        label_background,
        label_stroke,
        scale,
        stroke_thickness,
        selected,
        stale,
        degraded,
    };
}

VehicleVisualStyle make_style(const VehicleVisualKind kind,
                              const VehicleVisualIconShape icon_shape,
                              const std::string_view name,
                              const VehicleVisualColor base_fill,
                              const VehicleVisualColor base_stroke,
                              const VehicleVisualColor selected_fill,
                              const VehicleVisualColor selected_stroke,
                              const VehicleVisualColor trail_color,
                              const float nominal_scale)
{
    VehicleVisualStyle style;
    style.kind = kind;
    style.icon_shape = icon_shape;
    style.heading_indicator = VehicleVisualHeadingIndicator::NoseLine;
    style.name = name;
    style.label_template = "{entity}";
    style.trail_color = trail_color;
    style.trail_thickness = 2.0F;
    style.nominal_scale = nominal_scale;
    style.normal = variant(base_fill,
                           base_stroke,
                           rgba(238, 242, 244, 132),
                           rgba(226, 232, 236, 214),
                           rgba(16, 20, 23, 166),
                           rgba(255, 255, 255, 34),
                           nominal_scale,
                           1.0F,
                           false,
                           false,
                           false);
    style.selected = variant(selected_fill,
                             selected_stroke,
                             rgba(164, 225, 255, 238),
                             rgba(236, 248, 255, 255),
                             rgba(18, 36, 46, 224),
                             rgba(107, 195, 238, 176),
                             nominal_scale * 1.18F,
                             1.6F,
                             true,
                             false,
                             false);
    style.stale = variant(rgba(202, 132, 61, 188),
                          rgba(238, 189, 125, 164),
                          rgba(238, 202, 154, 146),
                          rgba(248, 220, 184, 232),
                          rgba(54, 39, 28, 194),
                          rgba(255, 255, 255, 34),
                          nominal_scale,
                          1.0F,
                          false,
                          true,
                          false);
    style.selected_stale = variant(rgba(202, 132, 61, 238),
                                   rgba(238, 189, 125, 240),
                                   rgba(255, 221, 178, 238),
                                   rgba(255, 235, 204, 255),
                                   rgba(54, 39, 28, 224),
                                   rgba(238, 189, 125, 176),
                                   nominal_scale * 1.18F,
                                   1.6F,
                                   true,
                                   true,
                                   false);
    style.degraded = variant(rgba(125, 132, 142, 188),
                             rgba(188, 198, 208, 172),
                             rgba(212, 220, 228, 130),
                             rgba(224, 230, 236, 224),
                             rgba(30, 34, 38, 194),
                             rgba(255, 255, 255, 34),
                             nominal_scale,
                             1.0F,
                             false,
                             false,
                             true);
    style.selected_degraded = variant(rgba(132, 154, 174, 232),
                                      rgba(224, 236, 244, 232),
                                      rgba(224, 236, 244, 218),
                                      rgba(238, 244, 248, 255),
                                      rgba(28, 42, 50, 224),
                                      rgba(180, 218, 238, 154),
                                      nominal_scale * 1.18F,
                                      1.6F,
                                      true,
                                      false,
                                      true);
    return style;
}

std::string entity_label(const animus::telemetry_core::EntityId id)
{
    return std::to_string(id.system_id) + ":" + std::to_string(id.component_id);
}

float clamped_visual_scale(const float value)
{
    if (value < 0.1F)
    {
        return 0.1F;
    }
    if (value > 10.0F)
    {
        return 10.0F;
    }
    return value;
}

std::optional<std::uint8_t> parse_u8(std::string_view text)
{
    unsigned int value = 0U;
    const char *begin = text.data();
    const char *end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end || value > 255U)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

} // namespace

VehicleVisualRegistry::VehicleVisualRegistry()
{
    styles_[0] = make_style(VehicleVisualKind::FixedWing,
                            VehicleVisualIconShape::FixedWing,
                            "fixed-wing",
                            rgba(236, 198, 83, 208),
                            rgba(255, 239, 171, 154),
                            rgba(74, 172, 226, 255),
                            rgba(220, 244, 255, 246),
                            rgba(103, 181, 219, 178),
                            1.0F);
    styles_[1] = make_style(VehicleVisualKind::Quadcopter,
                            VehicleVisualIconShape::Quadcopter,
                            "quadcopter",
                            rgba(125, 221, 166, 210),
                            rgba(211, 249, 226, 164),
                            rgba(76, 197, 143, 255),
                            rgba(224, 255, 239, 246),
                            rgba(94, 210, 153, 178),
                            1.0F);
    styles_[2] = make_style(VehicleVisualKind::Rover,
                            VehicleVisualIconShape::Rover,
                            "rover",
                            rgba(228, 151, 92, 212),
                            rgba(255, 218, 174, 168),
                            rgba(222, 112, 72, 255),
                            rgba(255, 231, 205, 246),
                            rgba(232, 144, 92, 178),
                            0.96F);
    styles_[3] = make_style(VehicleVisualKind::SurfaceBoat,
                            VehicleVisualIconShape::SurfaceBoat,
                            "surface",
                            rgba(98, 196, 210, 210),
                            rgba(196, 244, 249, 164),
                            rgba(60, 176, 204, 255),
                            rgba(218, 249, 255, 246),
                            rgba(87, 196, 223, 178),
                            1.02F);
    styles_[4] = make_style(VehicleVisualKind::Unknown,
                            VehicleVisualIconShape::Circle,
                            "unknown",
                            rgba(196, 174, 218, 202),
                            rgba(235, 224, 246, 154),
                            rgba(154, 128, 210, 245),
                            rgba(244, 238, 255, 232),
                            rgba(170, 151, 214, 168),
                            0.92F);
}

const VehicleVisualRegistry &VehicleVisualRegistry::defaults()
{
    static const VehicleVisualRegistry registry;
    return registry;
}

const VehicleVisualStyle &VehicleVisualRegistry::style(const VehicleVisualKind kind) const
{
    for (const VehicleVisualStyle &style : styles_)
    {
        if (style.kind == kind)
        {
            return style;
        }
    }
    return unknown_style();
}

const VehicleVisualStyle &VehicleVisualRegistry::default_style() const
{
    return style(default_vehicle_visual_kind());
}

const VehicleVisualStyle &VehicleVisualRegistry::unknown_style() const
{
    return styles_[4];
}

const VehicleVisualVariant &VehicleVisualRegistry::variant(const VehicleVisualStyle &style,
                                                           const VehicleVisualState state) const
{
    if (state.selected && state.degraded)
    {
        return style.selected_degraded;
    }
    if (state.selected && state.stale)
    {
        return style.selected_stale;
    }
    if (state.selected)
    {
        return style.selected;
    }
    if (state.degraded)
    {
        return style.degraded;
    }
    if (state.stale)
    {
        return style.stale;
    }
    return style.normal;
}

VehicleVisualKind default_vehicle_visual_kind()
{
    return VehicleVisualKind::FixedWing;
}

std::string vehicle_assignment_entity_key(const animus::telemetry_core::EntityId entity_id)
{
    return entity_label(entity_id);
}

std::optional<animus::telemetry_core::EntityId>
parse_vehicle_assignment_entity_key(const std::string_view key)
{
    const std::size_t colon = key.find(':');
    if (colon == std::string_view::npos || colon == 0U || colon + 1U >= key.size())
    {
        return std::nullopt;
    }
    const std::optional<std::uint8_t> system_id = parse_u8(key.substr(0U, colon));
    const std::optional<std::uint8_t> component_id = parse_u8(key.substr(colon + 1U));
    if (!system_id || !component_id)
    {
        return std::nullopt;
    }
    return animus::telemetry_core::EntityId{*system_id, *component_id};
}

VehicleVisualKind vehicle_visual_kind_for_type(const animus::vehicle_core::VehicleType type)
{
    switch (type)
    {
    case animus::vehicle_core::VehicleType::RcPlane:
        return VehicleVisualKind::FixedWing;
    }
    return VehicleVisualKind::Unknown;
}

VehicleResolvedVisual
resolve_vehicle_visual(const animus::vehicle_core::VehicleRegistry &vehicle_registry,
                       const VehicleVisualAssignments &assignments,
                       const animus::telemetry_core::Entity &entity,
                       const bool assigned_model_loaded,
                       const std::string_view assigned_model_status)
{
    VehicleResolvedVisual visual;
    visual.entity_key = vehicle_assignment_entity_key(entity.id);

    const animus::vehicle_core::VehicleDefinition *default_definition =
        vehicle_registry.default_definition();
    if (default_definition != nullptr)
    {
        visual.vehicle_id = default_definition->id;
        visual.vehicle_name = default_definition->display_name;
        visual.vehicle_type =
            std::string(animus::vehicle_core::to_string(default_definition->type));
        visual.detected_type = visual.vehicle_type;
        visual.icon_kind = vehicle_visual_kind_for_type(default_definition->type);
    }

    VehicleVisualAssignment assignment;
    bool has_assignment = false;
    if (const auto entity_assignment = assignments.entities.find(visual.entity_key);
        entity_assignment != assignments.entities.end())
    {
        assignment = entity_assignment->second;
        has_assignment = true;
    }
    else if (const auto type_default = assignments.defaults_by_type.find(visual.detected_type);
             type_default != assignments.defaults_by_type.end())
    {
        assignment.vehicle_id = type_default->second;
        has_assignment = true;
    }

    if (has_assignment)
    {
        visual.force_icon_only = assignment.force_icon_only;
        visual.scale = clamped_visual_scale(assignment.scale);
        visual.heading_source = assignment.heading_source == "none" ? "none" : "auto";
        visual.altitude_placement = "terrain_resolved";
        const animus::vehicle_core::VehicleDefinition *assigned_definition =
            vehicle_registry.find(assignment.vehicle_id);
        if (assigned_definition == nullptr)
        {
            visual.fallback_reason = "assigned model not found: " + assignment.vehicle_id;
        }
        else
        {
            visual.vehicle_id = assigned_definition->id;
            visual.vehicle_name = assigned_definition->display_name;
            visual.vehicle_type =
                std::string(animus::vehicle_core::to_string(assigned_definition->type));
            visual.icon_kind = vehicle_visual_kind_for_type(assigned_definition->type);
        }
    }

    visual.model_loaded = assigned_model_loaded && !visual.force_icon_only;
    visual.model_status =
        std::string(assigned_model_status.empty() ? "fallback icon" : assigned_model_status);
    if (visual.force_icon_only)
    {
        visual.model_loaded = false;
        visual.model_status = "icon-only";
        visual.fallback_reason = "icon-only forced";
    }
    else if (!visual.model_loaded && visual.fallback_reason.empty())
    {
        visual.fallback_reason =
            visual.model_status == "loaded" ? "model unavailable" : visual.model_status;
    }
    return visual;
}

const VehicleVisualStyle &resolve_entity_visual_style(const VehicleVisualRegistry &registry,
                                                      const animus::telemetry_core::Entity &entity)
{
    (void)entity;
    return registry.default_style();
}

const VehicleVisualStyle &resolve_entity_visual_style(const VehicleVisualRegistry &registry,
                                                      const VehicleResolvedVisual &visual)
{
    return registry.style(visual.icon_kind);
}

std::string vehicle_visual_label(const VehicleVisualStyle &style,
                                 const animus::telemetry_core::EntityId entity_id,
                                 const bool stale)
{
    constexpr std::string_view token = "{entity}";
    std::string label(style.label_template);
    const std::size_t token_pos = label.find(token);
    if (token_pos == std::string::npos)
    {
        label = entity_label(entity_id);
    }
    else
    {
        label.replace(token_pos, token.size(), entity_label(entity_id));
    }
    if (stale)
    {
        label += " stale";
    }
    return label;
}

} // namespace animus::app
