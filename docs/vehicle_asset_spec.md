# Animus Vehicle Asset Specification

Animus vehicle assets use GLB/glTF 2.0 as the native runtime model format plus a
small YAML descriptor that tells Animus how to interpret the model for telemetry
visualization. Runtime vehicle assets should not use OBJ, FBX, STL, Collada, or
Blender files as the primary delivery format. Those may become import/source
formats later, but runtime packages should use `.glb`.

## Package Layout

```text
assets/vehicles/generic_quadcopter/
  vehicle.animus.yaml
  generic_quadcopter.glb
  generic_quadcopter_lod1.glb
  generic_quadcopter_lod2.glb
  preview.png
  README.md
```

The `.glb` file is the visual asset. `vehicle.animus.yaml` is the Animus-specific
descriptor. LOD files, preview image, and README are optional unless a project or
distribution profile later requires them.

## Vehicle Space

Animus vehicle-space convention:

- `+Y` is forward/nose.
- `+X` is right/right wing.
- `+Z` is up.
- Origin is center of mass or body center.
- Units are meters.

Imported GLB files are not assumed to use this convention. The descriptor must
state the source forward/up axes and any yaw/pitch/roll offsets needed to convert
source model space into Animus vehicle space.

Telemetry/world placement composes transforms in this order:

```text
worldTransform =
  terrain/world placement transform
  * telemetry yaw/pitch/roll transform
  * model correction transform
  * origin offset transform
  * scale transform
```

The renderer must support model correction, yaw/pitch/roll offsets, scale,
origin offset, and optional body axes debug display.

## Descriptor Example

```yaml
id: animus.quadcopter.generic
displayName: Generic Quadcopter
type: quadcopter

model:
  file: generic_quadcopter.glb
  unitScale: 1.0

orientation:
  sourceForward: "+Z"
  sourceUp: "+Y"
  animusForward: "+Y"
  animusUp: "+Z"
  yawOffsetDeg: 0.0
  pitchOffsetDeg: 0.0
  rollOffsetDeg: 0.0

origin:
  meaning: center_of_mass
  offsetMeters: [0.0, 0.0, 0.0]

dimensions:
  lengthMeters: 0.45
  wingspanMeters: 0.45
  heightMeters: 0.12

nodes:
  body: "body"
  propellers:
    frontLeft: "prop_front_left"
    frontRight: "prop_front_right"
    backLeft: "prop_back_left"
    backRight: "prop_back_right"
  cameraGimbal: "camera_gimbal"

rendering:
  defaultScale: 1.0
  minScreenSizePx: 28
  maxScreenSizePx: 220
  showLabel: true
  showTrack: true
  showHeadingVector: true

lod:
  high: generic_quadcopter.glb
  medium: generic_quadcopter_lod1.glb
  low: generic_quadcopter_lod2.glb

telemetryMapping:
  pose:
    latitude: latDeg
    longitude: lonDeg
    altitude: altitudeMeters
    yaw: headingDeg
    pitch: pitchDeg
    roll: rollDeg
  animation:
    propellerRpm: motorRpm
    gimbalYaw: gimbalYawDeg
    gimbalPitch: gimbalPitchDeg
```

## Required Fields

- `id`: globally unique inside the app.
- `displayName`: user-facing label.
- `type`: one of `quadcopter`, `rc_plane`, `fixed_wing_uav`, `rover`, `vessel`,
  `generic`, or `unknown`.
- `model.file`: relative path to the primary `.glb`.
- `orientation`: source axes, Animus axes, and yaw/pitch/roll offsets.
- `origin`: origin meaning and meter offset.

Recommended fields:

- `dimensions`: physical size for bounds, labels, and initial scale checks.
- `nodes`: named nodes for body parts, propellers, gimbals, lights, or sensors.
- `rendering`: default overlay and display-mode preferences.
- `lod`: high/medium/low GLB paths.
- `telemetryMapping`: pose and animation field mapping when the telemetry source
  does not use Animus canonical field names.

## Validation Rules

Descriptor validation should report readable errors or warnings:

- Missing `id`, `displayName`, `type`, `model.file`, `orientation`, or `origin`
  is an error.
- Unknown `type` values are errors unless explicitly mapped to `unknown`.
- `model.file` must point to a `.glb` file.
- Missing primary model file is an error for model mode but must not prevent icon
  fallback rendering.
- Missing LOD files should warn and fall back to the highest available model.
- Missing optional node names should warn only when a feature needs them.
- Invalid axis strings, non-finite offsets, non-positive scale, or invalid
  dimensions are errors.
- Duplicate IDs across discovered packages are errors.

If a model fails to load or validate, the app must render the entity in icon
mode and keep telemetry selection, labels, tracks, and inspector behavior
working.

## Built-In Package Targets

Initial built-in packages should be descriptors first, with GLB assets added
only when vetted assets are available:

- `generic_quadcopter`: body, four propellers, optional camera gimbal.
- `generic_rc_plane`: body, propeller, ailerons, elevator, rudder.
- `generic_fixed_wing_uav`: body, wings, optional camera gimbal or sensor mount.

Do not add large random binary models just to satisfy the package layout. If no
GLB exists, provide descriptor examples and rely on fallback icon rendering.

## Display Modes

Vehicle rendering should support:

- Icon mode: billboard/icon, heading arrow, and label; used when far away or
  model unavailable.
- Model mode: GLB model with yaw/pitch/roll and selected outline/ring; used when
  close enough and model is available.
- Hybrid mode: simplified model or icon plus label, heading vector, altitude
  stem, and short track tail; used at medium range.

Display mode should be selected by distance, screen size, model availability,
user settings, and selected state.

## Telemetry Binding

Conceptual flow:

```text
TelemetrySample -> EntityState -> VehiclePose -> world transform -> model transform -> renderer
```

Conceptual pose:

```cpp
struct VehiclePose {
    double timeSeconds;
    double latDeg;
    double lonDeg;
    double altitudeMeters;
    AltitudeReference altitudeReference;
    double yawDeg;
    double pitchDeg;
    double rollDeg;
    bool hasValidPosition = false;
    bool hasValidOrientation = false;
};
```

Behavior:

- Latitude/longitude place the vehicle in the terrain/world frame.
- Altitude uses existing datum and terrain-height handling.
- Yaw/heading rotates the nose.
- Pitch/roll rotate the model body.
- Missing orientation falls back to heading-only or icon orientation.
- Invalid position marks the entity degraded and does not place it randomly.
- Stale telemetry fades/desaturates or warns while keeping the last known
  position.
