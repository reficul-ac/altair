# Animus Cesium Vendor Assets

This directory contains the offline CesiumJS runtime used by the Animus Qt
Terrain 3D workspace.

The bundled runtime is loaded only through Qt resource URLs. Animus does not use
Cesium Ion, CDN assets, online terrain, online imagery, or provider tokens.
Quantized-mesh terrain is read from the operator-local cache at
`map_cache/terrain/quantized-mesh` when that directory contains `layer.json`;
otherwise the workspace uses the bundled deterministic Stanford/cruise6dof
heightmap and raster imagery fixture.
