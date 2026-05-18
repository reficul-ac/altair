# Animus QGC Map Audit

This audit records the QGroundControl map areas that should be inspected while porting behavior into the canonical Qt Animus shell under `tools/animus-qt/`.

## Licensing Boundary

QGroundControl is published under Apache 2.0 or GPLv3-or-later for source code, with artwork and images under CC BY-SA. Animus should prefer Apache 2.0-compatible source paths, keep copyright and license notices with any imported code, and avoid importing artwork unless CC BY-SA obligations are intentionally accepted.

The current implementation adds original Altair scaffolding modeled on QGC map
subsystem responsibilities. No QGC source or artwork has been vendored.

## Pinned QGC Revision

Audit baseline:

- Repository: `https://github.com/mavlink/qgroundcontrol.git`
- Branch: `master`
- Commit: `aadca1bed71253a56807ba61021677693215829c`
- Checked: 2026-05-17 with `git ls-remote`

Any future QGC-derived source import must record the exact source file path,
copyright holder, SPDX or license header, commit SHA, and Animus destination
before the code lands. Keep the original license header adjacent to imported or
substantially adapted code. Do not copy QGC artwork into Altair unless the
resulting CC BY-SA obligations are accepted and documented.

## Files And Classes To Inspect

Inspect these QGC areas before each porting slice:

- `src/FlightMap/FlightMap.qml`: base map composition, follow behavior, vehicle/home/mission layering, and interaction defaults.
- `src/FlightMap/MapScale.qml`: scale indicator behavior and QML integration.
- `src/FlightMap/Widgets/`: map controls, centering, zoom, and optional layer controls.
- `src/FlightMap/MapItems/`: vehicle, home, mission, geofence, rally, and breadcrumb visual patterns.
- QGC map engine sources commonly named around `QGCMapEngine`, `QGCMapUrlEngine`, tile download tasks, cache workers, and offline map providers.
- QGC settings and provider factories for street, satellite, hybrid, topo, and custom URL sources.
- Mission, fence, rally, and vehicle model adapters that expose typed QML models without requiring broad app globals.

## Inspected Source Areas

The local QGC-style cache slice did not import QGC implementation code. It
records the areas to inspect before any direct QtLocation provider/cache port:

| QGC source area | Purpose to inspect | Animus destination |
| --- | --- | --- |
| `src/FlightMap/FlightMap.qml` | Map item layering, follow behavior, and vehicle/home/trail composition. | `tools/animus-qt/qml/Map2DView.qml` |
| `src/FlightMap/MapScale.qml` | Zoom/scale presentation and QML update cadence. | Future map controls in `tools/animus-qt/qml/Map2DView.qml` |
| `src/FlightMap/MapItems/` | Vehicle, home, mission, fence, rally, and breadcrumb visual roles. | Future typed overlay QML and Qt models under `tools/animus-qt/src` |
| `src/QtLocationPlugin/*` | Provider URL policy, cache workers, tile IO boundaries, QtLocation plugin registration, and offline metadata. | `tools/animus-qt/src/maps/qgc/AnimusMapCacheManager.*` and future `tools/animus-qt/src/maps/qgc/plugin/*` |
| `src/QtLocationPlugin/Providers/*` | Licensed provider defaults, attribution, average tile sizing, and custom URL handling. | `AnimusMapCacheManager` provider registry |

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
- `tools/animus-qt/src/maps/MapSourceRegistry.*`: compatibility source model retained for offline policy callers.
- `tools/animus-qt/src/maps/OfflineMapManager.*`: online, cached/offline, and strict-offline policy.
- `tools/animus-qt/src/maps/qgc/AnimusMapCacheManager.*`: QGC-style provider registry, cache DB initialization, tile-set metadata, tile-count/size estimates, and create/download/import/export/delete actions.
- `tools/animus-qt/qml/Map2DView.qml`: QtQuick map surface using the cache manager for QGC-style provider/cache state, with selected vehicle, home, breadcrumb, snap, attribution, and offline/cache warning overlays.
- `tools/animus-qt/qml/Terrain3DView.qml` and `tools/animus-qt/web/cesium/`: deterministic terrain preview and future Qt WebEngine/Cesium bridge assets.

## Source-To-Animus Mapping

No QGC-derived files have been imported. Current map work is original Altair
code:

| Source | Animus file | License handling |
| --- | --- | --- |
| Original Altair implementation | `tools/animus-qt/src/maps/qgc/AnimusMapCacheManager.*` | Altair repository license; implements provider policy and cache/tile-set metadata without vendored QGC code. |
| Original Altair implementation | `tools/animus-qt/qml/Map2DView.qml` | Altair repository license; composes the QtQuick map surface, markers, breadcrumbs, attribution, and cache/provider UI. |

## Remaining Audit Work

Before importing QGC-derived implementation code, check out the pinned upstream
revision or record a new one, inspect license headers in the exact files used,
and extend the source-to-Animus mapping table above.
