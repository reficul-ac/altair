import * as THREE from 'three';
import { fmt } from './hud-ui';
import { TrailBuffer, trailPointFromState, type SessionEvent, type VehicleStateMessage, type TrailPoint } from './state';

export const CAMERA_MODES = ['chase', 'orbit', 'top', 'side', 'free'] as const;
export type CameraMode = (typeof CAMERA_MODES)[number];
export type ThemeName = 'grid' | 'rez' | 'snow';
export type VehicleModelKind = 'fixed-wing' | 'multirotor' | 'vtol' | 'generic';

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

export class SceneRenderer {
  readonly trail = new TrailBuffer(2200);
  private readonly renderer: THREE.WebGLRenderer;
  private readonly scene = new THREE.Scene();
  private readonly camera = new THREE.PerspectiveCamera(55, 1, 0.1, 4000);
  private readonly freeControls = { yaw: 0, pitch: -0.35, keys: new Set<string>(), dragging: false, lastX: 0, lastY: 0 };
  private aircraft = makeVehicleModel('fixed-wing');
  private selectedModelKind: VehicleModelKind = 'fixed-wing';
  private readonly otherAircraft = new Map<string, { group: THREE.Group; kind: VehicleModelKind }>();
  private readonly headingCue = new THREE.ArrowHelper(new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0, 8), 42, 0xffc857, 10, 5);
  private readonly trailGeometry = new THREE.BufferGeometry();
  private readonly trailMaterial = new THREE.LineBasicMaterial({ color: 0x66e0a3 });
  private readonly runwayMaterial = new THREE.MeshStandardMaterial({ color: 0x253640, roughness: 0.9 });
  private readonly grid = new THREE.GridHelper(700, 28, 0x497287, 0x20323d);
  private readonly markers = new Map<string, THREE.Mesh>();
  private lastMessage: VehicleStateMessage | null = null;
  private lastPoint: TrailPoint | null = null;
  private frames = 0;
  private fps = 0;
  private lastFpsMs = performance.now();
  private lastFrameMs = 0;
  private lastAnimationMs = performance.now();
  paused = false;
  cameraMode: CameraMode = 'chase';
  theme: ThemeName = 'grid';
  ortho = true;
  debug = false;

  constructor(
    private readonly shell: HTMLElement,
    private readonly canvas: HTMLCanvasElement,
    private readonly radarCanvas: HTMLCanvasElement,
    private readonly orthoCanvas: HTMLCanvasElement
  ) {
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(0x0b1116);
    this.scene.fog = new THREE.Fog(0x0b1116, 420, 1500);
    this.camera.position.set(-80, -140, 80);
    this.camera.up.set(0, 0, 1);
    this.freeControls.yaw = Math.atan2(this.camera.position.x, this.camera.position.y);
    const light = new THREE.DirectionalLight(0xffffff, 2.4);
    light.position.set(-180, -100, 250);
    this.scene.add(light);
    this.scene.add(new THREE.AmbientLight(0x8aa1b2, 1.5));
    this.grid.rotation.x = Math.PI / 2;
    this.scene.add(this.grid);
    const runway = new THREE.Mesh(new THREE.PlaneGeometry(240, 26), this.runwayMaterial);
    runway.rotation.x = Math.PI / 2;
    runway.position.z = -0.04;
    this.scene.add(runway);
    this.scene.add(new THREE.AxesHelper(90));
    this.scene.add(this.aircraft);
    this.scene.add(this.headingCue);
    this.scene.add(new THREE.Line(this.trailGeometry, this.trailMaterial));
    this.bindPointer();
  }

  applyVehicle(message: VehicleStateMessage): void {
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
    }
    this.applyPose(this.aircraft, message);
    this.headingCue.setDirection(new THREE.Vector3(Math.sin(message.attitude.yawRad), Math.cos(message.attitude.yawRad), 0).normalize());
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
    this.scene.fog = new THREE.Fog(next.fog, 420, 1500);
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
    replacement.rotation.copy(previous.rotation);
    this.scene.remove(previous);
    this.aircraft = replacement;
    this.selectedModelKind = kind;
    this.scene.add(this.aircraft);
  }

  private applyPose(group: THREE.Group, message: VehicleStateMessage): void {
    const point = trailPointFromState(message);
    if (point) group.position.set(point.eastM, point.northM, point.upM);
    group.rotation.order = 'ZYX';
    group.rotation.set(message.attitude.pitchRad, -message.attitude.yawRad, -message.attitude.rollRad);
  }

  private updateMarkers(events: SessionEvent[]): void {
    for (const event of events) {
      if (!event.position || this.markers.has(event.id)) continue;
      const marker = new THREE.Mesh(
        new THREE.SphereGeometry(2.8, 12, 12),
        new THREE.MeshStandardMaterial({ color: event.level === 'warning' ? 0xffc857 : event.level === 'error' ? 0xff6b7a : 0x3aa0ff })
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
    this.trailGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    this.trailGeometry.computeBoundingSphere();
  }

  private animate(nowMs: number): void {
    const deltaS = Math.min(0.05, (nowMs - this.lastAnimationMs) / 1000);
    this.lastFrameMs = nowMs - this.lastAnimationMs;
    this.lastAnimationMs = nowMs;
    this.resize();
    this.updateCamera(deltaS);
    drawRadar(this.radarCanvas, this.shell);
    if (this.ortho) drawOrtho(this.orthoCanvas, this.shell, this.trail.values(), this.lastPoint);
    this.updateDebug(nowMs);
    this.renderer.render(this.scene, this.camera);
    requestAnimationFrame((next) => this.animate(next));
  }

  private resize(): void {
    const { clientWidth, clientHeight } = this.canvas.parentElement!;
    this.renderer.setSize(clientWidth, clientHeight, false);
    this.camera.aspect = clientWidth / clientHeight;
    this.camera.updateProjectionMatrix();
  }

  private updateCamera(deltaS: number): void {
    const target = this.aircraft.position;
    if (this.cameraMode !== 'top') {
      this.camera.up.set(0, 0, 1);
    }
    if (this.cameraMode === 'chase') {
      const yaw = this.lastMessage?.attitude.yawRad ?? 0;
      const offset = new THREE.Vector3(-Math.sin(yaw) * 110, -Math.cos(yaw) * 110, 62);
      this.camera.position.lerp(target.clone().add(offset), 0.05);
      this.camera.lookAt(target.x, target.y, target.z + 6);
      return;
    }
    if (this.cameraMode === 'orbit') {
      const orbitYaw = performance.now() * 0.00008;
      const offset = new THREE.Vector3(Math.sin(orbitYaw) * 145, Math.cos(orbitYaw) * 145, 86);
      this.camera.position.lerp(target.clone().add(offset), 0.035);
      this.camera.lookAt(target.x, target.y, target.z + 8);
      return;
    }
    if (this.cameraMode === 'top') {
      this.camera.up.set(0, 1, 0);
      this.camera.position.lerp(target.clone().add(new THREE.Vector3(0, 0, 260)), 0.09);
      this.camera.lookAt(target.x, target.y, target.z);
      return;
    }
    if (this.cameraMode === 'side') {
      const yaw = this.lastMessage?.attitude.yawRad ?? 0;
      const offset = new THREE.Vector3(Math.cos(yaw) * 120, -Math.sin(yaw) * 120, 38);
      this.camera.position.lerp(target.clone().add(offset), 0.06);
      this.camera.lookAt(target.x, target.y, target.z + 4);
      return;
    }
    const boost = this.freeControls.keys.has('ShiftLeft') || this.freeControls.keys.has('ShiftRight') ? 3 : 1;
    const speed = 70 * boost * deltaS;
    const forward = new THREE.Vector3(Math.sin(this.freeControls.yaw), Math.cos(this.freeControls.yaw), 0);
    const right = new THREE.Vector3(forward.y, -forward.x, 0);
    if (this.freeControls.keys.has('KeyW')) this.camera.position.addScaledVector(forward, speed);
    if (this.freeControls.keys.has('KeyS')) this.camera.position.addScaledVector(forward, -speed);
    if (this.freeControls.keys.has('KeyA')) this.camera.position.addScaledVector(right, -speed);
    if (this.freeControls.keys.has('KeyD')) this.camera.position.addScaledVector(right, speed);
    if (this.freeControls.keys.has('KeyQ')) this.camera.position.z -= speed;
    if (this.freeControls.keys.has('KeyE')) this.camera.position.z += speed;
    const look = this.camera.position.clone().add(new THREE.Vector3(Math.sin(this.freeControls.yaw) * Math.cos(this.freeControls.pitch), Math.cos(this.freeControls.yaw) * Math.cos(this.freeControls.pitch), Math.sin(this.freeControls.pitch)));
    this.camera.lookAt(look);
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
    setText('debug-camera', this.cameraMode);
    setText('debug-position', this.lastPoint ? `${fmt(this.lastPoint.eastM)} E / ${fmt(this.lastPoint.northM)} N / ${fmt(this.lastPoint.upM)} U` : '--');
  }

  private bindPointer(): void {
    window.addEventListener('keydown', (event) => this.freeControls.keys.add(event.code));
    window.addEventListener('keyup', (event) => this.freeControls.keys.delete(event.code));
    this.canvas.addEventListener('pointerdown', (event) => {
      if (this.cameraMode !== 'free') return;
      this.freeControls.dragging = true;
      this.freeControls.lastX = event.clientX;
      this.freeControls.lastY = event.clientY;
      this.canvas.setPointerCapture(event.pointerId);
    });
    this.canvas.addEventListener('pointermove', (event) => {
      if (!this.freeControls.dragging) return;
      this.freeControls.yaw -= (event.clientX - this.freeControls.lastX) * 0.005;
      this.freeControls.pitch = Math.max(-1.25, Math.min(1.25, this.freeControls.pitch - (event.clientY - this.freeControls.lastY) * 0.005));
      this.freeControls.lastX = event.clientX;
      this.freeControls.lastY = event.clientY;
    });
    this.canvas.addEventListener('pointerup', (event) => {
      this.freeControls.dragging = false;
      this.canvas.releasePointerCapture(event.pointerId);
    });
  }
}

function setText(id: string, value: string): void {
  document.querySelector<HTMLElement>(`#${id}`)!.textContent = value;
}

function makeVehicleModel(kind: VehicleModelKind, accent = 0x3aa0ff): THREE.Group {
  if (kind === 'multirotor') return makeMultirotor(accent);
  if (kind === 'vtol') return makeVtol(accent);
  if (kind === 'generic') return makeGenericVehicle(accent);
  return makeFixedWing(accent);
}

function makeFixedWing(accent = 0x3aa0ff): THREE.Group {
  const group = new THREE.Group();
  const fuselage = new THREE.Mesh(new THREE.CapsuleGeometry(3.6, 24, 8, 16), new THREE.MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.38, metalness: 0.08 }));
  fuselage.rotation.z = Math.PI / 2;
  group.add(fuselage);
  group.add(new THREE.Mesh(new THREE.BoxGeometry(5, 46, 1.1), new THREE.MeshStandardMaterial({ color: accent, roughness: 0.45 })));
  const tail = new THREE.Mesh(new THREE.BoxGeometry(4, 15, 5), new THREE.MeshStandardMaterial({ color: 0xffc857, roughness: 0.5 }));
  tail.position.x = -11;
  group.add(tail);
  const nose = new THREE.Mesh(new THREE.ConeGeometry(3.7, 8, 24), new THREE.MeshStandardMaterial({ color: 0xfffbf0, roughness: 0.36 }));
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 16;
  group.add(nose);
  return group;
}

