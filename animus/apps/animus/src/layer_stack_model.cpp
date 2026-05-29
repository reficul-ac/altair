#include "layer_stack_model.hpp"

namespace animus::app
{
namespace
{

LayerStackRow make_row(std::string id,
                       std::string label,
                       const bool visible,
                       const bool available,
                       std::string source,
                       std::string status,
                       std::string details)
{
    LayerStackRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.visible = visible;
    row.available = available;
    row.source = std::move(source);
    row.status = std::move(status);
    row.details = std::move(details);
    return row;
}

void add_warning(LayerStackRow &row, const LayerWarningLevel level, std::string badge)
{
    row.warning = level;
    row.warning_badge = std::move(badge);
}

} // namespace

std::vector<LayerStackRow> build_layer_stack_rows(const AppLayerSettings &settings,
                                                  const LayerStackContext &context)
{
    std::vector<LayerStackRow> rows;
    rows.reserve(14U);

    rows.push_back(make_row("vehicle_icons",
                            "Vehicle icons",
                            settings.vehicle_icons_visible,
                            context.telemetry_loaded,
                            context.telemetry_live ? "live telemetry" : "offline telemetry",
                            context.telemetry_loaded ? "active" : "unavailable",
                            "Fallback vehicle icons and selected-entity markers."));
    rows.push_back(make_row("vehicle_labels",
                            "Vehicle labels",
                            settings.vehicle_labels_visible,
                            context.telemetry_loaded,
                            context.telemetry_live ? "live telemetry" : "offline telemetry",
                            context.telemetry_loaded ? "active" : "unavailable",
                            "Compact entity labels gated independently from vehicle icons."));
    rows.push_back(make_row("track_tail",
                            "Track tail",
                            settings.track_tail_visible,
                            context.telemetry_loaded,
                            context.telemetry_live ? "live telemetry" : "offline telemetry",
                            context.telemetry_loaded ? "active" : "unavailable",
                            "Selected entity trail or live tail using existing decimation."));
    rows.push_back(make_row("heading_vectors",
                            "Heading vectors",
                            settings.heading_vectors_visible,
                            context.telemetry_loaded,
                            context.telemetry_live ? "live telemetry" : "offline telemetry",
                            context.telemetry_loaded ? "active" : "unavailable",
                            "Nose-line heading indicators from current entity samples."));

    LayerStackRow route =
        make_row("planned_route",
                 "Planned route",
                 settings.planned_route_visible,
                 context.plan_loaded,
                 context.plan_loaded ? "QGC plan" : "none",
                 context.plan_loaded ? "active" : "unavailable",
                 "Mission waypoint path, complex-item outlines, and route arrows.");
    if (context.plan_error)
    {
        add_warning(route, LayerWarningLevel::Warning, "plan");
        route.status = "error";
    }
    rows.push_back(std::move(route));

    LayerStackRow fence = make_row("geofence_rally",
                                   "Geofence/rally",
                                   settings.geofence_rally_visible,
                                   context.plan_loaded,
                                   context.plan_loaded ? "QGC plan" : "none",
                                   context.plan_loaded ? "active" : "unavailable",
                                   "QGC geofence polygons, circles, and rally points.");
    if (context.plan_error)
    {
        add_warning(fence, LayerWarningLevel::Warning, "plan");
        fence.status = "error";
    }
    rows.push_back(std::move(fence));

    LayerStackRow confidence =
        make_row("terrain_confidence",
                 "Terrain confidence",
                 settings.terrain_confidence_visible,
                 context.terrain_confidence_available,
                 "resident terrain",
                 context.terrain_confidence_available ? "active" : "unavailable",
                 "Selected-entity confidence ring showing exact, fallback, synthetic, or "
                 "unavailable terrain.");
    if (context.fallback_tiles > 0U || context.synthetic_tiles > 0U)
    {
        add_warning(confidence, LayerWarningLevel::Info, "fallback");
    }
    rows.push_back(std::move(confidence));

    LayerStackRow heatmap =
        make_row("terrain_clearance_heatmap",
                 "Terrain clearance heatmap",
                 settings.terrain_clearance_heatmap_visible,
                 false,
                 "not implemented",
                 "unavailable",
                 "Persisted Phase 10 preference only; no heatmap renderer is added.");
    add_warning(heatmap, LayerWarningLevel::Info, "unavailable");
    rows.push_back(std::move(heatmap));

    LayerStackRow overlay = make_row("geotiff_overlay",
                                     "GeoTIFF overlay",
                                     settings.geotiff_overlay_visible,
                                     context.geotiff_configured && !context.geotiff_missing,
                                     context.geotiff_configured ? "GeoTIFF" : "none",
                                     context.geotiff_configured ? "configured" : "unavailable",
                                     "Configured raster overlays draped over the terrain mesh.");
    overlay.has_opacity = true;
    overlay.opacity = settings.geotiff_overlay_opacity;
    overlay.has_draw_order = true;
    overlay.draw_order = settings.geotiff_overlay_draw_order;
    overlay.order_label = std::to_string(settings.geotiff_overlay_draw_order);
    if (context.geotiff_missing)
    {
        add_warning(overlay, LayerWarningLevel::Warning, "missing");
        overlay.status = "missing";
    }
    rows.push_back(std::move(overlay));

    LayerStackRow bathy = make_row("bathymetry",
                                   "Bathymetry",
                                   settings.bathymetry_visible,
                                   context.bathymetry_configured && !context.bathymetry_missing,
                                   context.bathymetry_configured ? "GeoTIFF" : "none",
                                   context.bathymetry_runtime_enabled ? "active" : "unavailable",
                                   "Bathymetry merge preference for terrain tile loading.");
    bathy.has_opacity = true;
    bathy.opacity = settings.bathymetry_opacity;
    if (context.bathymetry_missing)
    {
        add_warning(bathy, LayerWarningLevel::Warning, "missing");
        bathy.status = "missing";
    }
    rows.push_back(std::move(bathy));

    rows.push_back(make_row("hillshade",
                            "Hillshade",
                            settings.hillshade_visible,
                            context.terrain_tiles_loaded,
                            "height raster",
                            context.terrain_tiles_loaded ? "active" : "loading",
                            "Built-in terrain lighting and contour shading path."));

    LayerStackRow tile_debug =
        make_row("tile_state_debug",
                 "Tile state debug",
                 settings.tile_state_debug_visible,
                 true,
                 "runtime state",
                 settings.tile_state_debug_visible ? "enabled" : "off",
                 "Current tile debug color overlay; detailed tables remain Developer-only.");
    if (context.failed_tiles > 0U)
    {
        add_warning(tile_debug, LayerWarningLevel::Warning, "failures");
    }
    rows.push_back(std::move(tile_debug));

    LayerStackRow fallback =
        make_row("fallback_highlight",
                 "Fallback highlight",
                 settings.fallback_highlight_visible,
                 true,
                 "parent/synthetic",
                 settings.fallback_highlight_visible ? "enabled" : "off",
                 "Highlights parent fallback and synthetic terrain using existing tint paths.");
    if (context.fallback_tiles > 0U || context.synthetic_tiles > 0U)
    {
        add_warning(fallback, LayerWarningLevel::Info, "fallback");
    }
    rows.push_back(std::move(fallback));

    return rows;
}

AppLayerSettings layer_preset_operator_clean(const AppLayerSettings &current,
                                             const bool plan_loaded)
{
    AppLayerSettings next = current;
    next.vehicle_icons_visible = true;
    next.vehicle_labels_visible = true;
    next.track_tail_visible = true;
    next.heading_vectors_visible = true;
    next.planned_route_visible = plan_loaded;
    next.geofence_rally_visible = plan_loaded;
    next.tile_state_debug_visible = false;
    next.fallback_highlight_visible = false;
    return next;
}

AppLayerSettings layer_preset_terrain_analysis(const AppLayerSettings &current,
                                               const bool bathymetry_configured)
{
    AppLayerSettings next = current;
    next.terrain_confidence_visible = true;
    next.fallback_highlight_visible = true;
    next.bathymetry_visible = bathymetry_configured;
    next.tile_state_debug_visible = false;
    return next;
}

AppLayerSettings layer_preset_mission_review(const AppLayerSettings &current,
                                             const bool plan_loaded)
{
    AppLayerSettings next = current;
    next.planned_route_visible = plan_loaded;
    next.geofence_rally_visible = plan_loaded;
    next.track_tail_visible = true;
    next.vehicle_labels_visible = true;
    next.vehicle_icons_visible = true;
    next.heading_vectors_visible = true;
    next.tile_state_debug_visible = false;
    return next;
}

AppLayerSettings layer_preset_debug_tiles(const AppLayerSettings &current)
{
    AppLayerSettings next = current;
    next.tile_state_debug_visible = true;
    next.fallback_highlight_visible = true;
    return next;
}

AppLayerSettings layer_preset_capture_mode(const AppLayerSettings &current, const bool plan_loaded)
{
    AppLayerSettings next = current;
    next.vehicle_icons_visible = true;
    next.vehicle_labels_visible = false;
    next.track_tail_visible = true;
    next.heading_vectors_visible = false;
    next.planned_route_visible = plan_loaded;
    next.geofence_rally_visible = plan_loaded;
    next.tile_state_debug_visible = false;
    next.fallback_highlight_visible = false;
    return next;
}

} // namespace animus::app
