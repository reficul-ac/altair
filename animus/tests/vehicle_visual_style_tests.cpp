#include "vehicle_visual_style.hpp"

#include "animus/vehicle_core/vehicle_definition.hpp"

#include <filesystem>

#include <gtest/gtest.h>

namespace
{

animus::telemetry_core::Entity entity()
{
    return {animus::telemetry_core::EntityId{1U, 1U}, std::nullopt};
}

animus::vehicle_core::VehicleRegistry registry()
{
    return animus::vehicle_core::VehicleRegistry::load_from_directory(
        std::filesystem::path(ANIMUS_SOURCE_DIR) / "assets" / "vehicles");
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

TEST(VehicleVisualStyle, ResolvesDefaultRegistryVehicle)
{
    const auto vehicles = registry();
    const animus::app::VehicleVisualAssignments assignments;

    const auto resolved =
        animus::app::resolve_vehicle_visual(vehicles, assignments, entity(), true, "loaded");

    EXPECT_EQ(resolved.entity_key, "1:1");
    EXPECT_EQ(resolved.vehicle_id, "animus.rc_plane.generic");
    EXPECT_EQ(resolved.vehicle_type, "rc_plane");
    EXPECT_EQ(resolved.icon_kind, animus::app::VehicleVisualKind::FixedWing);
    EXPECT_TRUE(resolved.model_loaded);
    EXPECT_TRUE(resolved.fallback_reason.empty());
}

TEST(VehicleVisualStyle, PerEntityAssignmentOverridesTypeDefault)
{
    const auto vehicles = registry();
    animus::app::VehicleVisualAssignments assignments;
    assignments.defaults_by_type["rc_plane"] = "missing.default";
    assignments.entities["1:1"] = {
        "animus.rc_plane.generic", true, 2.0F, "none", "terrain_resolved"};

    const auto resolved =
        animus::app::resolve_vehicle_visual(vehicles, assignments, entity(), true, "loaded");

    EXPECT_EQ(resolved.vehicle_id, "animus.rc_plane.generic");
    EXPECT_TRUE(resolved.force_icon_only);
    EXPECT_FALSE(resolved.model_loaded);
    EXPECT_EQ(resolved.model_status, "icon-only");
    EXPECT_EQ(resolved.heading_source, "none");
    EXPECT_FLOAT_EQ(resolved.scale, 2.0F);
}

TEST(VehicleVisualStyle, InvalidAssignmentFallsBackWithReason)
{
    const auto vehicles = registry();
    animus::app::VehicleVisualAssignments assignments;
    assignments.entities["1:1"] = {"missing.vehicle", false, 200.0F, "auto", "terrain_resolved"};

    const auto resolved =
        animus::app::resolve_vehicle_visual(vehicles, assignments, entity(), false, "not loaded");

    EXPECT_EQ(resolved.vehicle_id, "animus.rc_plane.generic");
    EXPECT_EQ(resolved.vehicle_type, "rc_plane");
    EXPECT_FALSE(resolved.model_loaded);
    EXPECT_EQ(resolved.scale, 10.0F);
    EXPECT_NE(resolved.fallback_reason.find("assigned model not found"), std::string::npos);
}
