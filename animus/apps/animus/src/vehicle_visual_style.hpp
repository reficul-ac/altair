#pragma once

#include "animus/telemetry_core/telemetry.hpp"
#include "animus/vehicle_core/vehicle_definition.hpp"
#include "vehicle_visual_assignment.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace animus::app
{

enum class VehicleVisualKind
{
    FixedWing,
    Quadcopter,
    Rover,
    SurfaceBoat,
    Unknown,
};

enum class VehicleVisualIconShape
{
    FixedWing,
    Quadcopter,
    Rover,
    SurfaceBoat,
    Circle,
};

enum class VehicleVisualHeadingIndicator
{
    None,
    NoseLine,
};

struct VehicleVisualColor
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend bool operator==(const VehicleVisualColor &, const VehicleVisualColor &) = default;
};

struct VehicleVisualVariant
{
    VehicleVisualColor fill;
    VehicleVisualColor stroke;
    VehicleVisualColor shadow;
    VehicleVisualColor heading;
    VehicleVisualColor label_text;
    VehicleVisualColor label_background;
    VehicleVisualColor label_stroke;
    float scale = 1.0F;
    float stroke_thickness = 1.0F;
    bool selected = false;
    bool stale = false;
    bool degraded = false;
};

struct VehicleVisualStyle
{
    VehicleVisualKind kind = VehicleVisualKind::Unknown;
    VehicleVisualIconShape icon_shape = VehicleVisualIconShape::Circle;
    VehicleVisualHeadingIndicator heading_indicator = VehicleVisualHeadingIndicator::NoseLine;
    std::string_view name;
    std::string_view label_template;
    VehicleVisualColor trail_color;
    float trail_thickness = 2.0F;
    float nominal_scale = 1.0F;
    VehicleVisualVariant normal;
    VehicleVisualVariant selected;
    VehicleVisualVariant stale;
    VehicleVisualVariant selected_stale;
    VehicleVisualVariant degraded;
    VehicleVisualVariant selected_degraded;
};

struct VehicleVisualState
{
    bool selected = false;
    bool stale = false;
    bool degraded = false;
};

struct VehicleResolvedVisual
{
    std::string entity_key;
    std::string detected_type = "unknown";
    std::string vehicle_id = "animus.rc_plane.generic";
    std::string vehicle_name = "Generic RC Plane";
    std::string vehicle_type = "rc_plane";
    VehicleVisualKind icon_kind = VehicleVisualKind::FixedWing;
    bool force_icon_only = false;
    float scale = 1.0F;
    std::string heading_source = "auto";
    std::string altitude_placement = "terrain_resolved";
    bool model_loaded = false;
    std::string model_status = "fallback icon";
    std::string fallback_reason;
};

class VehicleVisualRegistry
{
  public:
    [[nodiscard]] static const VehicleVisualRegistry &defaults();

    [[nodiscard]] const VehicleVisualStyle &style(VehicleVisualKind kind) const;
    [[nodiscard]] const VehicleVisualStyle &default_style() const;
    [[nodiscard]] const VehicleVisualStyle &unknown_style() const;
    [[nodiscard]] const VehicleVisualVariant &variant(const VehicleVisualStyle &style,
                                                      VehicleVisualState state) const;

  private:
    std::array<VehicleVisualStyle, 5> styles_{};

    VehicleVisualRegistry();
};

[[nodiscard]] VehicleVisualKind default_vehicle_visual_kind();
[[nodiscard]] std::string vehicle_assignment_entity_key(animus::telemetry_core::EntityId entity_id);
[[nodiscard]] std::optional<animus::telemetry_core::EntityId>
parse_vehicle_assignment_entity_key(std::string_view key);
[[nodiscard]] VehicleVisualKind
vehicle_visual_kind_for_type(animus::vehicle_core::VehicleType type);
[[nodiscard]] VehicleResolvedVisual
resolve_vehicle_visual(const animus::vehicle_core::VehicleRegistry &vehicle_registry,
                       const VehicleVisualAssignments &assignments,
                       const animus::telemetry_core::Entity &entity,
                       bool assigned_model_loaded,
                       std::string_view assigned_model_status);
[[nodiscard]] const VehicleVisualStyle &
resolve_entity_visual_style(const VehicleVisualRegistry &registry,
                            const animus::telemetry_core::Entity &entity);
[[nodiscard]] const VehicleVisualStyle &
resolve_entity_visual_style(const VehicleVisualRegistry &registry,
                            const VehicleResolvedVisual &visual);
[[nodiscard]] std::string vehicle_visual_label(const VehicleVisualStyle &style,
                                               animus::telemetry_core::EntityId entity_id,
                                               bool stale);

} // namespace animus::app
