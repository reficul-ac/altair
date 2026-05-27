# Animus Vehicle Core

`vehicle_core` owns vehicle descriptor loading, registry validation, and
CPU-side static GLB mesh loading. It does not include app UI, OpenGL resource
ownership, telemetry playback, or Altair/Bayek vehicle assumptions.

The current GLB path is intentionally small: triangle meshes, positions,
optional normals, indices, node transforms, and material base color factors.
Textures, skins, animation, morph targets, and advanced glTF extensions are
left for later phases.
