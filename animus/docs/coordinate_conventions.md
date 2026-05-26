# Animus Coordinate Conventions

Animus uses Web Mercator XYZ tile addressing for terrain and imagery tile
identity.

## Geographic Input

Latitude and longitude inputs are degrees. `geo_core` clamps latitude to the
valid Web Mercator interval:

```text
[-85.0511287798066, 85.0511287798066]
```

Longitude is clamped to `[-180, 180]`. A longitude of exactly `180` maps to the
last tile in the row, not to a tile index past the eastern edge.

## XYZ Tiles

`TileCoord` uses Slippy Map XYZ indexing:

- `z` is the zoom level.
- `x` increases from west to east.
- `y` increases from north to south.
- zoom `z` has `2^z` tiles on each axis.
- supported zooms are `0..30`.

Stable tile keys use:

```text
z/x/y
```

Layer cache keys append that tile key to the layer cache prefix:

```text
<layer-prefix>/<z>/<x>/<y>
```

## Tile UV

Tile-local UV coordinates use a top-left origin:

- `u = 0` at the west edge and `u = 1` at the east edge.
- `v = 0` at the north edge and `v = 1` at the south edge.

This matches the XYZ tile convention where `y` increases downward.

## Local Render Frame

The early terrain renderer should convert tile-local UVs into a local render
frame near the visible patch instead of rendering directly in latitude and
longitude. Later ENU placement utilities should live in `geo_core` and remain
independent of OpenGL.

## Height Units

Terrain, elevation, and bathymetry heights are meters. Future telemetry altitude
must carry datum metadata before it is mixed with terrain height.
