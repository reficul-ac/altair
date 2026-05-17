# Animus QGC Map Audit

This audit records the QGroundControl map areas that should be inspected while porting behavior into the canonical Qt Animus shell under `tools/animus-qt/`.

## Licensing Boundary

QGroundControl is published under Apache 2.0 or GPLv3-or-later for source code, with artwork and images under CC BY-SA. Animus should prefer Apache 2.0-compatible source paths, keep copyright and license notices with any imported code, and avoid importing artwork unless CC BY-SA obligations are intentionally accepted.

The current implementation adds original Altair scaffolding only. No QGC source or artwork has been vendored.

## Files And Classes To Inspect

Inspect these QGC areas before each porting slice:

- `src/FlightMap/FlightMap.qml`: base map composition, follow behavior, vehicle/home/mission layering, and interaction defaults.
- `src/FlightMap/MapScale.qml`: scale indicator behavior and QML integration.
- `src/FlightMap/Widgets/`: map controls, centering, zoom, and optional layer controls.
- `src/FlightMap/MapItems/`: vehicle, home, mission, geofence, rally, and breadcrumb visual patterns.
- QGC map engine sources commonly named around `QGCMapEngine`, `QGCMapUrlEngine`, tile download tasks, cache workers, and offline map providers.
- QGC settings and provider factories for street, satellite, hybrid, topo, and custom URL sources.
- Mission, fence, rally, and vehicle model adapters that expose typed QML models without requiring broad app globals.

## Porting Rules

- Do not vendor all of QGC.
- Keep Animus model ownership in Altair: telemetry, mission, fence, rally, map-pack policy, and SITL workflow state remain under `tools/animus-qt/src`.
- Port only map-facing provider/cache/offline behavior and QML item patterns needed for QGC-like operator behavior.
- Keep tile loading, disk IO, network requests, and terrain work outside the MAVLink receive/decode path.
- Preserve attribution and license metadata for every provider or offline pack.

## Initial Animus Mapping

The first Qt scaffold maps the planned architecture this way:

- `tools/animus-qt/src/models/VehicleModel.*`: selected vehicle state exposed to QML and Cesium.
- `tools/animus-qt/src/telemetry/BreadcrumbPathModel.*`: bounded, decimated trail storage for map overlays.
- `tools/animus-qt/src/telemetry/TelemetryService.*`: UI-rate-limited mock publisher and future MAVLink service boundary.
- `tools/animus-qt/src/maps/MapSourceRegistry.*`: centralized source/provider metadata.
- `tools/animus-qt/src/maps/OfflineMapManager.*`: online, cached/offline, and strict-offline policy.
- `tools/animus-qt/src/maps/MapPackManager.*`: `map_packs/<name>/metadata.json` discovery and validation.
- `tools/animus-qt/qml/Map2DView.qml`: strict-offline QtQuick map fallback with selected vehicle, home, trail, snap, and attribution.
- `tools/animus-qt/qml/Terrain3DView.qml` and `tools/animus-qt/web/cesium/`: deterministic terrain preview and future Qt WebEngine/Cesium bridge assets.

## Remaining Audit Work

The repository does not currently include a QGC checkout. Before importing QGC-derived implementation code, check out the exact upstream revision, record commit SHA and license headers here, and add a table mapping each imported or adapted file to its Animus destination.
