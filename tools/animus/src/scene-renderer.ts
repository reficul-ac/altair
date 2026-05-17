import { AmbientLight, ArrowHelper, AxesHelper, BoxGeometry, BufferAttribute, BufferGeometry, CapsuleGeometry, ConeGeometry, CylinderGeometry, DirectionalLight, Euler, Fog, GridHelper, Group, Line, LineBasicMaterial, Matrix4, Mesh, MeshStandardMaterial, PerspectiveCamera, PlaneGeometry, Quaternion, Scene, SphereGeometry, TorusGeometry, Vector3, WebGLRenderer } from 'three';
import { fmt } from './hud-ui';
import { buildFlightTerrainModel, type FlightTerrainModel } from './map-assets';
import type { MapCacheStatus } from './map-cache';
import { TrailBuffer, trailPointFromState, type SessionEvent, type VehicleStateMessage, type TrailPoint } from './state';

export const CAMERA_MODES = ['chase', 'orbit', 'top', 'side', 'fpv', 'free'] as const;
export type CameraMode = (typeof CAMERA_MODES)[number];
export type ThemeName = 'grid' | 'rez' | 'snow';
export type VehicleModelKind = 'fixed-wing' | 'multirotor' | 'vtol' | 'generic';
export type AircraftAttitude = Pick<VehicleStateMessage['attitude'], 'rollRad' | 'pitchRad' | 'yawRad'>;
export type AircraftPose = {
  quaternion: Quaternion;
  euler: Euler;
  heading: Vector3;
  right: Vector3;
  up: Vector3;
};

export function nextCameraMode(mode: CameraMode): CameraMode {
  return CAMERA_MODES[(CAMERA_MODES.indexOf(mode) + 1) % CAMERA_MODES.length];
}

export function classifyVehicleModel(vehicleType: string | null | undefined): VehicleModelKind {
  const label = (vehicleType ?? '').toLowerCase();
  if (label.includes('vtol') || label.includes('tailsitter')) return 'vtol';
  if (label.includes('rotor') || label.includes('copter') || label.includes('helicopter')) return 'multirotor';
  if (label.includes('fixed') || label.includes('plane') || label.includes('airship')) return 'fixed-wing';
  return 'generic';
}

export function aircraftPoseFromTelemetry(attitude: AircraftAttitude): AircraftPose {
  const yaw = attitude.yawRad;
  const pitch = attitude.pitchRad;
  const roll = attitude.rollRad;
  const flatHeading = new Vector3(Math.sin(yaw), Math.cos(yaw), 0).normalize();
  const flatRight = new Vector3(Math.cos(yaw), -Math.sin(yaw), 0).normalize();
  const worldUp = new Vector3(0, 0, 1);
  const heading = flatHeading.clone().multiplyScalar(Math.cos(pitch)).addScaledVector(worldUp, Math.sin(pitch)).normalize();
  const pitchUp = worldUp.clone().multiplyScalar(Math.cos(pitch)).addScaledVector(flatHeading, -Math.sin(pitch)).normalize();
  const right = flatRight.clone().multiplyScalar(Math.cos(roll)).addScaledVector(pitchUp, -Math.sin(roll)).normalize();
  const up = new Vector3().crossVectors(right, heading).normalize();
  const matrix = new Matrix4().makeBasis(heading, right, up);
  const quaternion = new Quaternion().setFromRotationMatrix(matrix);
  return { quaternion, euler: new Euler().setFromQuaternion(quaternion, 'ZYX'), heading, right, up };
}

export function cameraPresetOffset(mode: Exclude<CameraMode, 'free'>, attitude: AircraftAttitude): Vector3 {
  const pose = aircraftPoseFromTelemetry(attitude);
  if (mode === 'chase') return pose.heading.clone().multiplyScalar(-110).add(new Vector3(0, 0, 62));
  if (mode === 'orbit') return new Vector3(-120, -90, 86);
  if (mode === 'top') return new Vector3(0, 0, 260);
  if (mode === 'side') return pose.right.clone().multiplyScalar(120).add(new Vector3(0, 0, 38));
  return pose.heading.clone().multiplyScalar(10).add(new Vector3(0, 0, 3.5));
}