function makeMultirotor(accent = 0x3aa0ff): THREE.Group {
  const group = new THREE.Group();
  const body = new THREE.Mesh(new THREE.BoxGeometry(12, 8, 3), new THREE.MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.42 }));
  group.add(body);
  const armMaterial = new THREE.MeshStandardMaterial({ color: accent, roughness: 0.5 });
  const rotorMaterial = new THREE.MeshStandardMaterial({ color: 0xffc857, roughness: 0.32 });
  for (const [x, y] of [[14, 14], [14, -14], [-14, 14], [-14, -14]]) {
    const arm = new THREE.Mesh(new THREE.BoxGeometry(3, Math.hypot(x, y) * 2, 1), armMaterial);
    arm.rotation.z = Math.atan2(y, x) - Math.PI / 2;
    group.add(arm);
    const rotor = new THREE.Mesh(new THREE.CylinderGeometry(8, 8, 0.4, 32), rotorMaterial);
    rotor.position.set(x, y, 1.6);
    group.add(rotor);
  }
  const nose = new THREE.Mesh(new THREE.ConeGeometry(3.2, 7, 20), new THREE.MeshStandardMaterial({ color: 0xfffbf0, roughness: 0.36 }));
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 9;
  group.add(nose);
  return group;
}

function makeVtol(accent = 0x3aa0ff): THREE.Group {
  const group = makeFixedWing(accent);
  const rotorMaterial = new THREE.MeshStandardMaterial({ color: 0xffc857, roughness: 0.35 });
  for (const y of [-18, 18]) {
    const boom = new THREE.Mesh(new THREE.BoxGeometry(15, 2.4, 1.2), new THREE.MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.45 }));
    boom.position.set(3, y, 3);
    group.add(boom);
    const rotor = new THREE.Mesh(new THREE.CylinderGeometry(6.5, 6.5, 0.5, 32), rotorMaterial);
    rotor.rotation.y = Math.PI / 2;
    rotor.position.set(10, y, 3);
    group.add(rotor);
  }
  return group;
}

function makeGenericVehicle(accent = 0x3aa0ff): THREE.Group {
  const group = new THREE.Group();
  const body = new THREE.Mesh(new THREE.BoxGeometry(16, 10, 8), new THREE.MeshStandardMaterial({ color: accent, roughness: 0.48 }));
  group.add(body);
  const nose = new THREE.Mesh(new THREE.ConeGeometry(5, 10, 24), new THREE.MeshStandardMaterial({ color: 0xffc857, roughness: 0.4 }));
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
