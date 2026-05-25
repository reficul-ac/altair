(function () {
  const statusEl = document.getElementById('status');
  const canvas = document.getElementById('fallbackScene');
  const cesiumScene = document.getElementById('cesiumScene');
  const ctx = canvas.getContext('2d');

  let viewer = null;
  let vehicleEntity = null;
  let homeEntity = null;
  let headingEntity = null;
  let vehiclePointPrimitive = null;
  let homePointPrimitive = null;
  let aircraftOutlineCollection = null;
  let attitudeReferenceCollection = null;
  let terrainReferenceCollection = null;
  let trailPolylineCollection = null;
  let trailEntities = [];
  let overlayEntities = [];
  let imageryLayer = null;
  let terrainProviderKey = '';
  let aircraftModelUri = '';
  let aircraftModelRequestedUri = '';
  let aircraftModelPrimitive = null;
  let aircraftModelLoadToken = 0;
  let aircraftModelScale = 3.0;
  let vehicleModelController = null;
  let vehicleModelProfileKey = '';
  let vehicleModelProfile = null;
  let cameraInitialized = false;
  let cameraMode = 'chase';
  let workspaceMode = 'terrain-3d';
  let lastLockPosition = null;
  let cameraDrag = null;
  let activePointerInteraction = false;
  let spaceDown = false;
  const lockedCameraOffsets = {
    chase: {
      defaultHeadingDeg: 145.0,
      headingDeg: 145.0,
      pitchDeg: -68.0,
      rangeM: 170.0,
      followsVehicleHeading: true,
    },
    orbit: {
      defaultHeadingDeg: 135.0,
      headingDeg: 135.0,
      pitchDeg: -26.0,
      rangeM: 520.0,
      followsVehicleHeading: false,
    },
    tactical: {
      defaultHeadingDeg: 135.0,
      headingDeg: 135.0,
      pitchDeg: -15.0,
      rangeM: 170.0,
      followsVehicleHeading: false,
    },
  };
  const tacticalRingRadiusM = 7.5;
  const fpvVerticalFovDeg = 70.0;
  const fpvNoseOffsetM = {
    forward: 8.0,
    right: 0.0,
    up: 1.2,
  };
  const fpvDefaultPitchDeg = -8.0;
  const fpvLook = {
    yawDeg: 0.0,
    pitchDeg: fpvDefaultPitchDeg,
    forwardDot: 1.0,
  };
  const freeCamera = {
    focus: null,
    headingDeg: 0.0,
    pitchDeg: -32.0,
    rangeM: 500.0,
  };
  let state = {
    vehicle: null,
    home: null,
    trail: [],
    terrain: null,
    scene: null,
    overlays: null,
    model: null,
    controlSurfaces: [],
    clearance: null,
    config: null,
  };

  function numberOr(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? number : fallback;
  }

  function validPosition(point) {
    return point &&
      Number.isFinite(Number(point.latDeg)) &&
      Number.isFinite(Number(point.lonDeg)) &&
      Number.isFinite(Number(point.altitudeM));
  }

  function validOverlayPosition(point) {
    return point &&
      Number.isFinite(Number(point.latitudeDeg)) &&
      Number.isFinite(Number(point.longitudeDeg)) &&
      Number.isFinite(Number(point.altitudeM));
  }

  function overlayCartesian(point) {
    return Cesium.Cartesian3.fromDegrees(
      numberOr(point.longitudeDeg, 0),
      numberOr(point.latitudeDeg, 0),
      numberOr(point.altitudeM, 0)
    );
  }

  function cartesian(point) {
    return Cesium.Cartesian3.fromDegrees(
      numberOr(point.lonDeg, 0),
      numberOr(point.latDeg, 0),
      numberOr(point.altitudeM, 0)
    );
  }

  function orientationFor(vehicle, position) {
    const headingRad = Number.isFinite(Number(vehicle.yawRad))
      ? Number(vehicle.yawRad)
      : Cesium.Math.toRadians(numberOr(vehicle.headingDeg, 0));
    const pitchRad = numberOr(vehicle.pitchRad, 0);
    const rollRad = numberOr(vehicle.rollRad, 0);
    return Cesium.Transforms.headingPitchRollQuaternion(
      position,
      new Cesium.HeadingPitchRoll(headingRad, pitchRad, rollRad)
    );
  }

  function modelMatrixFor(vehicle, position, scale) {
    return Cesium.Matrix4.fromTranslationQuaternionRotationScale(
      position,
      orientationFor(vehicle, position),
      new Cesium.Cartesian3(scale, scale, scale),
      new Cesium.Matrix4()
    );
  }

  function resizeCanvas() {
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.floor(canvas.clientWidth * ratio));
    const height = Math.max(1, Math.floor(canvas.clientHeight * ratio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  }

  function project(latDeg, lonDeg, centerLat, centerLon, scale, width, height) {
    const metersPerDegLat = 111320.0;
    const metersPerDegLon = metersPerDegLat * Math.cos(centerLat * Math.PI / 180.0);
    const eastM = (lonDeg - centerLon) * metersPerDegLon;
    const northM = (latDeg - centerLat) * metersPerDegLat;
    return {
      x: width * 0.5 + eastM * scale,
      y: height * 0.55 - northM * scale,
    };
  }

  function drawRidges(width, height) {
    const sky = ctx.createLinearGradient(0, 0, 0, height * 0.62);
    sky.addColorStop(0, '#8fb1ce');
    sky.addColorStop(0.72, '#e4ece7');
    sky.addColorStop(1, '#d4dfd7');
    ctx.fillStyle = sky;
    ctx.fillRect(0, 0, width, height);

    function ridge(points, fill, stroke) {
      ctx.beginPath();
      ctx.moveTo(0, height);
      points.forEach((point) => ctx.lineTo(point[0] * width, point[1] * height));
      ctx.lineTo(width, height);
      ctx.closePath();
      ctx.fillStyle = fill;
      ctx.fill();
      ctx.strokeStyle = stroke;
      ctx.lineWidth = 2;
      ctx.stroke();
    }

    ridge([[0, 0.64], [0.12, 0.52], [0.24, 0.59], [0.38, 0.43], [0.55, 0.55], [0.72, 0.46], [1, 0.61]], '#879a83', '#61705f');
    ridge([[0, 0.79], [0.16, 0.69], [0.32, 0.73], [0.48, 0.62], [0.68, 0.70], [0.82, 0.59], [1, 0.73]], '#5f775f', '#415542');

    ctx.strokeStyle = 'rgba(215, 230, 208, 0.78)';
    ctx.lineWidth = 1;
    for (let y = 0.68; y < 0.97; y += 0.052) {
      ctx.beginPath();
      ctx.moveTo(width * 0.05, height * y);
      ctx.bezierCurveTo(width * 0.32, height * (y - 0.03), width * 0.61, height * (y + 0.04), width * 0.95, height * (y - 0.015));
      ctx.stroke();
    }
  }

  function drawFallbackMarker(point, headingDeg) {
    const headingRad = (numberOr(headingDeg, 0) - 90) * Math.PI / 180.0;
    ctx.save();
    ctx.translate(point.x, point.y);
    ctx.rotate(headingRad);
    ctx.beginPath();
    ctx.moveTo(24, 0);
    ctx.lineTo(-16, -13);
    ctx.lineTo(-8, 0);
    ctx.lineTo(-16, 13);
    ctx.closePath();
    ctx.fillStyle = '#1d6fd6';
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 3;
    ctx.fill();
    ctx.stroke();
    ctx.restore();
  }

  function renderFallback(errorText) {
    canvas.style.display = 'block';
    cesiumScene.style.display = 'none';
    resizeCanvas();
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    const vehicle = state.vehicle || {};
    const home = state.home || {};
    const centerLat = numberOr(vehicle.latDeg, numberOr(home.latDeg, 37.4275));
    const centerLon = numberOr(vehicle.lonDeg, numberOr(home.lonDeg, -122.1697));
    const scale = Math.max(0.16, Math.min(width, height) / 900.0);

    drawRidges(width, height);
    const trail = state.trail || [];
    for (let i = 1; i < trail.length; ++i) {
      const p0 = project(numberOr(trail[i - 1].latDeg, centerLat), numberOr(trail[i - 1].lonDeg, centerLon), centerLat, centerLon, scale, width, height);
      const p1 = project(numberOr(trail[i].latDeg, centerLat), numberOr(trail[i].lonDeg, centerLon), centerLat, centerLon, scale, width, height);
      const recency = i / Math.max(1, trail.length - 1);
      ctx.strokeStyle = `rgba(29, 111, 214, ${0.22 + recency * 0.66})`;
      ctx.lineWidth = 2 + recency * 2;
      ctx.beginPath();
      ctx.moveTo(p0.x, p0.y);
      ctx.lineTo(p1.x, p1.y);
      ctx.stroke();
    }
    if (home.valid) {
      const homePoint = project(numberOr(home.latDeg, centerLat), numberOr(home.lonDeg, centerLon), centerLat, centerLon, scale, width, height);
      ctx.strokeStyle = '#7a4b00';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(homePoint.x - 8, homePoint.y - 8);
      ctx.lineTo(homePoint.x + 8, homePoint.y + 8);
      ctx.moveTo(homePoint.x + 8, homePoint.y - 8);
      ctx.lineTo(homePoint.x - 8, homePoint.y + 8);
      ctx.stroke();
    }
    if (vehicle.positionValid) {
      drawFallbackMarker(project(numberOr(vehicle.latDeg, centerLat), numberOr(vehicle.lonDeg, centerLon), centerLat, centerLon, scale, width, height), vehicle.headingDeg);
    }

    const terrain = state.terrain || {};
    const terrainText = terrain.provider === 'quantized-mesh'
      ? 'quantized-mesh terrain unavailable in renderer'
      : 'heightmap fixture unavailable in renderer';
    statusEl.hidden = false;
    statusEl.textContent = errorText ? `${terrainText} | ${errorText}` : terrainText;
  }

  function isHealthySceneStatus(status, detail) {
    if (status === 'webengine-ready' || status === 'cesium-ready') {
      return !detail;
    }
    if (status === 'terrain-ready') {
      return !detail || detail === 'local heightmap fixture' ||
        detail === 'local quantized-mesh terrain';
    }
    if (status === 'tactical-ready') {
      return !detail || detail === 'vehicle-locked attitude view';
    }
    return false;
  }

  function setSceneStatus(status, error) {
    const suffix = error ? ` | ${error}` : '';
    statusEl.textContent = `${status}${suffix}`;
    statusEl.hidden = isHealthySceneStatus(status, error || '');
    console.info(`ANIMUS_SCENE_STATUS ${JSON.stringify({status, error: error || ''})}`);
  }

  function emitCameraMode() {
    console.info(`ANIMUS_CAMERA_MODE ${JSON.stringify({mode: cameraMode})}`);
  }

  function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, value));
  }

  function resetLockedCameraOffset(mode) {
    const offset = lockedCameraOffsets[mode];
    if (!offset) return;
    offset.headingDeg = offset.defaultHeadingDeg;
    offset.pitchDeg = mode === 'orbit' ? -26.0 : (mode === 'tactical' ? -15.0 : -12.0);
    offset.rangeM = mode === 'orbit' ? 520.0 : (mode === 'tactical' ? 170.0 : 260.0);
  }

  function resetTacticalCamera() {
    workspaceMode = 'tactical';
    cameraMode = 'tactical';
    resetLockedCameraOffset('tactical');
    applyManualCamera();
    if (viewer && viewer.scene) viewer.scene.requestRender();
    emitCameraMode();
    return true;
  }

  function resetFpvLook() {
    fpvLook.yawDeg = 0.0;
    fpvLook.pitchDeg = fpvDefaultPitchDeg;
    fpvLook.forwardDot = 1.0;
  }

  function resetFpvCamera() {
    workspaceMode = 'fpv';
    cameraMode = 'fpv';
    resetFpvLook();
    applyManualCamera();
    if (viewer && viewer.scene) viewer.scene.requestRender();
    emitCameraMode();
    return true;
  }

  function vehicleLockHeadingDeg() {
    const vehicle = state.vehicle || {};
    return vehicle.positionValid && validPosition(vehicle) ? vehicleHeadingDeg(vehicle) : 0.0;
  }

  function cameraHeadingPitchRange() {
    if (!viewer) return null;
    const camera = viewer.camera;
    const offset = Cesium.Cartesian3.subtract(
      camera.positionWC,
      cameraMode === 'free' && freeCamera.focus ? freeCamera.focus : (lastLockPosition || camera.positionWC),
      new Cesium.Cartesian3()
    );
    const range = Math.max(25.0, Cesium.Cartesian3.magnitude(offset));
    return {
      headingDeg: Cesium.Math.toDegrees(camera.heading),
      pitchDeg: Cesium.Math.toDegrees(camera.pitch),
      rangeM: range,
    };
  }

  function currentFocus() {
    if (workspaceMode !== 'tactical' && cameraMode === 'free' && freeCamera.focus) return freeCamera.focus;
    if (lastLockPosition) return lastLockPosition;
    const vehicle = state.vehicle || {};
    if (vehicle.positionValid && validPosition(vehicle)) return cartesian(vehicle);
    const home = state.home || {};
    if (home.valid && validPosition(home)) return cartesian(home);
    return null;
  }

  function setFreeFromCurrentPose(focus) {
    if (!viewer) return;
    const chosenFocus = focus || currentFocus();
    if (chosenFocus) {
      freeCamera.focus = Cesium.Cartesian3.clone(chosenFocus, freeCamera.focus || new Cesium.Cartesian3());
    }
    const pose = cameraHeadingPitchRange();
    if (pose) {
      freeCamera.headingDeg = pose.headingDeg;
      freeCamera.pitchDeg = clamp(pose.pitchDeg, -89.0, -2.0);
      freeCamera.rangeM = clamp(pose.rangeM, 20.0, 50000.0);
    }
  }

  function applyCameraToFocus(focus, headingDeg, pitchDeg, rangeM) {
    if (!viewer || !focus) return;
    viewer.camera.lookAt(
      focus,
      new Cesium.HeadingPitchRange(
        Cesium.Math.toRadians(headingDeg),
        Cesium.Math.toRadians(clamp(pitchDeg, -89.0, -2.0)),
        clamp(rangeM, 20.0, 50000.0)
      )
    );
    viewer.camera.lookAtTransform(Cesium.Matrix4.IDENTITY);
  }

  function attitudeAxes(vehicle, position) {
    const attitude = Cesium.Matrix3.fromQuaternion(
      orientationFor(vehicle, position),
      new Cesium.Matrix3()
    );
    return {
      forward: Cesium.Cartesian3.normalize(
        Cesium.Matrix3.multiplyByVector(attitude, Cesium.Cartesian3.UNIT_X, new Cesium.Cartesian3()),
        new Cesium.Cartesian3()
      ),
      right: Cesium.Cartesian3.normalize(
        Cesium.Matrix3.multiplyByVector(attitude, Cesium.Cartesian3.UNIT_Y, new Cesium.Cartesian3()),
        new Cesium.Cartesian3()
      ),
      up: Cesium.Cartesian3.normalize(
        Cesium.Matrix3.multiplyByVector(attitude, Cesium.Cartesian3.UNIT_Z, new Cesium.Cartesian3()),
        new Cesium.Cartesian3()
      ),
    };
  }

  function enuVector(position, eastM, northM, upM) {
    const transform = Cesium.Transforms.eastNorthUpToFixedFrame(position);
    return Cesium.Cartesian3.normalize(
      Cesium.Matrix4.multiplyByPointAsVector(
        transform,
        new Cesium.Cartesian3(eastM, northM, upM),
        new Cesium.Cartesian3()
      ),
      new Cesium.Cartesian3()
    );
  }

  function fpvAttitudeAxes(vehicle, position) {
    const headingRad = Cesium.Math.toRadians(vehicleHeadingDeg(vehicle));
    const pitchRad = numberOr(vehicle.pitchRad, 0);
    const rollRad = numberOr(vehicle.rollRad, 0);
    const forward = enuVector(
      position,
      Math.sin(headingRad) * Math.cos(pitchRad),
      Math.cos(headingRad) * Math.cos(pitchRad),
      Math.sin(pitchRad)
    );
    const rightLevel = enuVector(position, Math.cos(headingRad), -Math.sin(headingRad), 0.0);
    const upNoRoll = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.cross(rightLevel, forward, new Cesium.Cartesian3()),
      new Cesium.Cartesian3()
    );
    const right = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.add(
        Cesium.Cartesian3.multiplyByScalar(rightLevel, Math.cos(rollRad), new Cesium.Cartesian3()),
        Cesium.Cartesian3.multiplyByScalar(upNoRoll, Math.sin(rollRad), new Cesium.Cartesian3()),
        new Cesium.Cartesian3()
      ),
      new Cesium.Cartesian3()
    );
    const up = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.cross(right, forward, new Cesium.Cartesian3()),
      new Cesium.Cartesian3()
    );
    return {forward, right, up};
  }

  function fpvCameraPose(vehicle, position) {
    const axes = fpvAttitudeAxes(vehicle, position);
    const origin = Cesium.Cartesian3.add(
      position,
      Cesium.Cartesian3.add(
        Cesium.Cartesian3.multiplyByScalar(axes.forward, fpvNoseOffsetM.forward, new Cesium.Cartesian3()),
        Cesium.Cartesian3.add(
          Cesium.Cartesian3.multiplyByScalar(axes.right, fpvNoseOffsetM.right, new Cesium.Cartesian3()),
          Cesium.Cartesian3.multiplyByScalar(axes.up, fpvNoseOffsetM.up, new Cesium.Cartesian3()),
          new Cesium.Cartesian3()
        ),
        new Cesium.Cartesian3()
      ),
      new Cesium.Cartesian3()
    );
    const yawRad = Cesium.Math.toRadians(clamp(fpvLook.yawDeg, -89.9, 89.9));
    const pitchRad = Cesium.Math.toRadians(clamp(fpvLook.pitchDeg, -89.0, 89.0));
    const forwardScale = Math.cos(pitchRad) * Math.cos(yawRad);
    const rightScale = Math.cos(pitchRad) * Math.sin(yawRad);
    const upScale = Math.sin(pitchRad);
    const direction = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.add(
        Cesium.Cartesian3.multiplyByScalar(axes.forward, forwardScale, new Cesium.Cartesian3()),
        Cesium.Cartesian3.add(
          Cesium.Cartesian3.multiplyByScalar(axes.right, rightScale, new Cesium.Cartesian3()),
          Cesium.Cartesian3.multiplyByScalar(axes.up, upScale, new Cesium.Cartesian3()),
          new Cesium.Cartesian3()
        ),
        new Cesium.Cartesian3()
      ),
      new Cesium.Cartesian3()
    );
    fpvLook.forwardDot = Cesium.Cartesian3.dot(direction, axes.forward);
    const upProjection = Cesium.Cartesian3.multiplyByScalar(
      direction,
      Cesium.Cartesian3.dot(axes.up, direction),
      new Cesium.Cartesian3()
    );
    let cameraUp = Cesium.Cartesian3.subtract(axes.up, upProjection, new Cesium.Cartesian3());
    if (Cesium.Cartesian3.magnitude(cameraUp) < 1.0e-6) {
      cameraUp = Cesium.Cartesian3.cross(axes.right, direction, new Cesium.Cartesian3());
    }
    cameraUp = Cesium.Cartesian3.normalize(cameraUp, cameraUp);
    return {origin, direction, right: axes.right, up: cameraUp, forwardDot: fpvLook.forwardDot};
  }

  function applyFpvCamera() {
    if (!viewer) return false;
    const vehicle = state.vehicle || {};
    if (!vehicle.positionValid || !validPosition(vehicle)) return false;
    const pose = fpvCameraPose(vehicle, cartesian(vehicle));
    if (viewer.camera.frustum) {
      const aspectRatio = Math.max(0.1, numberOr(viewer.camera.frustum.aspectRatio, 1.0));
      const verticalFovRad = Cesium.Math.toRadians(fpvVerticalFovDeg);
      viewer.camera.frustum.fov = aspectRatio > 1.0
        ? 2.0 * Math.atan(Math.tan(verticalFovRad * 0.5) * aspectRatio)
        : verticalFovRad;
    }
    viewer.camera.setView({
      destination: pose.origin,
      orientation: {
        direction: pose.direction,
        up: pose.up,
      },
    });
    viewer.camera.lookAtTransform(Cesium.Matrix4.IDENTITY);
    return true;
  }

  function applyManualCamera() {
    if (!viewer) return;
    if (workspaceMode === 'fpv' || cameraMode === 'fpv') {
      applyFpvCamera();
      return;
    }
    if (workspaceMode !== 'tactical' && cameraMode === 'free') {
      if (!freeCamera.focus) {
        const focus = currentFocus();
        if (focus) freeCamera.focus = Cesium.Cartesian3.clone(focus, new Cesium.Cartesian3());
      }
      applyCameraToFocus(freeCamera.focus, freeCamera.headingDeg, freeCamera.pitchDeg, freeCamera.rangeM);
      return;
    }
    const focus = currentFocus();
    const offset = lockedCameraOffsets[cameraMode];
    if (!focus || !offset) return;
    const headingDeg = offset.followsVehicleHeading
      ? vehicleLockHeadingDeg() + offset.headingDeg
      : offset.headingDeg;
    applyCameraToFocus(focus, headingDeg, offset.pitchDeg, offset.rangeM);
  }

  function panFreeCamera(deltaX, deltaY) {
    if (!viewer || !freeCamera.focus) return;
    const range = Math.max(25.0, freeCamera.rangeM);
    const height = Math.max(1, viewer.scene.canvas.clientHeight || viewer.scene.canvas.height || 1);
    const metersPerPixel = range / height * 1.2;
    const right = Cesium.Cartesian3.normalize(viewer.camera.rightWC, new Cesium.Cartesian3());
    const up = Cesium.Cartesian3.normalize(viewer.camera.upWC, new Cesium.Cartesian3());
    const move = Cesium.Cartesian3.add(
      Cesium.Cartesian3.multiplyByScalar(right, -deltaX * metersPerPixel, new Cesium.Cartesian3()),
      Cesium.Cartesian3.multiplyByScalar(up, deltaY * metersPerPixel, new Cesium.Cartesian3()),
      new Cesium.Cartesian3()
    );
    Cesium.Cartesian3.add(freeCamera.focus, move, freeCamera.focus);
    applyManualCamera();
  }

  function rotateCurrentCamera(deltaX, deltaY) {
    const headingDelta = deltaX * 0.35;
    const pitchDelta = -deltaY * 0.25;
    if (workspaceMode === 'fpv' || cameraMode === 'fpv') {
      fpvLook.yawDeg = clamp(fpvLook.yawDeg + headingDelta, -89.9, 89.9);
      fpvLook.pitchDeg = clamp(fpvLook.pitchDeg + pitchDelta, -89.0, 89.0);
    } else if (workspaceMode !== 'tactical' && cameraMode === 'free') {
      freeCamera.headingDeg += headingDelta;
      freeCamera.pitchDeg = clamp(freeCamera.pitchDeg + pitchDelta, -89.0, -2.0);
    } else {
      const offset = lockedCameraOffsets[cameraMode];
      if (!offset) return;
      offset.headingDeg += headingDelta;
      offset.pitchDeg = clamp(offset.pitchDeg + pitchDelta, -89.0, -2.0);
    }
    applyManualCamera();
  }

  function zoomCurrentCamera(wheelDeltaY) {
    if (workspaceMode === 'fpv' || cameraMode === 'fpv') {
      applyManualCamera();
      return;
    }
    const factor = Math.exp(clamp(wheelDeltaY, -600.0, 600.0) * 0.0015);
    if (workspaceMode !== 'tactical' && cameraMode === 'free') {
      if (!freeCamera.focus) setFreeFromCurrentPose(currentFocus());
      freeCamera.rangeM = clamp(freeCamera.rangeM * factor, 20.0, 50000.0);
    } else {
      const offset = lockedCameraOffsets[cameraMode];
      if (!offset) return;
      offset.rangeM = clamp(offset.rangeM * factor, 20.0, 50000.0);
    }
    applyManualCamera();
  }

  function switchToFreeFromPan() {
    if (workspaceMode === 'fpv') return;
    if (workspaceMode === 'tactical') return;
    if (cameraMode === 'free') return;
    setFreeFromCurrentPose(currentFocus());
    cameraMode = 'free';
    emitCameraMode();
  }

  function installCameraControls() {
    if (!viewer || !viewer.scene || !viewer.scene.canvas) return;
    const controller = viewer.scene.screenSpaceCameraController;
    controller.enableRotate = false;
    controller.enableTranslate = false;
    controller.enableZoom = false;
    controller.enableTilt = false;
    controller.enableLook = false;

    const target = viewer.scene.canvas;
    target.tabIndex = 0;
    target.addEventListener('contextmenu', function (event) {
      event.preventDefault();
    });

    function classifyCameraDrag(event) {
      const buttons = Number.isFinite(Number(event.buttons)) ? Number(event.buttons) : 0;
      const middleDown = event.button === 1 || (buttons & 4) !== 0;
      const pressLikeEvent = event.type === 'pointerdown' || event.type === 'mousedown';
      const implicitTrackpadPress = pressLikeEvent && event.button !== 2 && buttons === 0;
      const primaryDown = event.button === 0 || (buttons & 1) !== 0 || implicitTrackpadPress;
      if (workspaceMode === 'fpv') return primaryDown || middleDown ? 'rotate' : null;
      if (workspaceMode === 'tactical') return primaryDown || middleDown ? 'rotate' : null;
      if (middleDown) return 'rotate';
      if (primaryDown) return spaceDown ? 'rotate' : 'pan';
      return null;
    }

    function startCameraDrag(event, source, id) {
      const action = classifyCameraDrag(event);
      if (!action) return false;
      target.focus();
      cameraDrag = {
        id,
        source,
        x: event.clientX,
        y: event.clientY,
        action,
      };
      return true;
    }

    function continueActiveCameraDrag(event) {
      if (!cameraDrag) return false;
      const deltaX = event.clientX - cameraDrag.x;
      const deltaY = event.clientY - cameraDrag.y;
      cameraDrag.x = event.clientX;
      cameraDrag.y = event.clientY;
      if (cameraDrag.action === 'rotate') {
        rotateCurrentCamera(deltaX, deltaY);
      } else {
        switchToFreeFromPan();
        panFreeCamera(deltaX, deltaY);
      }
      viewer.scene.requestRender();
      return true;
    }

    function continueCameraDrag(event, source, id) {
      if (!cameraDrag || cameraDrag.source !== source || cameraDrag.id !== id) return false;
      return continueActiveCameraDrag(event);
    }

    function endCameraDrag(source, id) {
      if (!cameraDrag || cameraDrag.source !== source || cameraDrag.id !== id) return false;
      cameraDrag = null;
      return true;
    }

    function endActiveCameraDrag() {
      if (!cameraDrag) return false;
      cameraDrag = null;
      return true;
    }

    target.addEventListener('pointerdown', function (event) {
      if (!startCameraDrag(event, 'pointer', event.pointerId)) return;
      activePointerInteraction = true;
      if (target.setPointerCapture) {
        target.setPointerCapture(event.pointerId);
      }
      event.preventDefault();
    });
    target.addEventListener('pointermove', function (event) {
      if (!continueCameraDrag(event, 'pointer', event.pointerId)) return;
      event.preventDefault();
    });
    target.addEventListener('pointerup', function (event) {
      if (endCameraDrag('pointer', event.pointerId)) {
        activePointerInteraction = false;
        if (target.hasPointerCapture && target.hasPointerCapture(event.pointerId)) {
          target.releasePointerCapture(event.pointerId);
        }
      }
      event.preventDefault();
    });
    target.addEventListener('pointercancel', function (event) {
      if (endCameraDrag('pointer', event.pointerId)) {
        activePointerInteraction = false;
        if (target.hasPointerCapture && target.hasPointerCapture(event.pointerId)) {
          target.releasePointerCapture(event.pointerId);
        }
      }
    });
    target.addEventListener('mousedown', function (event) {
      if (activePointerInteraction) return;
      if (!startCameraDrag(event, 'mouse', 'mouse')) return;
      event.preventDefault();
    });
    target.addEventListener('mousemove', function (event) {
      if (!cameraDrag) return;
      if (cameraDrag.source !== 'mouse' && cameraDrag.source !== 'pointer') return;
      if (!continueActiveCameraDrag(event)) return;
      event.preventDefault();
    });
    target.addEventListener('mouseup', function (event) {
      if (endActiveCameraDrag()) {
        activePointerInteraction = false;
        event.preventDefault();
      }
    });
    target.addEventListener('mouseleave', function () {
      endCameraDrag('mouse', 'mouse');
    });
    window.addEventListener('pointerup', function (event) {
      if (endCameraDrag('pointer', event.pointerId)) {
        activePointerInteraction = false;
        if (target.hasPointerCapture && target.hasPointerCapture(event.pointerId)) {
          target.releasePointerCapture(event.pointerId);
        }
      }
    });
    window.addEventListener('mouseup', function () {
      if (endActiveCameraDrag()) {
        activePointerInteraction = false;
      }
    });
    window.addEventListener('blur', function () {
      if (cameraDrag && cameraDrag.source === 'pointer') {
        activePointerInteraction = false;
      }
      cameraDrag = null;
    });
    target.addEventListener('wheel', function (event) {
      if (spaceDown) {
        rotateCurrentCamera(event.deltaX, event.deltaY);
      } else {
        zoomCurrentCamera(event.deltaY);
      }
      viewer.scene.requestRender();
      event.preventDefault();
    }, {passive: false});
    window.addEventListener('keydown', function (event) {
      if (event.code === 'Space') {
        spaceDown = true;
        event.preventDefault();
      }
    });
    window.addEventListener('keyup', function (event) {
      if (event.code === 'Space') {
        spaceDown = false;
        event.preventDefault();
      }
    });
  }

  function fixtureRectangle(fixture) {
    return Cesium.Rectangle.fromDegrees(
      numberOr(fixture.westDeg, -122.2607248),
      numberOr(fixture.southDeg, 37.3552151),
      numberOr(fixture.eastDeg, -122.0786752),
      numberOr(fixture.northDeg, 37.4997849)
    );
  }

  function fixtureHeight(fixture, u, v) {
    const minHeight = numberOr(fixture.minHeightM, 2.0);
    const maxHeight = numberOr(fixture.maxHeightM, 58.0);
    const ridge = 0.46 +
      0.24 * Math.sin(u * Math.PI * 3.2 + v * Math.PI * 1.1) +
      0.18 * Math.cos((u - v) * Math.PI * 4.6) +
      0.12 * Math.sin(v * Math.PI * 7.0);
    const bowl = 0.20 * Math.exp(-((u - 0.55) ** 2 + (v - 0.45) ** 2) / 0.035);
    const value = Math.max(0.0, Math.min(1.0, ridge + bowl));
    return minHeight + value * (maxHeight - minHeight);
  }

  function buildFixtureTerrainProvider(fixture) {
    const width = Math.max(2, Math.floor(numberOr(fixture.width, 33)));
    const height = Math.max(2, Math.floor(numberOr(fixture.height, 33)));
    const rectangle = fixtureRectangle(fixture);
    const tilingScheme = new Cesium.GeographicTilingScheme({
      rectangle,
      numberOfLevelZeroTilesX: 1,
      numberOfLevelZeroTilesY: 1,
    });
    return new Cesium.CustomHeightmapTerrainProvider({
      width,
      height,
      tilingScheme,
      callback: function () {
        const data = new Float32Array(width * height);
        for (let y = 0; y < height; ++y) {
          for (let x = 0; x < width; ++x) {
            const u = x / Math.max(1, width - 1);
            const v = y / Math.max(1, height - 1);
            data[y * width + x] = fixtureHeight(fixture, u, v);
          }
        }
        return data;
      },
    });
  }

  function updateImageryProvider(fixture) {
    if (!viewer || !fixture || !fixture.imageryUrlTemplate) return;
    if (imageryLayer && imageryLayer.animusUrlTemplate === fixture.imageryUrlTemplate) return;
    if (imageryLayer) {
      viewer.imageryLayers.remove(imageryLayer, true);
      imageryLayer = null;
    }
    const provider = new Cesium.UrlTemplateImageryProvider({
      url: fixture.imageryUrlTemplate,
      rectangle: fixtureRectangle(fixture),
      minimumLevel: Math.floor(numberOr(fixture.imageryMinimumLevel, 0)),
      maximumLevel: Math.floor(numberOr(fixture.imageryMaximumLevel, 0)),
      tilingScheme: new Cesium.GeographicTilingScheme({
        rectangle: fixtureRectangle(fixture),
        numberOfLevelZeroTilesX: 1,
        numberOfLevelZeroTilesY: 1,
      }),
      credit: 'Animus offline fixture',
    });
    imageryLayer = viewer.imageryLayers.addImageryProvider(provider);
    imageryLayer.animusUrlTemplate = fixture.imageryUrlTemplate;
  }

  function applyWorkspaceSceneStyle() {
    if (!viewer || !viewer.scene || !viewer.scene.globe) return;
    const tactical = workspaceMode === 'tactical';
    viewer.scene.backgroundColor = Cesium.Color.fromCssColorString(tactical ? '#050b0f' : '#8fb1ce');
    viewer.scene.globe.baseColor = Cesium.Color.fromCssColorString(tactical ? '#050b0f' : '#d4dfd7');
    viewer.scene.globe.show = !tactical;
    if (headingEntity) headingEntity.show = false;
  }

  function ensureViewer() {
    if (viewer) return true;
    if (!window.Cesium) {
      setSceneStatus('cesium-unavailable', 'Bundled CesiumJS assets are unavailable');
      renderFallback('Bundled CesiumJS assets are unavailable');
      return false;
    }

    try {
      canvas.style.display = 'none';
      cesiumScene.style.display = 'block';
      viewer = new Cesium.Viewer('cesiumScene', {
        animation: false,
        baseLayer: false,
        baseLayerPicker: false,
        fullscreenButton: false,
        geocoder: false,
        homeButton: false,
        infoBox: false,
        navigationHelpButton: false,
        sceneModePicker: false,
        selectionIndicator: false,
        timeline: false,
        terrainProvider: new Cesium.EllipsoidTerrainProvider(),
        vrButton: false,
        requestRenderMode: false,
        contextOptions: {
          webgl: {
            preserveDrawingBuffer: true,
          },
        },
      });
      viewer.scene.backgroundColor = Cesium.Color.fromCssColorString('#8fb1ce');
      viewer.scene.globe.baseColor = Cesium.Color.fromCssColorString('#d4dfd7');
      viewer.scene.globe.enableLighting = false;
      viewer.scene.skyAtmosphere.show = false;
      viewer.scene.fog.enabled = false;
      viewer.scene.sun.show = false;
      viewer.scene.moon.show = false;
      installCameraControls();
      vehicleEntity = viewer.entities.add({
        id: 'vehicle',
        name: 'Altair vehicle',
      });
      headingEntity = viewer.entities.add({
        id: 'vehicle-heading',
        show: false,
        polyline: {
          positions: [],
          width: 1,
          material: Cesium.Color.fromCssColorString('#f0c84b').withAlpha(0.0),
        },
      });
      homeEntity = viewer.entities.add({
        id: 'home',
        name: 'Home',
        point: {
          pixelSize: 12,
          color: Cesium.Color.fromCssColorString('#d88c00'),
          outlineColor: Cesium.Color.WHITE,
          outlineWidth: 2,
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
        label: {
          text: 'HOME',
          font: '12px sans-serif',
          pixelOffset: new Cesium.Cartesian2(0, -22),
          fillColor: Cesium.Color.fromCssColorString('#2d2514'),
          showBackground: true,
          backgroundColor: Cesium.Color.fromCssColorString('#f7f7f3').withAlpha(0.84),
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
        show: false,
      });
      const pointPrimitives = viewer.scene.primitives.add(new Cesium.PointPrimitiveCollection());
      vehiclePointPrimitive = pointPrimitives.add({
        show: false,
        pixelSize: 12,
        color: Cesium.Color.fromCssColorString('#1d6fd6'),
        outlineColor: Cesium.Color.WHITE,
        outlineWidth: 2,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
      });
      homePointPrimitive = pointPrimitives.add({
        show: false,
        pixelSize: 16,
        color: Cesium.Color.fromCssColorString('#d88c00'),
        outlineColor: Cesium.Color.WHITE,
        outlineWidth: 3,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
      });
      aircraftOutlineCollection = viewer.scene.primitives.add(new Cesium.PolylineCollection());
      attitudeReferenceCollection = viewer.scene.primitives.add(new Cesium.PolylineCollection());
      terrainReferenceCollection = viewer.scene.primitives.add(new Cesium.PolylineCollection());
      trailPolylineCollection = viewer.scene.primitives.add(new Cesium.PolylineCollection());
      setSceneStatus('cesium-ready', '');
      return true;
    } catch (error) {
      const message = error && error.message ? error.message : String(error);
      setSceneStatus('cesium-error', message);
      renderFallback(message);
      return false;
    }
  }

  async function updateTerrainProvider() {
    if (!viewer) return;
    if (workspaceMode === 'tactical') {
      terrainProviderKey = 'tactical';
      viewer.terrainProvider = new Cesium.EllipsoidTerrainProvider();
      if (imageryLayer) {
        viewer.imageryLayers.remove(imageryLayer, true);
        imageryLayer = null;
      }
      viewer.scene.globe.show = false;
      setSceneStatus('tactical-ready', 'vehicle-locked attitude view');
      viewer.scene.requestRender();
      return;
    }
    viewer.scene.globe.show = true;
    const terrain = state.terrain || {};
    const fixture = terrain.fixture || {};
    const provider = terrain.provider || 'heightmap-fixture';
    const key = provider === 'quantized-mesh'
      ? `mesh:${terrain.cacheUrl || ''}`
      : `fixture:${fixture.name || 'default'}:${fixture.width || ''}x${fixture.height || ''}`;
    if (terrainProviderKey === key) {
      updateImageryProvider(fixture);
      return;
    }
    terrainProviderKey = key;

    if (provider !== 'quantized-mesh' || !terrain.cacheUrl) {
      viewer.terrainProvider = buildFixtureTerrainProvider(fixture);
      updateImageryProvider(fixture);
      setSceneStatus('terrain-ready', 'local heightmap fixture');
      viewer.scene.requestRender();
      return;
    }

    setSceneStatus('terrain-loading', terrain.cachePath || terrain.cacheUrl);
    updateImageryProvider(fixture);
    try {
      viewer.terrainProvider = await Cesium.CesiumTerrainProvider.fromUrl(terrain.cacheUrl, {
        requestVertexNormals: true,
        requestWaterMask: false,
      });
      setSceneStatus('terrain-ready', 'local quantized-mesh terrain');
    } catch (error) {
      const message = error && error.message ? error.message : String(error);
      viewer.terrainProvider = new Cesium.EllipsoidTerrainProvider();
      setSceneStatus('terrain-error', message);
    }
    viewer.scene.requestRender();
  }

  function removeAircraftModelPrimitive() {
    if (!aircraftModelPrimitive || !viewer || !viewer.scene) return;
    viewer.scene.primitives.remove(aircraftModelPrimitive);
    aircraftModelPrimitive = null;
    const controller = ensureVehicleModelController();
    if (controller) controller.setModel(null);
  }

  function currentVehicleModelMatrix(scale) {
    const vehicle = state.vehicle || {};
    if (!vehicle.positionValid || !validPosition(vehicle)) return Cesium.Matrix4.IDENTITY;
    return modelMatrixFor(vehicle, cartesian(vehicle), scale);
  }

  async function loadAircraftModelPrimitive(modelUri, scale, fallbackUri) {
    const token = ++aircraftModelLoadToken;
    removeAircraftModelPrimitive();
    if (!modelUri) {
      aircraftModelUri = '';
      return;
    }
    if (!Cesium.Model || typeof Cesium.Model.fromGltfAsync !== 'function') {
      const controller = ensureVehicleModelController();
      if (controller) {
        controller.warnOnce('model-from-gltf-missing', 'Cesium Model.fromGltfAsync is not available');
      }
      aircraftModelUri = '';
      return;
    }

    try {
      const primitive = await Cesium.Model.fromGltfAsync({
        url: modelUri,
        modelMatrix: currentVehicleModelMatrix(scale),
        minimumPixelSize: 320,
        maximumScale: 1200,
        upAxis: Cesium.Axis.Z,
        forwardAxis: Cesium.Axis.X,
        silhouetteColor: Cesium.Color.WHITE,
        silhouetteSize: 1.0,
      });
      if (token !== aircraftModelLoadToken) return;
      aircraftModelPrimitive = viewer.scene.primitives.add(primitive);
      aircraftModelUri = modelUri;
      aircraftModelScale = scale;
      const controller = ensureVehicleModelController();
      if (controller) controller.setModel(aircraftModelPrimitive);
      if (vehiclePointPrimitive) vehiclePointPrimitive.show = false;
      viewer.scene.requestRender();
    } catch (error) {
      if (token !== aircraftModelLoadToken) return;
      const controller = ensureVehicleModelController();
      if (controller) {
        controller.warnOnce(
          `aircraft-load:${modelUri}`,
          `failed to load aircraft model ${modelUri}: ${error && error.message ? error.message : String(error)}`
        );
      }
      if (fallbackUri && fallbackUri !== modelUri) {
        loadAircraftModelPrimitive(fallbackUri, 3.0, '');
      } else {
        aircraftModelUri = '';
      }
    }
  }

  function updateAircraftModel(fixture, model) {
    if (!viewer || !vehicleEntity) return;
    const fallbackUri = fixture && fixture.aircraftModelUrl ? String(fixture.aircraftModelUrl) : '';
    const profileAsset = vehicleModelProfile && vehicleModelProfile.asset
      ? String(vehicleModelProfile.asset)
      : '';
    const snapshotAsset = model && model.asset ? String(model.asset) : '';
    const modelUri = profileAsset || snapshotAsset || fallbackUri;
    const scale = vehicleModelProfile && Number.isFinite(Number(vehicleModelProfile.scale))
      ? Number(vehicleModelProfile.scale) * 3.0
      : 3.0;
    if (aircraftModelRequestedUri === modelUri) return;
    aircraftModelRequestedUri = modelUri;
    aircraftModelUri = modelUri;
    vehicleEntity.model = undefined;
    loadAircraftModelPrimitive(modelUri, scale, fallbackUri);
    if (vehiclePointPrimitive) {
      vehiclePointPrimitive.show = false;
    }
  }

  function ensureVehicleModelController() {
    if (vehicleModelController || !window.VehicleModelController) return vehicleModelController;
    vehicleModelController = new window.VehicleModelController(Cesium);
    return vehicleModelController;
  }

  function snapshotVehicleModelProfile(model) {
    const surfaces = Array.isArray(state.controlSurfaces)
      ? state.controlSurfaces
          .filter((surface) => surface && surface.id && surface.node)
          .map((surface) => ({
            id: String(surface.id),
            node: String(surface.node),
            axis: Array.isArray(surface.axis) ? surface.axis : [1.0, 0.0, 0.0],
          }))
      : [];
    if (!surfaces.length) return null;
    return {
      id: model && model.profile ? String(model.profile) : 'snapshot',
      asset: model && model.asset ? String(model.asset) : '',
      surfaces,
    };
  }

  function loadVehicleModelProfile(model) {
    const controller = ensureVehicleModelController();
    if (!controller) return;
    const profile = model && model.profile ? String(model.profile) : '';
    const asset = model && model.asset ? String(model.asset) : '';
    const key = `${profile}:${asset}`;
    if (vehicleModelProfileKey === key) return;
    vehicleModelProfileKey = key;
    if (!profile) {
      vehicleModelProfile = null;
      controller.setProfile(null);
      updateAircraftModel((state.terrain || {}).fixture || {}, state.model || {});
      return;
    }
    const snapshotProfile = snapshotVehicleModelProfile(model);
    if (snapshotProfile) {
      controller.setProfile(snapshotProfile);
    }
    fetch(`models/${profile}.json`)
      .then((response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json();
      })
      .then((profileData) => {
        vehicleModelProfile = profileData;
        controller.setProfile(profileData);
        updateAircraftModel((state.terrain || {}).fixture || {}, state.model || {});
      })
      .catch((error) => {
        controller.warnOnce(
          `profile-load:${profile}`,
          `failed to load profile models/${profile}.json: ${error && error.message ? error.message : String(error)}`
        );
        updateAircraftModel((state.terrain || {}).fixture || {}, state.model || {});
      });
  }

  function applyControlSurfaces(controlSurfaces) {
    const controller = ensureVehicleModelController();
    if (!controller) return;
    controller.setModel(aircraftModelPrimitive);
    controller.applyControlSurfaces(controlSurfaces);
    if (viewer && viewer.scene) viewer.scene.requestRender();
  }

  function vehicleHeadingDeg(vehicle) {
    return numberOr(vehicle.headingDeg, Cesium.Math.toDegrees(numberOr(vehicle.yawRad, 0)));
  }

  function applyCamera(vehicle, position, capturePose) {
    if (!viewer || !validPosition(vehicle)) return;
    lastLockPosition = Cesium.Cartesian3.clone(position, lastLockPosition || new Cesium.Cartesian3());
    const headingDeg = vehicleHeadingDeg(vehicle);
    if (capturePose) {
      viewer.camera.lookAt(
        position,
        new Cesium.HeadingPitchRange(
          Cesium.Math.toRadians(headingDeg + 85.0),
          Cesium.Math.toRadians(-52.0),
          500.0
        )
      );
      viewer.camera.lookAtTransform(Cesium.Matrix4.IDENTITY);
      return;
    }

    applyManualCamera();
  }

  function vehicleOffsetPoint(position, headingRad, forwardM, rightM, upM) {
    const eastM = Math.sin(headingRad) * forwardM + Math.cos(headingRad) * rightM;
    const northM = Math.cos(headingRad) * forwardM - Math.sin(headingRad) * rightM;
    const transform = Cesium.Transforms.eastNorthUpToFixedFrame(position);
    return Cesium.Matrix4.multiplyByPoint(
      transform,
      new Cesium.Cartesian3(eastM, northM, upM),
      new Cesium.Cartesian3()
    );
  }

  function updateAircraftOutline(vehicle, position) {
    if (!aircraftOutlineCollection) return;
    aircraftOutlineCollection.removeAll();
    if (workspaceMode === 'fpv') return;
    if (!vehicle.positionValid || !validPosition(vehicle)) return;
    if (aircraftModelPrimitive) return;

    const headingRad = Cesium.Math.toRadians(vehicleHeadingDeg(vehicle));
    const rollRad = numberOr(vehicle.rollRad, 0);
    const pitchRad = numberOr(vehicle.pitchRad, 0);
    const wingLiftM = Math.sin(rollRad) * 26.0;
    const noseLiftM = Math.sin(pitchRad) * 18.0;
    const blue = Cesium.Color.fromCssColorString('#1d6fd6').withAlpha(0.96);
    const white = Cesium.Color.WHITE.withAlpha(0.86);

    const nose = vehicleOffsetPoint(position, headingRad, 62.0, 0.0, 4.0 + noseLiftM);
    const tail = vehicleOffsetPoint(position, headingRad, -48.0, 0.0, 2.0 - noseLiftM);
    const leftWing = vehicleOffsetPoint(position, headingRad, -6.0, -76.0, 3.0 - wingLiftM);
    const rightWing = vehicleOffsetPoint(position, headingRad, -6.0, 76.0, 3.0 + wingLiftM);
    const leftTail = vehicleOffsetPoint(position, headingRad, -42.0, -28.0, 2.0 - wingLiftM * 0.45);
    const rightTail = vehicleOffsetPoint(position, headingRad, -42.0, 28.0, 2.0 + wingLiftM * 0.45);
    const fin = vehicleOffsetPoint(position, headingRad, -48.0, 0.0, 34.0);

    [
      [tail, nose, 9.0, blue],
      [leftWing, position, rightWing, 11.0, blue],
      [leftTail, tail, rightTail, 7.0, blue],
      [tail, fin, 5.0, white],
    ].forEach((segment) => {
      const material = Cesium.Material.fromType('Color', {color: segment[segment.length - 1]});
      aircraftOutlineCollection.add({
        positions: segment.slice(0, -2),
        width: segment[segment.length - 2],
        material,
      });
    });
  }

  function updateVehicle() {
    if (!viewer || !vehicleEntity || !headingEntity) return;
    const vehicle = state.vehicle || {};
    if (!vehicle.positionValid || !validPosition(vehicle)) {
      vehicleEntity.show = false;
      headingEntity.show = false;
      if (aircraftModelPrimitive) aircraftModelPrimitive.show = false;
      if (aircraftOutlineCollection) aircraftOutlineCollection.removeAll();
      if (vehiclePointPrimitive) vehiclePointPrimitive.show = false;
      viewer.scene.requestRender();
      return;
    }

    const position = cartesian(vehicle);
    const ownshipVisible = workspaceMode !== 'fpv';
    vehicleEntity.show = ownshipVisible;
    vehicleEntity.position = position;
    vehicleEntity.orientation = orientationFor(vehicle, position);
    if (aircraftModelPrimitive) {
      aircraftModelPrimitive.modelMatrix = modelMatrixFor(vehicle, position, aircraftModelScale);
      aircraftModelPrimitive.show = ownshipVisible;
    }
    updateAircraftOutline(vehicle, position);
    if (vehiclePointPrimitive) {
      vehiclePointPrimitive.show = ownshipVisible && !aircraftModelPrimitive;
      vehiclePointPrimitive.position = position;
    }

    const headingDeg = vehicleHeadingDeg(vehicle);
    const rangeM = Math.max(25.0, numberOr(vehicle.groundspeedMps, 0) * 2.0);
    const start = Cesium.Cartographic.fromDegrees(numberOr(vehicle.lonDeg, 0), numberOr(vehicle.latDeg, 0), numberOr(vehicle.altitudeM, 0));
    const geodesic = new Cesium.EllipsoidGeodesic();
    const endLat = numberOr(vehicle.latDeg, 0) + Math.cos(Cesium.Math.toRadians(headingDeg)) * rangeM / 111320.0;
    const endLon = numberOr(vehicle.lonDeg, 0) + Math.sin(Cesium.Math.toRadians(headingDeg)) * rangeM / (111320.0 * Math.cos(Cesium.Math.toRadians(numberOr(vehicle.latDeg, 0))));
    const end = Cesium.Cartographic.fromDegrees(endLon, endLat, numberOr(vehicle.altitudeM, 0));
    geodesic.setEndPoints(start, end);
    headingEntity.show = false;
    headingEntity.polyline.positions = [
      position,
      Cesium.Cartesian3.fromRadians(end.longitude, end.latitude, end.height),
    ];

    cameraInitialized = true;
    applyCamera(vehicle, position, false);
    viewer.scene.requestRender();
  }

  function updateHome() {
    if (!viewer || !homeEntity) return;
    if (workspaceMode === 'tactical' || workspaceMode === 'fpv') {
      homeEntity.show = false;
      if (homePointPrimitive) homePointPrimitive.show = false;
      viewer.scene.requestRender();
      return;
    }
    const home = state.home || {};
    if (!home.valid || !validPosition(home)) {
      homeEntity.show = false;
      if (homePointPrimitive) homePointPrimitive.show = false;
      viewer.scene.requestRender();
      return;
    }
    homeEntity.show = true;
    homeEntity.position = cartesian(home);
    if (homePointPrimitive) {
      homePointPrimitive.show = true;
      homePointPrimitive.position = cartesian(home);
    }
    viewer.scene.requestRender();
  }

  function updateTrail() {
    if (!viewer) return;
    trailEntities.forEach((entity) => viewer.entities.remove(entity));
    trailEntities = [];
    if (trailPolylineCollection) {
      trailPolylineCollection.removeAll();
    }
    if (workspaceMode === 'tactical' || workspaceMode === 'fpv') {
      viewer.scene.requestRender();
      return;
    }

    const trail = (state.trail || []).filter(validPosition);
    for (let i = 1; i < trail.length; ++i) {
      const recency = i / Math.max(1, trail.length - 1);
      if (trailPolylineCollection) {
        trailPolylineCollection.add({
          positions: [cartesian(trail[i - 1]), cartesian(trail[i])],
          width: 9.0 + recency * 10.0,
          material: Cesium.Material.fromType('Color', {
            color: Cesium.Color.fromCssColorString('#f0c84b').withAlpha(0.68 + recency * 0.32),
          }),
        });
      }
      trailEntities.push(viewer.entities.add({
        name: 'Trail segment',
        polyline: {
          positions: [cartesian(trail[i - 1]), cartesian(trail[i])],
          width: 5.0 + recency * 6.0,
          material: Cesium.Color.fromCssColorString('#fff2a6').withAlpha(0.48 + recency * 0.50),
          depthFailMaterial: Cesium.Color.fromCssColorString('#fff2a6').withAlpha(0.58 + recency * 0.40),
          clampToGround: false,
        },
      }));
    }
    viewer.scene.requestRender();
  }

  function clearOverlayEntities() {
    if (!viewer) return;
    overlayEntities.forEach((entity) => viewer.entities.remove(entity));
    overlayEntities = [];
  }

  function eventSeverityColor(severity) {
    if (severity === 'warning') return '#d92626';
    if (severity === 'caution') return '#d59b28';
    return '#3f4a3d';
  }

  function updateNavigationOverlays() {
    if (!viewer) return;
    clearOverlayEntities();
    if (workspaceMode === 'tactical' || workspaceMode === 'fpv') {
      viewer.scene.requestRender();
      return;
    }

    const overlays = state.overlays || {};
    const missionItems = Array.isArray(overlays.missionItems)
      ? overlays.missionItems.filter(validOverlayPosition)
      : [];
    if (missionItems.length >= 2) {
      overlayEntities.push(viewer.entities.add({
        id: 'overlay-mission-route',
        name: 'Mission route',
        polyline: {
          positions: missionItems.map(overlayCartesian),
          width: 4,
          material: Cesium.Color.fromCssColorString('#1d6fd6').withAlpha(0.86),
          depthFailMaterial: Cesium.Color.fromCssColorString('#1d6fd6').withAlpha(0.42),
        },
      }));
    }
    missionItems.forEach((item) => {
      const active = Number(item.sequence) === Number(overlays.activeMissionSeq);
      overlayEntities.push(viewer.entities.add({
        name: `Mission ${item.sequence}`,
        position: overlayCartesian(item),
        point: {
          pixelSize: active ? 16 : 13,
          color: Cesium.Color.fromCssColorString(active ? '#1d6fd6' : '#f7f7f3'),
          outlineColor: Cesium.Color.fromCssColorString('#1d6fd6'),
          outlineWidth: 2,
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
        label: {
          text: String(item.sequence),
          font: '12px sans-serif',
          fillColor: Cesium.Color.fromCssColorString(active ? '#ffffff' : '#1d6fd6'),
          style: Cesium.LabelStyle.FILL,
          verticalOrigin: Cesium.VerticalOrigin.CENTER,
          pixelOffset: new Cesium.Cartesian2(0, 0),
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
      }));
    });

    const geofences = Array.isArray(overlays.geofences) ? overlays.geofences : [];
    geofences.forEach((fence) => {
      if (!fence || !fence.enabled) return;
      if (fence.type === 'polygon' && Array.isArray(fence.vertices) && fence.vertices.length >= 3) {
        const vertices = fence.vertices.filter((vertex) =>
          Number.isFinite(Number(vertex.latitudeDeg)) &&
          Number.isFinite(Number(vertex.longitudeDeg))
        );
        if (vertices.length < 3) return;
        const positions = vertices.map((vertex) => Cesium.Cartesian3.fromDegrees(
          numberOr(vertex.longitudeDeg, 0),
          numberOr(vertex.latitudeDeg, 0),
          0
        ));
        overlayEntities.push(viewer.entities.add({
          name: fence.label || 'Geofence polygon',
          polygon: {
            hierarchy: positions,
            material: Cesium.Color.fromCssColorString('#d59b28').withAlpha(0.18),
            outline: true,
            outlineColor: Cesium.Color.fromCssColorString('#d59b28'),
            height: 0,
          },
          polyline: {
            positions: positions.concat([positions[0]]),
            width: 3,
            material: Cesium.Color.fromCssColorString('#d59b28').withAlpha(0.95),
            clampToGround: true,
          },
        }));
      } else if (fence.type === 'circle' &&
                 Number.isFinite(Number(fence.centerLatitudeDeg)) &&
                 Number.isFinite(Number(fence.centerLongitudeDeg)) &&
                 Number(fence.radiusM) > 0) {
        overlayEntities.push(viewer.entities.add({
          name: fence.label || 'Geofence circle',
          position: Cesium.Cartesian3.fromDegrees(
            numberOr(fence.centerLongitudeDeg, 0),
            numberOr(fence.centerLatitudeDeg, 0),
            0
          ),
          ellipse: {
            semiMajorAxis: numberOr(fence.radiusM, 1),
            semiMinorAxis: numberOr(fence.radiusM, 1),
            material: Cesium.Color.fromCssColorString('#d59b28').withAlpha(0.13),
            outline: true,
            outlineColor: Cesium.Color.fromCssColorString('#d59b28'),
            height: 0,
          },
        }));
      }
    });

    const rallyPoints = Array.isArray(overlays.rallyPoints)
      ? overlays.rallyPoints.filter((point) => point.valid && validOverlayPosition(point))
      : [];
    rallyPoints.forEach((point) => {
      overlayEntities.push(viewer.entities.add({
        name: point.label || 'Rally point',
        position: overlayCartesian(point),
        point: {
          pixelSize: 14,
          color: Cesium.Color.fromCssColorString('#0f7b43'),
          outlineColor: Cesium.Color.WHITE,
          outlineWidth: 2,
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
      }));
    });

    const eventMarkers = Array.isArray(overlays.eventMarkers)
      ? overlays.eventMarkers.filter((event) => event.positionValid && validOverlayPosition(event))
      : [];
    eventMarkers.forEach((event) => {
      overlayEntities.push(viewer.entities.add({
        name: event.label || 'Event marker',
        position: overlayCartesian(event),
        point: {
          pixelSize: 12,
          color: Cesium.Color.fromCssColorString(eventSeverityColor(event.severity)),
          outlineColor: Cesium.Color.WHITE,
          outlineWidth: 2,
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
      }));
    });
    viewer.scene.requestRender();
  }

  function localOffsetPoint(position, eastM, northM, upM) {
    const transform = Cesium.Transforms.eastNorthUpToFixedFrame(position);
    return Cesium.Matrix4.multiplyByPoint(
      transform,
      new Cesium.Cartesian3(eastM, northM, upM),
      new Cesium.Cartesian3()
    );
  }

  function localCirclePoints(position, radiusM, upM) {
    const points = [];
    for (let index = 0; index <= 96; ++index) {
      const angle = (index / 96.0) * Math.PI * 2.0;
      points.push(localOffsetPoint(
        position,
        Math.sin(angle) * radiusM,
        Math.cos(angle) * radiusM,
        upM
      ));
    }
    return points;
  }

  function vehicleRelativePoint(position, forwardAxis, rightAxis, upAxis, forwardM, rightM, upM) {
    const forward = Cesium.Cartesian3.multiplyByScalar(
      forwardAxis,
      forwardM,
      new Cesium.Cartesian3()
    );
    const right = Cesium.Cartesian3.multiplyByScalar(
      rightAxis,
      rightM,
      new Cesium.Cartesian3()
    );
    const up = Cesium.Cartesian3.multiplyByScalar(
      upAxis,
      upM,
      new Cesium.Cartesian3()
    );
    return Cesium.Cartesian3.add(
      position,
      Cesium.Cartesian3.add(forward, Cesium.Cartesian3.add(right, up, new Cesium.Cartesian3()), new Cesium.Cartesian3()),
      new Cesium.Cartesian3()
    );
  }

  function headingAxes(position, headingDeg) {
    const headingRad = Cesium.Math.toRadians(headingDeg);
    return {
      forward: enuVector(position, Math.sin(headingRad), Math.cos(headingRad), 0.0),
      right: enuVector(position, Math.cos(headingRad), -Math.sin(headingRad), 0.0),
      up: enuVector(position, 0.0, 0.0, 1.0),
    };
  }

  function addTerrainReferenceLine(positions, width, cssColor, alpha) {
    if (!terrainReferenceCollection) return;
    terrainReferenceCollection.add({
      positions,
      width,
      material: Cesium.Material.fromType('Color', {
        color: Cesium.Color.fromCssColorString(cssColor).withAlpha(alpha),
      }),
    });
  }

  function clearanceCueColor() {
    const clearance = state.clearance || {};
    if (clearance.state === 'warning') return '#d92626';
    if (clearance.state === 'caution') return '#d59b28';
    if (clearance.state === 'clear') return '#0f7b43';
    return '#3f4a3d';
  }

  function addTerrainTrackCorridor(center, vehicle, baseAlt) {
    if (!terrainReferenceCollection) return;
    const axes = headingAxes(center, vehicleHeadingDeg(vehicle));
    const clearanceColor = clearanceCueColor();
    const terrain = state.terrain || {};
    const clearance = state.clearance || {};
    const terrainHeight = numberOr(clearance.terrainHeightM, numberOr(terrain.currentHeightM, baseAlt));
    const vehicleAlt = numberOr(vehicle.altitudeM, baseAlt + 120.0);
    const aglM = numberOr(clearance.aglM, vehicleAlt - terrainHeight);
    const laneHalfWidthM = clamp(numberOr(vehicle.groundspeedMps, 0) * 1.3 + 32.0, 42.0, 86.0);
    const forwardStartM = 45.0;
    const forwardEndM = clamp(numberOr(vehicle.groundspeedMps, 0) * 12.0 + 420.0, 420.0, 900.0);
    const centerUpM = Math.max(28.0, aglM * 0.24);

    [-laneHalfWidthM, laneHalfWidthM].forEach((rightM) => {
      addTerrainReferenceLine([
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardStartM, rightM, centerUpM),
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardEndM, rightM, centerUpM),
      ], 4.2, clearanceColor, 0.62);
    });
    [120.0, 300.0, 560.0].forEach((forwardM, index) => {
      if (forwardM > forwardEndM) return;
      addTerrainReferenceLine([
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardM, -laneHalfWidthM, centerUpM),
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardM, laneHalfWidthM, centerUpM),
      ], index === 0 ? 3.8 : 3.0, index === 0 ? '#f7f7f3' : clearanceColor, index === 0 ? 0.56 : 0.42);
    });
    addTerrainReferenceLine([
      vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardStartM, 0.0, centerUpM + 10.0),
      vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardEndM, 0.0, centerUpM + 10.0),
    ], 3.2, '#fff2a6', 0.48);

    [0.35, 0.72].forEach((fraction) => {
      const forwardM = forwardStartM + (forwardEndM - forwardStartM) * fraction;
      addTerrainReferenceLine([
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardM, -laneHalfWidthM * 0.9, centerUpM + 16.0),
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardM + 58.0, 0.0, centerUpM - 4.0),
        vehicleRelativePoint(center, axes.forward, axes.right, axes.up, forwardM, laneHalfWidthM * 0.9, centerUpM + 16.0),
      ], 2.8, '#9ed0ff', 0.34);
    });
  }

  function updateTerrainReference() {
    if (terrainReferenceCollection) terrainReferenceCollection.removeAll();
    if (workspaceMode === 'tactical' || workspaceMode === 'fpv') return;
    const vehicle = state.vehicle || {};
    const home = state.home || {};
    const centerLat = numberOr(vehicle.latDeg, numberOr(home.latDeg, 37.4275));
    const centerLon = numberOr(vehicle.lonDeg, numberOr(home.lonDeg, -122.1697));
    const baseAlt = Math.max(0, numberOr(home.altitudeM, 0));
    if (!cameraInitialized && !(vehicle.positionValid && validPosition(vehicle))) {
      cameraInitialized = true;
      viewer.camera.setView({
        destination: Cesium.Cartesian3.fromDegrees(centerLon, centerLat - 0.010, baseAlt + 1650.0),
        orientation: {
          heading: 0.0,
          pitch: Cesium.Math.toRadians(-58.0),
          roll: 0.0,
        },
      });
    }
    const center = vehicle.positionValid && validPosition(vehicle)
      ? cartesian(vehicle)
      : Cesium.Cartesian3.fromDegrees(centerLon, centerLat, baseAlt);
    [-500.0, -250.0, 0.0, 250.0, 500.0].forEach((offset) => {
      addTerrainReferenceLine([
        localOffsetPoint(center, -760.0, offset, 1.0),
        localOffsetPoint(center, 760.0, offset, 1.0),
      ], offset === 0.0 ? 3.6 : 2.0, offset === 0.0 ? '#2d3d34' : '#49624b', offset === 0.0 ? 0.54 : 0.32);
      addTerrainReferenceLine([
        localOffsetPoint(center, offset, -760.0, 1.0),
        localOffsetPoint(center, offset, 760.0, 1.0),
      ], offset === 0.0 ? 3.6 : 2.0, offset === 0.0 ? '#2d3d34' : '#49624b', offset === 0.0 ? 0.54 : 0.32);
    });
    addTerrainReferenceLine(localCirclePoints(center, 250.0, 2.0), 2.2, '#f7f7f3', 0.30);
    addTerrainReferenceLine(localCirclePoints(center, 500.0, 2.0), 1.8, '#49624b', 0.24);
    if (vehicle.positionValid && validPosition(vehicle)) {
      addTerrainTrackCorridor(center, vehicle, baseAlt);
    }
    viewer.scene.requestRender();
  }

  function attitudeAxisRingPoints(position, radiusM, firstAxis, secondAxis) {
    const points = [];
    for (let index = 0; index <= 96; ++index) {
      const angle = (index / 96.0) * Math.PI * 2.0;
      const first = Cesium.Cartesian3.multiplyByScalar(
        firstAxis,
        Math.cos(angle) * radiusM,
        new Cesium.Cartesian3()
      );
      const second = Cesium.Cartesian3.multiplyByScalar(
        secondAxis,
        Math.sin(angle) * radiusM,
        new Cesium.Cartesian3()
      );
      const offset = Cesium.Cartesian3.add(first, second, new Cesium.Cartesian3());
      points.push(Cesium.Cartesian3.add(position, offset, new Cesium.Cartesian3()));
    }
    return points;
  }

  function addAttitudeReferenceLine(positions, width, cssColor, alpha) {
    if (!attitudeReferenceCollection) return;
    attitudeReferenceCollection.add({
      positions,
      width,
      material: Cesium.Material.fromType('Color', {
        color: Cesium.Color.fromCssColorString(cssColor).withAlpha(alpha),
      }),
    });
  }

  function addTacticalHeadingArrow(position, axes, forwardM, halfWidthM, color, alpha) {
    addAttitudeReferenceLine([
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, 0.0, 1.5),
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM - 18.0, -halfWidthM, 1.5),
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM - 11.0, 0.0, 1.5),
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM - 18.0, halfWidthM, 1.5),
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, 0.0, 1.5),
    ], 2.8, color, alpha);
  }

  function updateTacticalReferences(vehicle, position) {
    const attitude = Cesium.Matrix3.fromQuaternion(
      orientationFor(vehicle, position),
      new Cesium.Matrix3()
    );
    const forwardAxis = Cesium.Matrix3.multiplyByVector(
      attitude,
      Cesium.Cartesian3.UNIT_X,
      new Cesium.Cartesian3()
    );
    const rightAxis = Cesium.Matrix3.multiplyByVector(
      attitude,
      Cesium.Cartesian3.UNIT_Y,
      new Cesium.Cartesian3()
    );
    const upAxis = Cesium.Matrix3.multiplyByVector(
      attitude,
      Cesium.Cartesian3.UNIT_Z,
      new Cesium.Cartesian3()
    );
    const heading = headingAxes(position, vehicleHeadingDeg(vehicle));

    [36.0, 72.0].forEach((radius, index) => {
      addAttitudeReferenceLine(
        localCirclePoints(position, radius, -3.0),
        index === 0 ? 1.9 : 1.3,
        index === 0 ? '#4d5f61' : '#293b3d',
        index === 0 ? 0.44 : 0.30
      );
    });
    [-72.0, 0.0, 72.0].forEach((offset) => {
      const major = offset === 0.0 || Math.abs(offset) === 72.0;
      addAttitudeReferenceLine([
        localOffsetPoint(position, -86.0, offset, -3.0),
        localOffsetPoint(position, 86.0, offset, -3.0),
      ], major ? 1.9 : 1.2, major ? '#344d4f' : '#243337', major ? 0.34 : 0.24);
      addAttitudeReferenceLine([
        localOffsetPoint(position, offset, -86.0, -3.0),
        localOffsetPoint(position, offset, 86.0, -3.0),
      ], major ? 1.9 : 1.2, major ? '#3f444f' : '#2a3038', major ? 0.32 : 0.22);
    });

    [
      {
        positions: attitudeAxisRingPoints(position, tacticalRingRadiusM, rightAxis, upAxis),
        color: '#d92626',
        width: 4.4,
      },
      {
        positions: attitudeAxisRingPoints(position, tacticalRingRadiusM * 1.12, forwardAxis, upAxis),
        color: '#2fbf5b',
        width: 3.8,
      },
      {
        positions: attitudeAxisRingPoints(position, tacticalRingRadiusM * 1.24, forwardAxis, rightAxis),
        color: '#2f6df6',
        width: 3.8,
      },
    ].forEach((ring) => {
      attitudeReferenceCollection.add({
        positions: ring.positions,
        width: ring.width,
        material: Cesium.Material.fromType('Color', {
          color: Cesium.Color.fromCssColorString(ring.color).withAlpha(0.58),
        }),
      });
    });
    [
      {axis: forwardAxis, color: '#2fbf5b', length: 18.0},
      {axis: rightAxis, color: '#d92626', length: 15.0},
      {axis: upAxis, color: '#2f6df6', length: 13.0},
    ].forEach((spoke) => {
      attitudeReferenceCollection.add({
        positions: [
          position,
          Cesium.Cartesian3.add(
            position,
            Cesium.Cartesian3.multiplyByScalar(spoke.axis, spoke.length, new Cesium.Cartesian3()),
            new Cesium.Cartesian3()
          ),
        ],
        width: 3.6,
        material: Cesium.Material.fromType('Color', {
          color: Cesium.Color.fromCssColorString(spoke.color).withAlpha(0.70),
        }),
      });
    });

    addAttitudeReferenceLine([
      vehicleRelativePoint(position, heading.forward, heading.right, heading.up, -58.0, 0.0, 1.0),
      vehicleRelativePoint(position, heading.forward, heading.right, heading.up, 104.0, 0.0, 1.0),
    ], 2.6, '#f7f7f3', 0.62);
    addTacticalHeadingArrow(position, heading, 118.0, 10.0, '#f7f7f3', 0.66);

    const trackLeadM = Math.max(62.0, Math.min(130.0, numberOr(vehicle.groundspeedMps, 0.0) * 5.0));
    addAttitudeReferenceLine([
      vehicleRelativePoint(position, heading.forward, heading.right, heading.up, 0.0, -16.0, 2.0),
      vehicleRelativePoint(position, heading.forward, heading.right, heading.up, trackLeadM, -16.0, 2.0),
    ], 2.0, '#d59b28', 0.54);
    addTacticalHeadingArrow(position, heading, trackLeadM + 10.0, 6.5, '#d59b28', 0.58);

    for (let index = 0; index < 12; ++index) {
      const angle = (index / 12.0) * Math.PI * 2.0;
      const major = index % 6 === 0;
      const radius = tacticalRingRadiusM * 1.42;
      const tickM = major ? 4.4 : 2.5;
      const first = Cesium.Cartesian3.multiplyByScalar(rightAxis, Math.cos(angle), new Cesium.Cartesian3());
      const second = Cesium.Cartesian3.multiplyByScalar(upAxis, Math.sin(angle), new Cesium.Cartesian3());
      const radial = Cesium.Cartesian3.normalize(
        Cesium.Cartesian3.add(first, second, new Cesium.Cartesian3()),
        new Cesium.Cartesian3()
      );
      attitudeReferenceCollection.add({
        positions: [
          Cesium.Cartesian3.add(
            position,
            Cesium.Cartesian3.multiplyByScalar(radial, radius - tickM, new Cesium.Cartesian3()),
            new Cesium.Cartesian3()
          ),
          Cesium.Cartesian3.add(
            position,
            Cesium.Cartesian3.multiplyByScalar(radial, radius + tickM, new Cesium.Cartesian3()),
            new Cesium.Cartesian3()
          ),
        ],
        width: major ? 2.8 : 1.6,
        material: Cesium.Material.fromType('Color', {
          color: Cesium.Color.fromCssColorString(major ? '#f7f7f3' : '#9fb0a1').withAlpha(major ? 0.62 : 0.36),
        }),
      });
    }
  }

  function updateFpvReferences(vehicle, position) {
    const axes = fpvAttitudeAxes(vehicle, position);
    const horizonWidthM = 58.0;
    const pitchLadderWidthM = 32.0;
    [110.0, 240.0].forEach((forwardM, index) => {
      addAttitudeReferenceLine([
        vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, -horizonWidthM, 0.0),
        vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, -14.0, 0.0),
      ], index === 0 ? 4.4 : 2.8, '#f7f7f3', index === 0 ? 0.58 : 0.38);
      addAttitudeReferenceLine([
        vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, 14.0, 0.0),
        vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, horizonWidthM, 0.0),
      ], index === 0 ? 4.4 : 2.8, '#f7f7f3', index === 0 ? 0.58 : 0.38);
      [-16.0, 16.0].forEach((upM) => {
        addAttitudeReferenceLine([
          vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, -pitchLadderWidthM, upM),
          vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, -10.0, upM),
        ], 2.4, upM > 0 ? '#9ed0ff' : '#fff2a6', 0.34);
        addAttitudeReferenceLine([
          vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, 10.0, upM),
          vehicleRelativePoint(position, axes.forward, axes.right, axes.up, forwardM, pitchLadderWidthM, upM),
        ], 2.4, upM > 0 ? '#9ed0ff' : '#fff2a6', 0.34);
      });
    });
    addAttitudeReferenceLine([
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, 80.0, -8.0, -8.0),
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, 120.0, 0.0, -14.0),
      vehicleRelativePoint(position, axes.forward, axes.right, axes.up, 80.0, 8.0, -8.0),
    ], 3.0, clearanceCueColor(), 0.54);
    viewer.scene.requestRender();
  }

  function updateAttitudeReferences() {
    if (!attitudeReferenceCollection) return;
    attitudeReferenceCollection.removeAll();
    if (workspaceMode !== 'tactical' && workspaceMode !== 'fpv') return;
    const vehicle = state.vehicle || {};
    if (!vehicle.positionValid || !validPosition(vehicle)) return;
    const position = cartesian(vehicle);
    if (workspaceMode === 'fpv') {
      updateFpvReferences(vehicle, position);
      return;
    }
    updateTacticalReferences(vehicle, position);
    viewer.scene.requestRender();
  }

  function renderCesium() {
    if (!ensureViewer()) return;
    canvas.style.display = 'none';
    cesiumScene.style.display = 'block';
    applyWorkspaceSceneStyle();
    if (workspaceMode === 'tactical' && cameraMode !== 'tactical') {
      cameraMode = 'tactical';
      resetLockedCameraOffset('tactical');
      emitCameraMode();
    }
    if (workspaceMode === 'fpv' && cameraMode !== 'fpv') {
      cameraMode = 'fpv';
      resetFpvLook();
      emitCameraMode();
    }
    updateTerrainProvider();
    updateAircraftModel((state.terrain || {}).fixture || {}, state.model || {});
    loadVehicleModelProfile(state.model || {});
    updateTerrainReference();
    updateVehicle();
    updateAttitudeReferences();
    applyControlSurfaces(state.controlSurfaces || []);
    updateHome();
    updateTrail();
    updateNavigationOverlays();
  }

  function applySnapshot(snapshot) {
    state = {
      vehicle: snapshot.vehicle || state.vehicle,
      home: snapshot.home || state.home,
      trail: snapshot.trail || state.trail || [],
      terrain: snapshot.terrain || state.terrain,
      scene: snapshot.scene || state.scene,
      overlays: snapshot.overlays || state.overlays,
      model: snapshot.model || state.model,
      controlSurfaces: snapshot.controlSurfaces || state.controlSurfaces || [],
      clearance: snapshot.clearance || state.clearance,
      config: snapshot.config || state.config,
    };
    renderCesium();
  }

  function captureCesiumPng() {
    if (!ensureViewer() || !viewer.scene || !viewer.scene.canvas) {
      return '';
    }
    const vehicle = state.vehicle || {};
    if (workspaceMode === 'fpv') {
      resetFpvCamera();
    } else if (workspaceMode !== 'tactical' && vehicle.positionValid && validPosition(vehicle)) {
      applyCamera(vehicle, cartesian(vehicle), true);
    } else if (workspaceMode === 'tactical') {
      resetTacticalCamera();
    }
    viewer.resize();
    viewer.scene.requestRender();
    viewer.scene.render();
    return viewer.scene.canvas.toDataURL('image/png');
  }

  function selectedProfileId() {
    if (vehicleModelProfile && vehicleModelProfile.id) return String(vehicleModelProfile.id);
    const model = state.model || {};
    return model.profile ? String(model.profile) : '';
  }

  function selectedProfileAssetUri() {
    if (vehicleModelProfile && vehicleModelProfile.asset) return String(vehicleModelProfile.asset);
    const model = state.model || {};
    return model.asset ? String(model.asset) : '';
  }

  function nativeRendererMetadata() {
    const profileAssetUri = selectedProfileAssetUri();
    return {
      renderer: ensureViewer() ? 'cesium-webengine' : 'html-fallback',
      workspaceMode,
      cameraMode,
      vehicleLocked: workspaceMode === 'tactical' || workspaceMode === 'fpv' ||
        cameraMode === 'chase' || cameraMode === 'orbit',
      freeRoamAvailable: workspaceMode !== 'tactical' && workspaceMode !== 'fpv',
      terrainEnabled: workspaceMode !== 'tactical',
      ownshipHidden: workspaceMode === 'fpv',
      fixedFovDeg: workspaceMode === 'fpv' ? fpvVerticalFovDeg : null,
      forwardHemisphereDot: workspaceMode === 'fpv' ? fpvLook.forwardDot : null,
      forwardHemisphereCompliant: workspaceMode !== 'fpv' || fpvLook.forwardDot >= -1.0e-6,
      profileId: selectedProfileId(),
      profileAssetUri,
      snapshotProfileId: state.model && state.model.profile ? String(state.model.profile) : '',
      snapshotAssetUri: state.model && state.model.asset ? String(state.model.asset) : '',
      loadedModelUri: aircraftModelUri || '',
      requestedModelUri: aircraftModelRequestedUri || '',
      modelLoaded: !!aircraftModelPrimitive,
      modelMatchesProfileAsset: !!profileAssetUri && aircraftModelUri === profileAssetUri,
      sceneCueCounts: {
        attitudeReferences: attitudeReferenceCollection ? attitudeReferenceCollection.length : 0,
        terrainReferences: terrainReferenceCollection ? terrainReferenceCollection.length : 0,
        trailSegments: trailPolylineCollection ? trailPolylineCollection.length : 0,
      },
    };
  }

  function overlayDiagnostics() {
    const overlays = state.overlays || {};
    const missionItems = Array.isArray(overlays.missionItems) ? overlays.missionItems : [];
    const geofences = Array.isArray(overlays.geofences) ? overlays.geofences : [];
    const rallyPoints = Array.isArray(overlays.rallyPoints) ? overlays.rallyPoints : [];
    const eventMarkers = Array.isArray(overlays.eventMarkers) ? overlays.eventMarkers : [];
    return {
      workspaceMode,
      visible: workspaceMode !== 'tactical' && workspaceMode !== 'fpv',
      missionItems: missionItems.length,
      geofences: geofences.length,
      rallyPoints: rallyPoints.length,
      eventMarkers: eventMarkers.length,
      activeMissionSeq: numberOr(overlays.activeMissionSeq, -1),
      missionValid: !!overlays.missionValid,
      renderedEntities: overlayEntities.length,
    };
  }

  function inspectControlSurfaces() {
    if (viewer && viewer.scene) {
      viewer.resize();
      viewer.scene.requestRender();
      viewer.scene.render();
    }
    const metadata = nativeRendererMetadata();
    const controller = ensureVehicleModelController();
    if (!controller) {
      return {
        ...metadata,
        ok: false,
        profileLoaded: false,
        surfaceCount: 0,
        surfaces: [],
        failures: ['VehicleModelController is not available'],
      };
    }
    controller.setModel(aircraftModelPrimitive);
    controller.applyControlSurfaces(state.controlSurfaces || []);
    const diagnostic = controller.inspectControlSurfaces(state.controlSurfaces || []);
    const failures = Array.isArray(diagnostic.failures) ? diagnostic.failures.slice() : [];
    if (metadata.renderer !== 'cesium-webengine') {
      failures.push(`renderer ${metadata.renderer} is not cesium-webengine`);
    }
    if (!metadata.modelMatchesProfileAsset) {
      failures.push(
        `loaded model ${metadata.loadedModelUri || '-'} does not match selected profile asset ${metadata.profileAssetUri || '-'}`
      );
    }
    return {
      ...diagnostic,
      ...metadata,
      ok: diagnostic.ok && failures.length === 0,
      failures,
    };
  }

  function setCameraMode(mode) {
    if (workspaceMode === 'tactical' && mode !== 'tactical') {
      return false;
    }
    if (workspaceMode === 'fpv' && mode !== 'fpv') {
      return false;
    }
    if (mode !== 'chase' && mode !== 'orbit' && mode !== 'free' &&
        mode !== 'tactical' && mode !== 'fpv') {
      return false;
    }
    if (mode === 'free' && cameraMode !== 'free') {
      setFreeFromCurrentPose(currentFocus());
    }
    cameraMode = mode;
    if (mode === 'chase' || mode === 'orbit') {
      resetLockedCameraOffset(mode);
    } else if (mode === 'tactical') {
      resetLockedCameraOffset('tactical');
    } else if (mode === 'fpv') {
      resetFpvLook();
    }
    const vehicle = state.vehicle || {};
    if (viewer && vehicle.positionValid && validPosition(vehicle)) {
      applyCamera(vehicle, cartesian(vehicle), false);
      viewer.scene.requestRender();
    } else if (viewer) {
      applyManualCamera();
      viewer.scene.requestRender();
    }
    emitCameraMode();
    return true;
  }

  function setWorkspaceMode(mode) {
    const nextMode = mode === 'tactical' ? 'tactical' : (mode === 'fpv' ? 'fpv' : 'terrain-3d');
    if (workspaceMode === nextMode) return true;
    workspaceMode = nextMode;
    if (workspaceMode === 'tactical') {
      cameraMode = 'tactical';
      resetLockedCameraOffset('tactical');
    } else if (workspaceMode === 'fpv') {
      cameraMode = 'fpv';
      resetFpvLook();
    } else if (cameraMode === 'tactical' || cameraMode === 'fpv') {
      cameraMode = 'chase';
      resetLockedCameraOffset('chase');
    }
    renderCesium();
    emitCameraMode();
    return true;
  }

  function cameraState() {
    const offset = lockedCameraOffsets[cameraMode] || null;
    if (workspaceMode === 'fpv') {
      applyFpvCamera();
    }
    return {
      renderer: ensureViewer() ? 'cesium-webengine' : 'html-fallback',
      ok: (workspaceMode !== 'tactical' || cameraMode === 'tactical') &&
        (workspaceMode !== 'fpv' || (cameraMode === 'fpv' && fpvLook.forwardDot >= -1.0e-6)),
      workspaceMode,
      mode: cameraMode,
      cameraMode,
      freeRoamAvailable: workspaceMode !== 'tactical' && workspaceMode !== 'fpv',
      vehicleLocked: workspaceMode === 'tactical' || workspaceMode === 'fpv' ||
        cameraMode === 'chase' || cameraMode === 'orbit',
      headingDeg: workspaceMode === 'fpv' ? fpvLook.yawDeg :
        (offset ? offset.headingDeg : freeCamera.headingDeg),
      pitchDeg: workspaceMode === 'fpv' ? fpvLook.pitchDeg :
        (offset ? offset.pitchDeg : freeCamera.pitchDeg),
      rangeM: offset ? offset.rangeM : freeCamera.rangeM,
      fixedFovDeg: workspaceMode === 'fpv' ? fpvVerticalFovDeg : null,
      fovDeg: viewer && viewer.camera && viewer.camera.frustum
        ? Cesium.Math.toDegrees(viewer.camera.frustum.fovy)
        : null,
      ownshipHidden: workspaceMode === 'fpv',
      terrainEnabled: workspaceMode !== 'tactical',
      forwardHemisphereDot: workspaceMode === 'fpv' ? fpvLook.forwardDot : null,
      forwardHemisphereCompliant: workspaceMode !== 'fpv' || fpvLook.forwardDot >= -1.0e-6,
      sceneCueCounts: {
        attitudeReferences: attitudeReferenceCollection ? attitudeReferenceCollection.length : 0,
        terrainReferences: terrainReferenceCollection ? terrainReferenceCollection.length : 0,
        trailSegments: trailPolylineCollection ? trailPolylineCollection.length : 0,
      },
    };
  }

  window.addEventListener('resize', function () {
    if (viewer) {
      viewer.resize();
      viewer.scene.requestRender();
    } else {
      renderFallback();
    }
  });
  window.animusApplySnapshot = applySnapshot;
  window.animusCaptureCesiumPng = captureCesiumPng;
  window.animusInspectControlSurfaces = inspectControlSurfaces;
  window.animusOverlayDiagnostics = overlayDiagnostics;
  window.animusResetFpvCamera = resetFpvCamera;
  window.animusResetTacticalCamera = resetTacticalCamera;
  window.animusSetCameraMode = setCameraMode;
  window.animusSetWorkspaceMode = setWorkspaceMode;
  window.animusFpvCameraState = cameraState;
  window.animusCameraState = cameraState;
  statusEl.textContent = 'Waiting for Animus terrain state...';
})();