export class SceneRenderer {
  readonly trail = new TrailBuffer(2200);
  private readonly renderer: WebGLRenderer;
  private readonly scene = new Scene();
  private readonly camera = new PerspectiveCamera(55, 1, 0.1, 4000);
  private readonly freeControls = { yaw: 0, pitch: -0.35, keys: new Set<string>(), dragging: false, lastX: 0, lastY: 0 };
  private aircraft = makeVehicleModel('fixed-wing');
  private readonly attitudeRings = makeAttitudeRings();
  private selectedModelKind: VehicleModelKind = 'fixed-wing';
  private readonly otherAircraft = new Map<string, { group: Group; kind: VehicleModelKind }>();
  private readonly headingCue = new ArrowHelper(new Vector3(1, 0, 0), new Vector3(0, 0, 8), 42, 0xffc857, 10, 5);
  private readonly trailGeometry = new BufferGeometry();
  private readonly trailMaterial = new LineBasicMaterial({ color: 0x66e0a3 });
  private readonly runwayMaterial = new MeshStandardMaterial({ color: 0x253640, roughness: 0.9 });
  private readonly terrainMaterial = new MeshStandardMaterial({ color: 0x2f453a, roughness: 0.96, metalness: 0 });
  private readonly grid = new GridHelper(700, 28, 0x497287, 0x20323d);
  private readonly terrainMesh = new Mesh(new BufferGeometry(), this.terrainMaterial);
  private readonly markers = new Map<string, Mesh>();
  private lastMessage: VehicleStateMessage | null = null;
  private mapCacheStatus: MapCacheStatus | null = null;
  private demCacheStatus: MapCacheStatus | null = null;
  private lastPoint: TrailPoint | null = null;
  private frames = 0;
  private fps = 0;
  private lastFpsMs = performance.now();
  private lastFrameMs = 0;
  private lastAnimationMs = performance.now();
  private freeCameraManuallyMoved = false;
  paused = false;
  cameraMode: CameraMode = 'chase';
  cameraLocked = false;
  theme: ThemeName = 'grid';
  ortho = true;
  debug = false;

