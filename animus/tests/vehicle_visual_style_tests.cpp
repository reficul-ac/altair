#include "vehicle_visual_style.hpp"

#include <gtest/gtest.h>

namespace
{

animus::telemetry_core::Entity entity()
{
    return {animus::telemetry_core::EntityId{1U, 1U}, std::nullopt};
}

} // namespace

TEST(VehicleVisualStyle, DefaultRegistryHasExpectedKinds)
{
    const auto &registry = animus::app::VehicleVisualRegistry::defaults();

    EXPECT_EQ(registry.style(animus::app::VehicleVisualKind::FixedWing).icon_shape,
              animus::app::VehicleVisualIconShape::FixedWing);
    EXPECT_EQ(registry.style(animus::app::VehicleVisualKind::Quadcopter).icon_shape,
              animus::app::VehicleVisualIconShape::Quadcopter);
    EXPECT_EQ(registry.style(animus::app::VehicleVisualKind::Rover).icon_shape,
              animus::app::VehicleVisualIconShape::Rover);
    EXPECT_EQ(registry.style(animus::app::VehicleVisualKind::SurfaceBoat).icon_shape,
              animus::app::VehicleVisualIconShape::SurfaceBoat);
    EXPECT_EQ(registry.style(animus::app::VehicleVisualKind::Unknown).icon_shape,
              animus::app::VehicleVisualIconShape::Circle);
}

TEST(VehicleVisualStyle, FixedWingIsDefaultWithoutTelemetryTypeMetadata)
{
    const auto &registry = animus::app::VehicleVisualRegistry::defaults();

    EXPECT_EQ(animus::app::default_vehicle_visual_kind(),
              animus::app::VehicleVisualKind::FixedWing);
    EXPECT_EQ(resolve_entity_visual_style(registry, entity()).kind,
              animus::app::VehicleVisualKind::FixedWing);
}

TEST(VehicleVisualStyle, InvalidLookupFallsBackToUnknown)
{
    const auto &registry = animus::app::VehicleVisualRegistry::defaults();
    const auto &style = registry.style(static_cast<animus::app::VehicleVisualKind>(99));

    EXPECT_EQ(style.kind, animus::app::VehicleVisualKind::Unknown);
    EXPECT_EQ(style.icon_shape, animus::app::VehicleVisualIconShape::Circle);
}

TEST(VehicleVisualStyle, StateVariantsPreserveFlagsAndScaleInvariants)
{
    const auto &registry = animus::app::VehicleVisualRegistry::defaults();
    const auto &style = registry.default_style();

    const auto &normal = registry.variant(style, {});
    const auto &selected = registry.variant(style, {.selected = true});
    const auto &stale = registry.variant(style, {.stale = true});
    const auto &degraded = registry.variant(style, {.degraded = true});
    const auto &selected_degraded = registry.variant(style, {.selected = true, .degraded = true});

    EXPECT_FALSE(normal.selected);
    EXPECT_FALSE(normal.stale);
    EXPECT_FALSE(normal.degraded);
    EXPECT_TRUE(selected.selected);
    EXPECT_TRUE(stale.stale);
    EXPECT_TRUE(degraded.degraded);
    EXPECT_TRUE(selected_degraded.selected);
    EXPECT_TRUE(selected_degraded.degraded);
    EXPECT_GE(selected.scale, normal.scale);
    EXPECT_GE(selected_degraded.scale, degraded.scale);
    EXPECT_GT(style.trail_thickness, 0.0F);
    EXPECT_GT(style.nominal_scale, 0.0F);
}

TEST(VehicleVisualStyle, LabelsRemainCompatibleWithExistingEntityLabels)
{
    const auto &registry = animus::app::VehicleVisualRegistry::defaults();
    const auto &style = registry.default_style();

    EXPECT_EQ(animus::app::vehicle_visual_label(style, {42U, 7U}, false), "42:7");
    EXPECT_EQ(animus::app::vehicle_visual_label(style, {42U, 7U}, true), "42:7 stale");
}
