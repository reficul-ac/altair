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
  let trailPolylineCollection = null;
  let trailEntities = [];
  let imageryLayer = null;
  let terrainProviderKey = '';
  let aircraftModelUri = '';
  let cameraInitialized = false;
  let cameraMode = 'chase';
  let lastLockPosition = null;
  let pointerDrag = null;
  let spaceDown = false;
  const lockedCameraOffsets = {
    chase: {
      defaultHeadingDeg: 180.0,
      headingDeg: 180.0,
      pitchDeg: -12.0,
      rangeM: 260.0,
      followsVehicleHeading: true,
    },
    orbit: {
      defaultHeadingDeg: 135.0,
      headingDeg: 135.0,
      pitchDeg: -26.0,
      rangeM: 520.0,
      followsVehicleHeading: false,
    },
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
    statusEl.textContent = errorText ? `${terrainText} | ${errorText}` : terrainText;
  }

  function setSceneStatus(status, error) {
    const suffix = error ? ` | ${error}` : '';
    statusEl.textContent = `${status}${suffix}`;
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
    offset.pitchDeg = mode === 'orbit' ? -26.0 : -12.0;
    offset.rangeM = mode === 'orbit' ? 520.0 : 260.0;
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
    if (cameraMode === 'free' && freeCamera.focus) return freeCamera.focus;
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

  function applyManualCamera() {
    if (!viewer) return;
    if (cameraMode === 'free') {
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
    if (cameraMode === 'free') {
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
    const factor = Math.exp(clamp(wheelDeltaY, -600.0, 600.0) * 0.0015);
    if (cameraMode === 'free') {
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
    target.addEventListener('pointerdown', function (event) {
      if (event.button !== 0 && event.button !== 1) return;
      target.focus();
      pointerDrag = {
        id: event.pointerId,
        x: event.clientX,
        y: event.clientY,
        action: event.button === 1 || spaceDown ? 'rotate' : 'pan',
        button: event.button,
      };
      target.setPointerCapture(event.pointerId);
      event.preventDefault();
    });
    target.addEventListener('pointermove', function (event) {
      if (!pointerDrag || pointerDrag.id !== event.pointerId) return;
      const deltaX = event.clientX - pointerDrag.x;
      const deltaY = event.clientY - pointerDrag.y;
      pointerDrag.x = event.clientX;
      pointerDrag.y = event.clientY;
      const rotate = pointerDrag.action === 'rotate' || (pointerDrag.button === 0 && spaceDown);
      if (rotate) {
        rotateCurrentCamera(deltaX, deltaY);
      } else {
        switchToFreeFromPan();
        panFreeCamera(deltaX, deltaY);
      }
      viewer.scene.requestRender();
      event.preventDefault();
    });
    target.addEventListener('pointerup', function (event) {
      if (pointerDrag && pointerDrag.id === event.pointerId) {
        pointerDrag = null;
        target.releasePointerCapture(event.pointerId);
      }
      event.preventDefault();
    });
    target.addEventListener('pointercancel', function (event) {
      if (pointerDrag && pointerDrag.id === event.pointerId) {
        pointerDrag = null;
      }
    });
    target.addEventListener('wheel', function (event) {
      zoomCurrentCamera(event.deltaY);
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
    const terrain = state.terrain || {};
    const fixture = terrain.fixture || {};
    const provider = terrain.provider || 'heightmap-fixture';
    const key = provider === 'quantized-mesh'
      ? `mesh:${terrain.cacheUrl || ''}`
      : `fixture:${fixture.name || 'default'}:${fixture.width || ''}x${fixture.height || ''}`;
    if (terrainProviderKey === key) return;
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

  function updateAircraftModel(fixture) {
    if (!viewer || !vehicleEntity) return;
    const modelUri = fixture && fixture.aircraftModelUrl ? String(fixture.aircraftModelUrl) : '';
    if (aircraftModelUri === modelUri) return;
    aircraftModelUri = modelUri;
    vehicleEntity.model = modelUri ? new Cesium.ModelGraphics({
      uri: modelUri,
      scale: 3.0,
      minimumPixelSize: 150,
      maximumScale: 1200,
      color: Cesium.Color.fromCssColorString('#1d6fd6'),
      colorBlendMode: Cesium.ColorBlendMode.MIX,
      colorBlendAmount: 0.85,
      silhouetteColor: Cesium.Color.WHITE,
      silhouetteSize: 2.0,
      runAnimations: false,
    }) : undefined;
    if (vehiclePointPrimitive) {
      vehiclePointPrimitive.show = false;
    }
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
    if (!vehicle.positionValid || !validPosition(vehicle)) return;

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
      if (aircraftOutlineCollection) aircraftOutlineCollection.removeAll();
      if (vehiclePointPrimitive) vehiclePointPrimitive.show = false;
      viewer.scene.requestRender();
      return;
    }

    const position = cartesian(vehicle);
    vehicleEntity.show = true;
    vehicleEntity.position = position;
    vehicleEntity.orientation = orientationFor(vehicle, position);
    updateAircraftOutline(vehicle, position);
    if (vehiclePointPrimitive) {
      vehiclePointPrimitive.show = !aircraftModelUri;
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

    const trail = (state.trail || []).filter(validPosition);
    for (let i = 1; i < trail.length; ++i) {
      const recency = i / Math.max(1, trail.length - 1);
      if (trailPolylineCollection) {
        trailPolylineCollection.add({
          positions: [cartesian(trail[i - 1]), cartesian(trail[i])],
          width: 7.0 + recency * 8.0,
          material: Cesium.Material.fromType('Color', {
            color: Cesium.Color.fromCssColorString('#f0c84b').withAlpha(0.62 + recency * 0.38),
          }),
        });
      }
      trailEntities.push(viewer.entities.add({
        name: 'Trail segment',
        polyline: {
          positions: [cartesian(trail[i - 1]), cartesian(trail[i])],
          width: 4.0 + recency * 5.0,
          material: Cesium.Color.fromCssColorString('#f0c84b').withAlpha(0.38 + recency * 0.62),
          depthFailMaterial: Cesium.Color.fromCssColorString('#f0c84b').withAlpha(0.48 + recency * 0.52),
          clampToGround: false,
        },
      }));
    }
    viewer.scene.requestRender();
  }

  function updateTerrainReference() {
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
    viewer.scene.requestRender();
  }

  function renderCesium() {
    if (!ensureViewer()) return;
    canvas.style.display = 'none';
    cesiumScene.style.display = 'block';
    updateTerrainProvider();
    updateAircraftModel((state.terrain || {}).fixture || {});
    updateTerrainReference();
    updateVehicle();
    updateHome();
    updateTrail();
  }

  function applySnapshot(snapshot) {
    state = {
      vehicle: snapshot.vehicle || state.vehicle,
      home: snapshot.home || state.home,
      trail: snapshot.trail || state.trail || [],
      terrain: snapshot.terrain || state.terrain,
      scene: snapshot.scene || state.scene,
      config: snapshot.config || state.config,
    };
    renderCesium();
  }

  function captureCesiumPng() {
    if (!ensureViewer() || !viewer.scene || !viewer.scene.canvas) {
      return '';
    }
    const vehicle = state.vehicle || {};
    if (vehicle.positionValid && validPosition(vehicle)) {
      applyCamera(vehicle, cartesian(vehicle), true);
    }
    viewer.resize();
    viewer.scene.requestRender();
    viewer.scene.render();
    return viewer.scene.canvas.toDataURL('image/png');
  }

  function setCameraMode(mode) {
    if (mode !== 'chase' && mode !== 'orbit' && mode !== 'free') {
      return false;
    }
    if (mode === 'free' && cameraMode !== 'free') {
      setFreeFromCurrentPose(currentFocus());
    }
    cameraMode = mode;
    if (mode === 'chase' || mode === 'orbit') {
      resetLockedCameraOffset(mode);
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
  window.animusSetCameraMode = setCameraMode;
  statusEl.textContent = 'Waiting for Animus terrain state...';
})();