  constructor(
    private readonly shell: HTMLElement,
    private readonly canvas: HTMLCanvasElement,
    private readonly radarCanvas: HTMLCanvasElement,
    private readonly orthoCanvas: HTMLCanvasElement
  ) {
    this.renderer = new WebGLRenderer({ canvas, antialias: true, alpha: false, preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(0x0b1116);
    this.scene.fog = new Fog(0x0b1116, 420, 1500);
    this.camera.position.set(-80, -140, 80);
    this.camera.up.set(0, 0, 1);
    this.freeControls.yaw = Math.atan2(this.camera.position.x, this.camera.position.y);
    const light = new DirectionalLight(0xffffff, 2.4);
    light.position.set(-180, -100, 250);
    this.scene.add(light);
    this.scene.add(new AmbientLight(0x8aa1b2, 1.5));
    this.grid.rotation.x = Math.PI / 2;
    this.scene.add(this.grid);
    const runway = new Mesh(new PlaneGeometry(240, 26), this.runwayMaterial);
    runway.rotation.x = Math.PI / 2;
    runway.position.z = -0.04;
    this.scene.add(runway);
    this.terrainMesh.name = 'map-cache-terrain-disabled';
    this.scene.add(this.terrainMesh);
    this.scene.add(new AxesHelper(90));
    this.scene.add(this.aircraft);
    this.scene.add(this.attitudeRings);
    this.scene.add(this.headingCue);
    this.scene.add(new Line(this.trailGeometry, this.trailMaterial));
    this.bindPointer();
  }

  setMapCacheStatus(status: MapCacheStatus): void {
    this.mapCacheStatus = status;
    this.updateGroundTerrain();
  }

  setDemCacheStatus(status: MapCacheStatus): void {
    this.demCacheStatus = status;
    this.updateGroundTerrain();
  }

  applyVehicle(message: VehicleStateMessage): void {
    const needsInitialCameraSnap = this.lastPoint === null;
    this.lastMessage = message;
    this.ensureSelectedVehicleModel(message.vehicleType);
    const point = trailPointFromState(message);
    if (point) {
      this.lastPoint = point;
      if (!this.paused) {
        this.trail.add(point);
        this.updateTrail();
      }
      this.aircraft.position.set(point.eastM, point.northM, point.upM);
      this.headingCue.position.set(point.eastM, point.northM, point.upM + 8);
      this.updateGroundTerrain();
    }
    this.applyPose(this.aircraft, message);
    this.updateControlSurfaces(this.aircraft, message);
    const pose = aircraftPoseFromTelemetry(message.attitude);
    this.headingCue.setDirection(pose.heading.clone().setZ(0).normalize());
    this.attitudeRings.position.copy(this.aircraft.position);
    this.attitudeRings.quaternion.copy(pose.quaternion);
    if (needsInitialCameraSnap) {
      if (this.cameraMode === 'free') this.snapCameraToFreeDefault();
      else this.snapCameraToPreset(this.cameraMode);
    }
  }

  applyFleet(vehicles: VehicleStateMessage[], selectedId: string | null, events: SessionEvent[]): void {
    for (const vehicle of vehicles) {
      if (!vehicle.id || vehicle.id === selectedId) continue;
      const kind = classifyVehicleModel(vehicle.vehicleType);
      let entry = this.otherAircraft.get(vehicle.id);
      if (entry && entry.kind !== kind) {
        this.scene.remove(entry.group);
        this.otherAircraft.delete(vehicle.id);
        entry = undefined;
      }
      if (!entry) {
        const group = makeVehicleModel(kind, 0x3aa0ff);
        group.scale.setScalar(0.72);
        entry = { group, kind };
        this.otherAircraft.set(vehicle.id, entry);
        this.scene.add(group);
      }
      this.applyPose(entry.group, vehicle);
      this.updateControlSurfaces(entry.group, vehicle);
    }
    for (const [id, entry] of this.otherAircraft) {
      if (!vehicles.some((vehicle) => vehicle.id === id && vehicle.id !== selectedId)) {
        this.scene.remove(entry.group);
        this.otherAircraft.delete(id);
      }
    }
    this.updateMarkers(events);
  }

  setTheme(theme: ThemeName): void {
    this.theme = theme;
    this.shell.classList.remove('theme-grid', 'theme-rez', 'theme-snow');
    this.shell.classList.add(`theme-${theme}`);
    document.querySelector<HTMLButtonElement>('#theme-toggle')!.textContent = theme[0].toUpperCase() + theme.slice(1);
    const palette: Record<ThemeName, { bg: number; fog: number; grid: number; runway: number; trail: number }> = {
      grid: { bg: 0x0b1116, fog: 0x0b1116, grid: 0x497287, runway: 0x253640, trail: 0x66e0a3 },
      rez: { bg: 0x050909, fog: 0x050909, grid: 0x1ed1b2, runway: 0x071313, trail: 0xe464ff },
      snow: { bg: 0xeef3f5, fog: 0xeef3f5, grid: 0x8297a1, runway: 0xd8e3e8, trail: 0x007a5a }
    };
    const next = palette[theme];
    this.renderer.setClearColor(next.bg);
    this.scene.fog = new Fog(next.fog, 420, 1500);
    (Array.isArray(this.grid.material) ? this.grid.material : [this.grid.material]).forEach((material) => {
      if ('color' in material) material.color.setHex(next.grid);
    });
    this.runwayMaterial.color.setHex(next.runway);
    this.trailMaterial.color.setHex(next.trail);
  }

  setCameraMode(mode: CameraMode): void {
    this.cameraMode = mode;
    document.querySelectorAll<HTMLButtonElement>('[data-camera]').forEach((button) => {
      button.classList.toggle('active', button.dataset.camera === mode);
    });
    if (mode === 'free') {
      if (!this.freeCameraManuallyMoved) this.snapCameraToFreeDefault();
    } else {
      this.snapCameraToPreset(mode);
    }
  }

  setCameraLocked(locked: boolean): void {
    this.cameraLocked = locked;
    const button = document.querySelector<HTMLButtonElement>('#camera-lock');
    if (button) {
      button.classList.toggle('active', locked);
      button.textContent = locked ? 'Unlock Camera' : 'Lock Camera';
    }
  }

  start(): void {
    requestAnimationFrame((now) => this.animate(now));
  }

  clearTrail(): void {
    this.trail.clear();
    this.updateTrail();
  }

  private ensureSelectedVehicleModel(vehicleType: string | null | undefined): void {
    const kind = classifyVehicleModel(vehicleType);
    if (kind === this.selectedModelKind) return;
    const previous = this.aircraft;
    const replacement = makeVehicleModel(kind);
    replacement.position.copy(previous.position);
    replacement.quaternion.copy(previous.quaternion);
    this.scene.remove(previous);
    this.aircraft = replacement;
    this.selectedModelKind = kind;
    this.scene.add(this.aircraft);
  }

  private applyPose(group: Group, message: VehicleStateMessage): void {
    const point = trailPointFromState(message);
    if (point) group.position.set(point.eastM, point.northM, point.upM);
    group.quaternion.copy(aircraftPoseFromTelemetry(message.attitude).quaternion);
  }

  private updateControlSurfaces(group: Group, message: VehicleStateMessage): void {
    const surfaces = message.controlSurfaces;
    const aileron = (surfaces?.aileron ?? 0) * 0.45;
    const elevator = (surfaces?.elevator ?? 0) * 0.45;
    const rudder = (surfaces?.rudder ?? 0) * 0.5;
    const leftAileron = group.getObjectByName('aileron-left');
    const rightAileron = group.getObjectByName('aileron-right');
    const elevatorSurface = group.getObjectByName('elevator');
    const rudderSurface = group.getObjectByName('rudder');
    if (leftAileron) leftAileron.rotation.y = aileron;
    if (rightAileron) rightAileron.rotation.y = -aileron;
    if (elevatorSurface) elevatorSurface.rotation.y = elevator;
    if (rudderSurface) rudderSurface.rotation.z = rudder;
  }

  private updateMarkers(events: SessionEvent[]): void {
    for (const event of events) {
      if (!event.position || this.markers.has(event.id)) continue;
      const marker = new Mesh(
        new SphereGeometry(2.8, 12, 12),
        new MeshStandardMaterial({ color: event.level === 'warning' ? 0xffc857 : event.level === 'error' ? 0xff6b7a : 0x3aa0ff })
      );
      marker.position.set(event.position.eastM, event.position.northM, event.position.upM + 3);
      this.markers.set(event.id, marker);
      this.scene.add(marker);
    }
    while (this.markers.size > 120) {
      const first = this.markers.keys().next().value as string | undefined;
      if (!first) break;
      const marker = this.markers.get(first);
      if (marker) this.scene.remove(marker);
      this.markers.delete(first);
    }
  }

  private updateTrail(): void {
    const points = this.trail.values();
    const positions = new Float32Array(points.length * 3);
    points.forEach((point, index) => {
      positions[index * 3] = point.eastM;
      positions[index * 3 + 1] = point.northM;
      positions[index * 3 + 2] = point.upM;
    });
    this.trailGeometry.setAttribute('position', new BufferAttribute(positions, 3));
    this.trailGeometry.computeBoundingSphere();
  }

  private updateGroundTerrain(): void {
    const model = buildFlightTerrainModel(this.demCacheStatus, this.lastMessage, 17, 45, this.mapCacheStatus);
    this.terrainMesh.visible = model.available;
    if (!model.available) return;
    this.terrainMesh.geometry.dispose();
    this.terrainMesh.geometry = terrainGeometry(model, this.lastMessage);
    this.terrainMaterial.color.setHex(model.textured ? 0x3f5a44 : 0x2f453a);
  }

  private animate(nowMs: number): void {
    const deltaS = Math.min(0.05, (nowMs - this.lastAnimationMs) / 1000);
    this.lastFrameMs = nowMs - this.lastAnimationMs;
    this.lastAnimationMs = nowMs;
    this.resizeNow();
    this.updateCamera(deltaS);
    this.attitudeRings.visible = !document.querySelector<HTMLElement>('#hud-tactical')?.classList.contains('hidden');
    drawRadar(this.radarCanvas, this.shell);
    if (this.ortho) drawOrtho(this.orthoCanvas, this.shell, this.trail.values(), this.lastPoint);
    this.updateDebug(nowMs);
    this.renderer.render(this.scene, this.camera);
    requestAnimationFrame((next) => this.animate(next));
  }

  resizeNow(): void {
    const { clientWidth, clientHeight } = this.canvas.parentElement!;
    if (clientWidth <= 0 || clientHeight <= 0) return;
    this.renderer.setSize(clientWidth, clientHeight, false);
    this.camera.aspect = clientWidth / clientHeight;
    this.camera.updateProjectionMatrix();
  }

  private updateCamera(deltaS: number): void {
    if (this.cameraLocked && this.cameraMode !== 'free') {
      this.applyPresetCamera(this.cameraMode, 1);
      return;
    }
    this.camera.up.set(0, 0, 1);
    const boost = this.freeControls.keys.has('ShiftLeft') || this.freeControls.keys.has('ShiftRight') ? 3 : 1;
    const speed = 70 * boost * deltaS;
    const forward = new Vector3(Math.sin(this.freeControls.yaw), Math.cos(this.freeControls.yaw), 0);
    const right = new Vector3(forward.y, -forward.x, 0);
    if (this.freeControls.keys.has('KeyW')) this.camera.position.addScaledVector(forward, speed);
    if (this.freeControls.keys.has('KeyS')) this.camera.position.addScaledVector(forward, -speed);
    if (this.freeControls.keys.has('KeyA')) this.camera.position.addScaledVector(right, -speed);
    if (this.freeControls.keys.has('KeyD')) this.camera.position.addScaledVector(right, speed);
    if (this.freeControls.keys.has('KeyQ')) this.camera.position.z -= speed;
    if (this.freeControls.keys.has('KeyE')) this.camera.position.z += speed;
    const look = this.camera.position.clone().add(new Vector3(Math.sin(this.freeControls.yaw) * Math.cos(this.freeControls.pitch), Math.cos(this.freeControls.yaw) * Math.cos(this.freeControls.pitch), Math.sin(this.freeControls.pitch)));
    this.camera.lookAt(look);
  }

  private snapCameraToPreset(mode: Exclude<CameraMode, 'free'>): void {
    this.applyPresetCamera(mode, 1);
    this.syncFreeControlsFromCamera();
  }

  private snapCameraToFreeDefault(): void {
    const attitude = this.lastMessage?.attitude ?? { rollRad: 0, pitchRad: 0, yawRad: 0 };
    const pose = aircraftPoseFromTelemetry(attitude);
    const target = this.lastPoint
      ? new Vector3(this.lastPoint.eastM, this.lastPoint.northM, this.lastPoint.upM)
      : this.aircraft.position.clone();
    const offset = pose.heading.clone().multiplyScalar(-130).addScaledVector(pose.right, 22).add(new Vector3(0, 0, 72));
    const lookAt = target.clone().add(new Vector3(0, 0, 18));
    this.camera.up.set(0, 0, 1);
    this.camera.position.copy(target).add(offset);
    this.camera.lookAt(lookAt);
    this.syncFreeControlsFromCamera();
  }

  private applyPresetCamera(mode: Exclude<CameraMode, 'free'>, alpha: number): void {
    const attitude = this.lastMessage?.attitude ?? { rollRad: 0, pitchRad: 0, yawRad: 0 };
    const target = this.aircraft.position;
    this.camera.up.set(0, 0, 1);
    if (mode === 'top') this.camera.up.set(0, 1, 0);
    const offset = cameraPresetOffset(mode, attitude);
    const desired = target.clone().add(offset);
    this.camera.position.lerp(desired, alpha);
    const lookAhead = mode === 'fpv'
      ? target.clone().add(aircraftPoseFromTelemetry(attitude).heading.multiplyScalar(95))
      : target.clone().add(new Vector3(0, 0, mode === 'top' ? 0 : 6));
    this.camera.lookAt(lookAhead);
  }

  private syncFreeControlsFromCamera(): void {
    const direction = new Vector3();
    this.camera.getWorldDirection(direction);
    this.freeControls.yaw = Math.atan2(direction.x, direction.y);
    this.freeControls.pitch = Math.asin(Math.max(-1, Math.min(1, direction.z)));
  }

  private updateDebug(nowMs: number): void {
    this.frames += 1;
    if (nowMs - this.lastFpsMs < 500) return;
    this.fps = (this.frames * 1000) / (nowMs - this.lastFpsMs);
    this.frames = 0;
    this.lastFpsMs = nowMs;
    setText('debug-fps', fmt(this.fps, '', 0));
    setText('debug-frame', fmt(this.lastFrameMs, ' ms', 1));
    setText('debug-trail', String(this.trail.values().length));
    setText('debug-camera', `${this.cameraMode}${this.cameraLocked ? ' locked' : ''}`);
    setText('debug-position', this.lastPoint ? `${fmt(this.lastPoint.eastM)} E / ${fmt(this.lastPoint.northM)} N / ${fmt(this.lastPoint.upM)} U` : '--');
  }

  private bindPointer(): void {
    window.addEventListener('keydown', (event) => {
      this.freeControls.keys.add(event.code);
      if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE'].includes(event.code)) this.freeCameraManuallyMoved = true;
    });
    window.addEventListener('keyup', (event) => this.freeControls.keys.delete(event.code));
    this.canvas.addEventListener('pointerdown', (event) => {
      if (this.cameraLocked) return;
      this.freeControls.dragging = true;
      this.freeControls.lastX = event.clientX;
      this.freeControls.lastY = event.clientY;
      this.canvas.setPointerCapture(event.pointerId);
    });
    this.canvas.addEventListener('pointermove', (event) => {
      if (!this.freeControls.dragging) return;
      if (event.clientX !== this.freeControls.lastX || event.clientY !== this.freeControls.lastY) this.freeCameraManuallyMoved = true;
      this.freeControls.yaw -= (event.clientX - this.freeControls.lastX) * 0.005;
      this.freeControls.pitch = Math.max(-1.25, Math.min(1.25, this.freeControls.pitch - (event.clientY - this.freeControls.lastY) * 0.005));
      this.freeControls.lastX = event.clientX;
      this.freeControls.lastY = event.clientY;
    });
    this.canvas.addEventListener('pointerup', (event) => {
      this.freeControls.dragging = false;
      this.canvas.releasePointerCapture(event.pointerId);
    });
    this.canvas.addEventListener('wheel', (event) => {
      if (this.cameraLocked) return;
      event.preventDefault();
      this.freeCameraManuallyMoved = true;
      const direction = new Vector3();
      this.camera.getWorldDirection(direction);
      this.camera.position.addScaledVector(direction, Math.max(-80, Math.min(80, event.deltaY * 0.12)));
    }, { passive: false });
  }
}

function setText(id: string, value: string): void {
  document.querySelector<HTMLElement>(`#${id}`)!.textContent = value;
}

function terrainGeometry(model: FlightTerrainModel, vehicle: VehicleStateMessage | null): BufferGeometry {
  const originAltM = vehicle?.globalPosition.originAltitudeM ?? vehicle?.home?.altitudeM ?? 0;
  const positions = new Float32Array(model.samples.length * 3);
  const uvs = new Float32Array(model.samples.length * 2);
  model.samples.forEach((sample, index) => {
    positions[index * 3] = sample.eastM;
    positions[index * 3 + 1] = sample.northM;
    positions[index * 3 + 2] = Math.min(sample.elevationM - originAltM, (vehicle?.localPosition.upM ?? 0) - 12);
    uvs[index * 2] = sample.u;
    uvs[index * 2 + 1] = sample.v;
  });
  const indices: number[] = [];
  for (let row = 0; row < model.gridSize - 1; row += 1) {
    for (let col = 0; col < model.gridSize - 1; col += 1) {
      const a = row * model.gridSize + col;
      indices.push(a, a + 1, a + model.gridSize, a + 1, a + model.gridSize + 1, a + model.gridSize);
    }
  }
  const geometry = new BufferGeometry();
  geometry.setAttribute('position', new BufferAttribute(positions, 3));
  geometry.setAttribute('uv', new BufferAttribute(uvs, 2));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function makeVehicleModel(kind: VehicleModelKind, accent = 0x3aa0ff): Group {
  if (kind === 'multirotor') return makeMultirotor(accent);
  if (kind === 'vtol') return makeVtol(accent);
  if (kind === 'generic') return makeGenericVehicle(accent);
  return makeFixedWing(accent);
}

function makeFixedWing(accent = 0x3aa0ff): Group {
  const group = new Group();
  const fuselage = new Mesh(new CapsuleGeometry(3.6, 24, 8, 16), new MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.38, metalness: 0.08 }));
  fuselage.rotation.z = Math.PI / 2;
  group.add(fuselage);
  group.add(new Mesh(new BoxGeometry(5, 46, 1.1), new MeshStandardMaterial({ color: accent, roughness: 0.45 })));
  const tail = new Mesh(new BoxGeometry(4, 15, 5), new MeshStandardMaterial({ color: 0xffc857, roughness: 0.5 }));
  tail.position.x = -11;
  group.add(tail);
  const surfaceMaterial = new MeshStandardMaterial({ color: 0x1b2632, roughness: 0.52 });
  const leftAileron = new Mesh(new BoxGeometry(2.6, 10, 0.55), surfaceMaterial);
  leftAileron.name = 'aileron-left';
  leftAileron.position.set(-1, 16.5, -0.2);
  group.add(leftAileron);
  const rightAileron = new Mesh(new BoxGeometry(2.6, 10, 0.55), surfaceMaterial);
  rightAileron.name = 'aileron-right';
  rightAileron.position.set(-1, -16.5, -0.2);
  group.add(rightAileron);
  const elevator = new Mesh(new BoxGeometry(2.2, 13, 0.5), surfaceMaterial);
  elevator.name = 'elevator';
  elevator.position.set(-14, 0, 1.7);
  group.add(elevator);
  const rudder = new Mesh(new BoxGeometry(0.6, 0.8, 5.8), surfaceMaterial);
  rudder.name = 'rudder';
  rudder.position.set(-13.4, 0, 4.3);
  group.add(rudder);
  const nose = new Mesh(new ConeGeometry(3.7, 8, 24), new MeshStandardMaterial({ color: 0xfffbf0, roughness: 0.36 }));
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 16;
  group.add(nose);
  return group;
}

function makeAttitudeRings(): Group {
  const group = new Group();
  const ringGeometry = new TorusGeometry(34, 0.32, 8, 96);
  const roll = new Mesh(ringGeometry, new MeshStandardMaterial({ color: 0x0a84ff, emissive: 0x06264a, roughness: 0.4 }));
  roll.rotation.y = Math.PI / 2;
  const pitch = new Mesh(ringGeometry, new MeshStandardMaterial({ color: 0x64d2ff, emissive: 0x123342, roughness: 0.4 }));
  pitch.rotation.x = Math.PI / 2;
  const yaw = new Mesh(ringGeometry, new MeshStandardMaterial({ color: 0xffd60a, emissive: 0x443800, roughness: 0.4 }));
  group.add(roll, pitch, yaw);
  group.visible = false;
  return group;
}

function makeMultirotor(accent = 0x3aa0ff): Group {
  const group = new Group();
  const body = new Mesh(new BoxGeometry(12, 8, 3), new MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.42 }));
  group.add(body);
  const armMaterial = new MeshStandardMaterial({ color: accent, roughness: 0.5 });
  const rotorMaterial = new MeshStandardMaterial({ color: 0xffc857, roughness: 0.32 });
  for (const [x, y] of [[14, 14], [14, -14], [-14, 14], [-14, -14]]) {
    const arm = new Mesh(new BoxGeometry(3, Math.hypot(x, y) * 2, 1), armMaterial);
    arm.rotation.z = Math.atan2(y, x) - Math.PI / 2;
    group.add(arm);
    const rotor = new Mesh(new CylinderGeometry(8, 8, 0.4, 32), rotorMaterial);
    rotor.position.set(x, y, 1.6);
    group.add(rotor);
  }
  const nose = new Mesh(new ConeGeometry(3.2, 7, 20), new MeshStandardMaterial({ color: 0xfffbf0, roughness: 0.36 }));
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 9;
  group.add(nose);
  return group;
}

