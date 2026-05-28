#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <array>
#include <cstdint>
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
[[nodiscard]] const VehicleVisualStyle &
resolve_entity_visual_style(const VehicleVisualRegistry &registry,
                            const animus::telemetry_core::Entity &entity);
[[nodiscard]] std::string vehicle_visual_label(const VehicleVisualStyle &style,
                                               animus::telemetry_core::EntityId entity_id,
                                               bool stale);

} // namespace animus::app
