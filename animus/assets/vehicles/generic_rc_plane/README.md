# Generic RC Plane

This package is the built-in Animus fallback vehicle model. Every telemetry
entity currently maps to `animus.rc_plane.generic` until a future assignment
system provides per-entity vehicle choices.

`generic_rc_plane.glb` is generated in-repository by:

```bash
python3 animus/tools/generate_generic_rc_plane_glb.py
```

The generated low-poly model uses simple analytic geometry: fuselage, wing,
tail surfaces, nose marker, and prop disk marker. It does not derive from or
embed any external model asset.