function makeVtol(accent = 0x3aa0ff): Group {
  const group = makeFixedWing(accent);
  const rotorMaterial = new MeshStandardMaterial({ color: 0xffc857, roughness: 0.35 });
  for (const y of [-18, 18]) {
    const boom = new Mesh(new BoxGeometry(15, 2.4, 1.2), new MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.45 }));
    boom.position.set(3, y, 3);
    group.add(boom);
    const rotor = new Mesh(new CylinderGeometry(6.5, 6.5, 0.5, 32), rotorMaterial);
    rotor.rotation.y = Math.PI / 2;
    rotor.position.set(10, y, 3);
    group.add(rotor);
  }
  return group;
}

function makeGenericVehicle(accent = 0x3aa0ff): Group {
  const group = new Group();
  const body = new Mesh(new BoxGeometry(16, 10, 8), new MeshStandardMaterial({ color: accent, roughness: 0.48 }));
  group.add(body);
  const nose = new Mesh(new ConeGeometry(5, 10, 24), new MeshStandardMaterial({ color: 0xffc857, roughness: 0.4 }));
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 12;
  group.add(nose);
  return group;
}

function drawRadar(canvas: HTMLCanvasElement, shell: HTMLElement): void {
  const ctx = canvas.getContext('2d')!;
  const size = canvas.width;
  ctx.clearRect(0, 0, size, size);
  ctx.strokeStyle = 'rgba(102, 224, 163, 0.45)';
  ctx.fillStyle = getComputedStyle(shell).getPropertyValue('--panel-bg').trim() || '#101922';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(size / 2, size / 2, size / 2 - 7, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  for (const ring of [0.33, 0.66]) {
    ctx.beginPath();
    ctx.arc(size / 2, size / 2, (size / 2 - 7) * ring, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.strokeStyle = 'rgba(231, 238, 243, 0.32)';
  ctx.beginPath();
  ctx.moveTo(size / 2, 10);
  ctx.lineTo(size / 2, size - 10);
  ctx.moveTo(10, size / 2);
  ctx.lineTo(size - 10, size / 2);
  ctx.stroke();
  ctx.fillStyle = '#ffc857';
  ctx.beginPath();
  ctx.moveTo(size / 2, 21);
  ctx.lineTo(size / 2 - 5, 33);
  ctx.lineTo(size / 2 + 5, 33);
  ctx.closePath();
  ctx.fill();
}

function drawOrtho(canvas: HTMLCanvasElement, shell: HTMLElement, points: readonly TrailPoint[], lastPoint: TrailPoint | null): void {
  const ctx = canvas.getContext('2d')!;
  const size = canvas.width;
  ctx.clearRect(0, 0, size, size);
  ctx.fillStyle = getComputedStyle(shell).getPropertyValue('--panel-bg').trim() || '#101922';
  ctx.fillRect(0, 0, size, size);
  ctx.strokeStyle = 'rgba(138, 161, 178, 0.22)';
  for (let i = 20; i < size; i += 20) {
    ctx.beginPath();
    ctx.moveTo(i, 0);
    ctx.lineTo(i, size);
    ctx.moveTo(0, i);
    ctx.lineTo(size, i);
    ctx.stroke();
  }
  const center = lastPoint ?? { eastM: 0, northM: 0, upM: 0, timestampMs: 0 };
  ctx.strokeStyle = '#66e0a3';
  ctx.lineWidth = 2;
  ctx.beginPath();
  points.forEach((point, index) => {
    const x = size / 2 + (point.eastM - center.eastM) * 2.2;
    const y = size / 2 - (point.northM - center.northM) * 2.2;
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
  ctx.fillStyle = '#ffc857';
  ctx.beginPath();
  ctx.arc(size / 2, size / 2, 5, 0, Math.PI * 2);
  ctx.fill();
}
