import type { SessionEvent, SessionSnapshotMessage, VehicleStateMessage } from './state';
import {
  AmbientLight,
  BufferAttribute,
  BufferGeometry,
  Color,
  DirectionalLight,
  Line,
  LineBasicMaterial,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  PerspectiveCamera,
  Scene,
  SphereGeometry,
  WebGLRenderer
} from 'three';

export type MapViewState = {
  scale: number;
  panEastM: number;
  panNorthM: number;
  followSelected: boolean;
  mode: MapMode;
};

type ScreenPoint = { x: number; y: number };
type LocalPoint = { eastM: number; northM: number; upM?: number | null };
export type MapMode = '2d' | 'terrain-2d' | 'terrain-3d';
export type TerrainSample = LocalPoint & { terrainHeightM: number; currentHeightM: number | null };
export type TerrainModel = {
  center: LocalPoint;
  samples: TerrainSample[];
  minTerrainM: number;
  maxTerrainM: number;
  spacingM: number;
};

const view: MapViewState = { scale: 2, panEastM: 0, panNorthM: 0, followSelected: true, mode: '2d' };
let bound = false;
let dragging = false;
let lastDrag: ScreenPoint | null = null;
let terrain3d: TerrainRenderer | null = null;

export function mapWorldToScreen(point: LocalPoint, center: LocalPoint, state: MapViewState, width: number, height: number): ScreenPoint {
  return {
    x: width / 2 + (point.eastM - center.eastM + state.panEastM) * state.scale,
    y: height / 2 - (point.northM - center.northM + state.panNorthM) * state.scale
  };
}

export function mapScreenToWorld(point: ScreenPoint, center: LocalPoint, state: MapViewState, width: number, height: number): LocalPoint {
  return {
    eastM: center.eastM + (point.x - width / 2) / state.scale - state.panEastM,
    northM: center.northM - (point.y - height / 2) / state.scale - state.panNorthM
  };
}

export function selectedMapVehicle(snapshot: SessionSnapshotMessage): VehicleStateMessage | null {
  return snapshot.vehicles.find((vehicle) => vehicle.id === snapshot.selectedVehicleId) ?? snapshot.vehicles[0] ?? null;
}

export function homePointFromVehicle(vehicle: VehicleStateMessage): LocalPoint | null {
  const home = vehicle.home;
  if (home && vehicle.globalPosition.originLatDeg !== null && vehicle.globalPosition.originLonDeg !== null) {
    return geoToLocal(home.latDeg, home.lonDeg, home.altitudeM, vehicle.globalPosition.originLatDeg, vehicle.globalPosition.originLonDeg, vehicle.globalPosition.originAltitudeM ?? home.altitudeM);
  }
  if (vehicle.globalPosition.originLatDeg !== null && vehicle.globalPosition.originLonDeg !== null) {
    return { eastM: 0, northM: 0, upM: 0 };
  }
  return null;
}

export function geofenceLocalPoints(vehicle: VehicleStateMessage): { id: string; inclusion: boolean; points: LocalPoint[]; radiusM?: number }[] {
  if (!vehicle.geofences || vehicle.globalPosition.originLatDeg === null || vehicle.globalPosition.originLonDeg === null) return [];
  const originAlt = vehicle.globalPosition.originAltitudeM ?? 0;
  return vehicle.geofences.map((zone) => {
    if (zone.kind === 'circle') {
      return {
        id: zone.id,
        inclusion: zone.inclusion,
        radiusM: zone.radiusM,
        points: [geoToLocal(zone.center.latDeg, zone.center.lonDeg, originAlt, vehicle.globalPosition.originLatDeg!, vehicle.globalPosition.originLonDeg!, originAlt)]
      };
    }
    return {
      id: zone.id,
      inclusion: zone.inclusion,
      points: zone.vertices.map((vertex) => geoToLocal(vertex.latDeg, vertex.lonDeg, originAlt, vehicle.globalPosition.originLatDeg!, vehicle.globalPosition.originLonDeg!, originAlt))
    };
  });
}

export function rallyLocalPoints(vehicle: VehicleStateMessage): LocalPoint[] {
  if (!vehicle.rallyPoints || vehicle.globalPosition.originLatDeg === null || vehicle.globalPosition.originLonDeg === null) return [];
  const originAlt = vehicle.globalPosition.originAltitudeM ?? 0;
  return vehicle.rallyPoints.map((point) => geoToLocal(point.latDeg, point.lonDeg, point.altitudeM ?? originAlt, vehicle.globalPosition.originLatDeg!, vehicle.globalPosition.originLonDeg!, originAlt));
}

export function setMapMode(mode: MapMode): void {
  view.mode = mode;
}

export function mapMode(): MapMode {
  return view.mode;
}

export function terrainModelFromVehicle(vehicle: VehicleStateMessage | null): TerrainModel | null {
  if (!vehicle?.terrain || vehicle.terrain.latDeg === null || vehicle.terrain.lonDeg === null || vehicle.terrain.terrainHeightM === null) return null;
  const originLat = vehicle.globalPosition.originLatDeg ?? vehicle.home?.latDeg ?? vehicle.terrain.latDeg;
  const originLon = vehicle.globalPosition.originLonDeg ?? vehicle.home?.lonDeg ?? vehicle.terrain.lonDeg;
  const originAlt = vehicle.globalPosition.originAltitudeM ?? vehicle.home?.altitudeM ?? vehicle.terrain.terrainHeightM;
  const report = geoToLocal(vehicle.terrain.latDeg, vehicle.terrain.lonDeg, vehicle.terrain.terrainHeightM, originLat, originLon, originAlt);
  const spacing = Math.max(5, vehicle.terrain.spacingM ?? 50);
  const samples: TerrainSample[] = [];
  for (let north = -2; north <= 2; north += 1) {
    for (let east = -2; east <= 2; east += 1) {
      const ripple = Math.sin((east + 2) * 0.9) * 2.5 + Math.cos((north - 1) * 0.7) * 2;
      samples.push({
        eastM: report.eastM + east * spacing,
        northM: report.northM + north * spacing,
        upM: (report.upM ?? 0) + ripple,
        terrainHeightM: vehicle.terrain.terrainHeightM + ripple,
        currentHeightM: vehicle.terrain.currentHeightM
      });
    }
  }
  const heights = samples.map((sample) => sample.terrainHeightM);
  return {
    center: report,
    samples,
    minTerrainM: Math.min(...heights),
    maxTerrainM: Math.max(...heights),
    spacingM: spacing
  };
}

export function bindMapControls(snapshotProvider: () => SessionSnapshotMessage | null): void {
  if (bound) return;
  bound = true;
  const canvas = document.querySelector<HTMLCanvasElement>('#map-canvas');
  if (!canvas) return;
  canvas.addEventListener('pointerdown', (event) => {
    dragging = true;
    lastDrag = { x: event.clientX, y: event.clientY };
    view.followSelected = false;
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!dragging || !lastDrag) return;
    view.panEastM += (event.clientX - lastDrag.x) / view.scale;
    view.panNorthM -= (event.clientY - lastDrag.y) / view.scale;
    lastDrag = { x: event.clientX, y: event.clientY };
    redraw(snapshotProvider);
  });
  canvas.addEventListener('pointerup', (event) => {
    dragging = false;
    lastDrag = null;
    canvas.releasePointerCapture(event.pointerId);
  });
  canvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    const before = view.scale;
    const factor = event.deltaY < 0 ? 1.18 : 1 / 1.18;
    view.scale = Math.max(0.2, Math.min(18, view.scale * factor));
    const rect = canvas.getBoundingClientRect();
    const snapshot = snapshotProvider();
    const selected = snapshot ? selectedMapVehicle(snapshot) : null;
    const center = selected ? pointFromVehicle(selected) ?? { eastM: 0, northM: 0 } : { eastM: 0, northM: 0 };
    const cursor = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    const beforeWorld = mapScreenToWorld(cursor, center, { ...view, scale: before }, canvas.width, canvas.height);
    const afterWorld = mapScreenToWorld(cursor, center, view, canvas.width, canvas.height);
    view.panEastM += afterWorld.eastM - beforeWorld.eastM;
    view.panNorthM += afterWorld.northM - beforeWorld.northM;
    view.followSelected = false;
    redraw(snapshotProvider);
  }, { passive: false });
  document.querySelector<HTMLButtonElement>('#map-focus')?.addEventListener('click', () => {
    view.panEastM = 0;
    view.panNorthM = 0;
    view.followSelected = true;
    redraw(snapshotProvider);
  });
  document.querySelector<HTMLButtonElement>('#map-zoom-in')?.addEventListener('click', () => {
    view.scale = Math.min(18, view.scale * 1.25);
    redraw(snapshotProvider);
  });
  document.querySelector<HTMLButtonElement>('#map-zoom-out')?.addEventListener('click', () => {
    view.scale = Math.max(0.2, view.scale / 1.25);
    redraw(snapshotProvider);
  });
  document.querySelectorAll<HTMLButtonElement>('[data-map-mode]').forEach((button) => {
    button.addEventListener('click', () => {
      const mode = button.dataset.mapMode as MapMode;
      setMapMode(mode);
      document.querySelectorAll<HTMLButtonElement>('[data-map-mode]').forEach((candidate) => candidate.classList.toggle('active', candidate === button));
      redraw(snapshotProvider);
    });
  });
}

export function drawMap(snapshot: SessionSnapshotMessage): void {
  const canvas = document.querySelector<HTMLCanvasElement>('#map-canvas')!;
  const terrainCanvas = document.querySelector<HTMLCanvasElement>('#terrain-canvas');
  canvas.classList.toggle('hidden', view.mode === 'terrain-3d');
  terrainCanvas?.classList.toggle('hidden', view.mode !== 'terrain-3d');
  if (view.mode === 'terrain-3d' && terrainCanvas) {
    drawTerrain3d(terrainCanvas, snapshot);
    return;
  }
  resizeCanvas(canvas);
  const ctx = canvas.getContext('2d')!;
  const width = canvas.width;
  const height = canvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#0b1116';
  ctx.fillRect(0, 0, width, height);
  const centerVehicle = selectedMapVehicle(snapshot);
  const center = view.followSelected ? pointFromVehicle(centerVehicle) ?? { eastM: 0, northM: 0 } : { eastM: 0, northM: 0 };
  if (view.mode === 'terrain-2d') drawTerrain2d(ctx, centerVehicle, center, width, height);
  drawGrid(ctx, center, width, height);
  drawGeofences(ctx, snapshot, center, width, height);
  drawMission(ctx, snapshot, center, width, height);
  drawRally(ctx, snapshot, center, width, height);
  drawHome(ctx, snapshot, center, width, height);
  for (const vehicle of snapshot.vehicles) {
    drawTrail(ctx, vehicle, center, width, height, vehicle.id === snapshot.selectedVehicleId);
  }
  drawEvents(ctx, snapshot.events, center, width, height);
  drawScale(ctx, width, height);
}

function drawTerrain2d(ctx: CanvasRenderingContext2D, vehicle: VehicleStateMessage | null, center: LocalPoint, width: number, height: number): void {
  const model = terrainModelFromVehicle(vehicle);
  if (!model) return;
  const span = Math.max(1, model.maxTerrainM - model.minTerrainM);
  for (const sample of model.samples) {
    const screen = mapWorldToScreen(sample, center, view, width, height);
    const normalized = (sample.terrainHeightM - model.minTerrainM) / span;
    ctx.fillStyle = `rgba(${Math.round(44 + normalized * 120)}, ${Math.round(125 + normalized * 80)}, ${Math.round(92 - normalized * 30)}, 0.26)`;
    ctx.fillRect(screen.x - (model.spacingM * view.scale) / 2, screen.y - (model.spacingM * view.scale) / 2, model.spacingM * view.scale, model.spacingM * view.scale);
  }
  const report = mapWorldToScreen(model.center, center, view, width, height);
  ctx.strokeStyle = '#f4d35e';
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(report.x, report.y, 9, 0, Math.PI * 2);
  ctx.stroke();
}

function drawTerrain3d(canvas: HTMLCanvasElement, snapshot: SessionSnapshotMessage): void {
  resizeCanvas(canvas);
  terrain3d ??= new TerrainRenderer(canvas);
  terrain3d.render(snapshot);
}

function drawGeofences(ctx: CanvasRenderingContext2D, snapshot: SessionSnapshotMessage, center: LocalPoint, width: number, height: number): void {
  const vehicle = selectedMapVehicle(snapshot);
  if (!vehicle) return;
  for (const zone of geofenceLocalPoints(vehicle)) {
    ctx.strokeStyle = zone.inclusion ? 'rgba(102, 224, 163, 0.8)' : 'rgba(255, 107, 122, 0.8)';
    ctx.fillStyle = zone.inclusion ? 'rgba(102, 224, 163, 0.08)' : 'rgba(255, 107, 122, 0.08)';
    ctx.lineWidth = 1.5;
    if (zone.radiusM !== undefined && zone.points[0]) {
      const screen = mapWorldToScreen(zone.points[0], center, view, width, height);
      ctx.beginPath();
      ctx.arc(screen.x, screen.y, zone.radiusM * view.scale, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    } else if (zone.points.length >= 3) {
      ctx.beginPath();
      zone.points.forEach((point, index) => {
        const screen = mapWorldToScreen(point, center, view, width, height);
        if (index === 0) ctx.moveTo(screen.x, screen.y);
        else ctx.lineTo(screen.x, screen.y);
      });
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
    }
  }
}

function drawRally(ctx: CanvasRenderingContext2D, snapshot: SessionSnapshotMessage, center: LocalPoint, width: number, height: number): void {
  const vehicle = selectedMapVehicle(snapshot);
  if (!vehicle) return;
  for (const point of rallyLocalPoints(vehicle)) {
    const screen = mapWorldToScreen(point, center, view, width, height);
    ctx.strokeStyle = '#e464ff';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(screen.x, screen.y - 7);
    ctx.lineTo(screen.x + 7, screen.y);
    ctx.lineTo(screen.x, screen.y + 7);
    ctx.lineTo(screen.x - 7, screen.y);
    ctx.closePath();
    ctx.stroke();
  }
}

function redraw(snapshotProvider: () => SessionSnapshotMessage | null): void {
  drawMap(snapshotProvider() ?? emptyMapSnapshot());
}

function emptyMapSnapshot(): SessionSnapshotMessage {
  return { type: 'session_snapshot', vehicles: [], selectedVehicleId: null, messages: [], events: [], packetCount: 0, decodedCount: 0 };
}

function resizeCanvas(canvas: HTMLCanvasElement): void {
  const rect = canvas.getBoundingClientRect();
  const ratio = Math.max(1, Math.min(2, window.devicePixelRatio || 1));
  const width = Math.max(1, Math.floor(rect.width * ratio));
  const height = Math.max(1, Math.floor(rect.height * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
}

function drawGrid(ctx: CanvasRenderingContext2D, center: LocalPoint, width: number, height: number): void {
  const spacingM = gridSpacingM(view.scale);
  const origin = mapWorldToScreen({ eastM: 0, northM: 0 }, center, view, width, height);
  ctx.strokeStyle = 'rgba(138, 161, 178, 0.18)';
  ctx.lineWidth = 1;
  for (let x = origin.x % (spacingM * view.scale); x <= width; x += spacingM * view.scale) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
  for (let y = origin.y % (spacingM * view.scale); y <= height; y += spacingM * view.scale) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
}

function drawTrail(ctx: CanvasRenderingContext2D, vehicle: VehicleStateMessage, center: LocalPoint, width: number, height: number, selected: boolean): void {
  const trail = vehicle.trail ?? [];
  strokePath(ctx, trail, center, width, height, selected ? '#66e0a3' : 'rgba(58, 160, 255, 0.55)', selected ? 2.5 : 1.4);
  const point = pointFromVehicle(vehicle);
  if (!point) return;
  const screen = mapWorldToScreen(point, center, view, width, height);
  ctx.fillStyle = selected ? '#ffc857' : '#3aa0ff';
  ctx.beginPath();
  ctx.arc(screen.x, screen.y, selected ? 7 : 5, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = '#e7eef3';
  ctx.font = `${12 * (window.devicePixelRatio || 1)}px Inter, sans-serif`;
  ctx.fillText(vehicle.id ?? `${vehicle.systemId}:${vehicle.componentId}`, screen.x + 10, screen.y - 10);
}

function drawEvents(ctx: CanvasRenderingContext2D, events: readonly SessionEvent[], center: LocalPoint, width: number, height: number): void {
  for (const event of events.slice().reverse()) {
    if (!event.position) continue;
    const screen = mapWorldToScreen(event.position, center, view, width, height);
    if (screen.x < -10 || screen.x > width + 10 || screen.y < -10 || screen.y > height + 10) continue;
    ctx.fillStyle = event.level === 'warning' ? '#ffc857' : event.level === 'error' ? '#ff6b7a' : '#3aa0ff';
    ctx.beginPath();
    ctx.arc(screen.x, screen.y, 4, 0, Math.PI * 2);
    ctx.fill();
  }
}

function drawHome(ctx: CanvasRenderingContext2D, snapshot: SessionSnapshotMessage, center: LocalPoint, width: number, height: number): void {
  const vehicle = selectedMapVehicle(snapshot);
  if (!vehicle) return;
  const home = homePointFromVehicle(vehicle);
  if (!home) return;
  const screen = mapWorldToScreen(home, center, view, width, height);
  ctx.strokeStyle = '#ffc857';
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(screen.x - 7, screen.y + 7);
  ctx.lineTo(screen.x, screen.y - 7);
  ctx.lineTo(screen.x + 7, screen.y + 7);
  ctx.closePath();
  ctx.stroke();
}

function drawMission(ctx: CanvasRenderingContext2D, snapshot: SessionSnapshotMessage, center: LocalPoint, width: number, height: number): void {
  const vehicle = selectedMapVehicle(snapshot);
  if (!vehicle?.mission?.waypoints || vehicle.globalPosition.originLatDeg === null || vehicle.globalPosition.originLonDeg === null) return;
  const originAlt = vehicle.globalPosition.originAltitudeM ?? 0;
  const points = vehicle.mission.waypoints.map((waypoint) => geoToLocal(waypoint.latDeg, waypoint.lonDeg, waypoint.altitudeM ?? originAlt, vehicle.globalPosition.originLatDeg!, vehicle.globalPosition.originLonDeg!, originAlt));
  strokePath(ctx, points, center, width, height, 'rgba(255, 200, 87, 0.78)', 1.8);
  points.forEach((point, index) => {
    const seq = vehicle.mission?.waypoints?.[index]?.seq ?? index;
    const screen = mapWorldToScreen(point, center, view, width, height);
    ctx.fillStyle = seq === vehicle.mission?.activeSeq ? '#ffc857' : '#0b1116';
    ctx.strokeStyle = '#ffc857';
    ctx.beginPath();
    ctx.arc(screen.x, screen.y, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  });
}

function strokePath(ctx: CanvasRenderingContext2D, points: readonly LocalPoint[], center: LocalPoint, width: number, height: number, color: string, lineWidth: number): void {
  if (points.length === 0) return;
  ctx.strokeStyle = color;
  ctx.lineWidth = lineWidth;
  ctx.beginPath();
  points.forEach((point, index) => {
    const screen = mapWorldToScreen(point, center, view, width, height);
    if (index === 0) ctx.moveTo(screen.x, screen.y);
    else ctx.lineTo(screen.x, screen.y);
  });
  ctx.stroke();
}

function drawScale(ctx: CanvasRenderingContext2D, width: number, height: number): void {
  const meters = gridSpacingM(view.scale);
  const pixels = meters * view.scale;
  ctx.strokeStyle = '#e7eef3';
  ctx.fillStyle = '#e7eef3';
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(22, height - 28);
  ctx.lineTo(22 + pixels, height - 28);
  ctx.stroke();
  ctx.font = `${12 * (window.devicePixelRatio || 1)}px Inter, sans-serif`;
  ctx.fillText(`${meters} m`, 22, height - 36);
}

function gridSpacingM(scale: number): number {
  if (scale > 8) return 5;
  if (scale > 3) return 10;
  if (scale > 1.2) return 25;
  if (scale > 0.5) return 50;
  return 100;
}

class TerrainRenderer {
  private readonly renderer: WebGLRenderer;
  private readonly scene = new Scene();
  private readonly camera = new PerspectiveCamera(52, 1, 0.1, 5000);

  constructor(private readonly canvas: HTMLCanvasElement) {
    this.renderer = new WebGLRenderer({ canvas, antialias: true, alpha: false });
    this.scene.background = new Color('#0b1116');
    this.scene.add(new AmbientLight(0xffffff, 0.62));
    const sun = new DirectionalLight(0xffffff, 1.1);
    sun.position.set(-120, 180, 90);
    this.scene.add(sun);
  }

  render(snapshot: SessionSnapshotMessage): void {
    this.renderer.setSize(this.canvas.width, this.canvas.height, false);
    this.camera.aspect = this.canvas.width / Math.max(1, this.canvas.height);
    this.camera.updateProjectionMatrix();
    this.scene.children = this.scene.children.slice(0, 2);
    const vehicle = selectedMapVehicle(snapshot);
    const model = terrainModelFromVehicle(vehicle);
    const center = view.followSelected ? pointFromVehicle(vehicle) ?? { eastM: 0, northM: 0, upM: 0 } : { eastM: 0, northM: 0, upM: 0 };
    if (model) this.addTerrain(model);
    this.addOverlays(snapshot, center);
    this.camera.position.set(center.eastM - 170 / view.scale, 130, -center.northM + 210 / view.scale);
    this.camera.lookAt(center.eastM, 0, -center.northM);
    this.renderer.render(this.scene, this.camera);
  }

  private addTerrain(model: TerrainModel): void {
    const geometry = new BufferGeometry();
    const vertices: number[] = [];
    const colors: number[] = [];
    const span = Math.max(1, model.maxTerrainM - model.minTerrainM);
    for (const sample of model.samples) {
      vertices.push(sample.eastM, sample.upM ?? 0, -sample.northM);
      const normalized = (sample.terrainHeightM - model.minTerrainM) / span;
      colors.push(0.18 + normalized * 0.24, 0.48 + normalized * 0.28, 0.32 - normalized * 0.1);
    }
    const indices: number[] = [];
    const width = 5;
    for (let row = 0; row < width - 1; row += 1) {
      for (let col = 0; col < width - 1; col += 1) {
        const a = row * width + col;
        indices.push(a, a + 1, a + width, a + 1, a + width + 1, a + width);
      }
    }
    geometry.setAttribute('position', new BufferAttribute(new Float32Array(vertices), 3));
    geometry.setAttribute('color', new BufferAttribute(new Float32Array(colors), 3));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();
    this.scene.add(new Mesh(geometry, new MeshStandardMaterial({ vertexColors: true, roughness: 0.86, metalness: 0 })));
  }

  private addOverlays(snapshot: SessionSnapshotMessage, center: LocalPoint): void {
    const selected = selectedMapVehicle(snapshot);
    if (selected) {
      for (const point of [homePointFromVehicle(selected), ...rallyLocalPoints(selected)].filter((point): point is LocalPoint => Boolean(point))) {
        this.addMarker(point, '#ffc857', 3.4);
      }
      const mission = missionLocalPoints(selected);
      this.addLine(mission, '#ffc857');
    }
    for (const vehicle of snapshot.vehicles) {
      this.addLine(vehicle.trail ?? [], vehicle.id === snapshot.selectedVehicleId ? '#66e0a3' : '#3aa0ff');
      const point = pointFromVehicle(vehicle);
      if (point) this.addMarker(point, vehicle.id === snapshot.selectedVehicleId ? '#ffc857' : '#3aa0ff', vehicle.id === snapshot.selectedVehicleId ? 4.5 : 3);
    }
    this.addMarker(center, '#e7eef3', 1.8);
  }

  private addMarker(point: LocalPoint, color: string, radius: number): void {
    const marker = new Mesh(new SphereGeometry(radius, 16, 10), new MeshBasicMaterial({ color }));
    marker.position.set(point.eastM, point.upM ?? 0, -point.northM);
    this.scene.add(marker);
  }

  private addLine(points: readonly LocalPoint[], color: string): void {
    if (points.length < 2) return;
    const geometry = new BufferGeometry();
    geometry.setAttribute('position', new BufferAttribute(new Float32Array(points.flatMap((point) => [point.eastM, point.upM ?? 0, -point.northM])), 3));
    this.scene.add(new Line(geometry, new LineBasicMaterial({ color, linewidth: 2 })));
  }
}

function pointFromVehicle(vehicle: VehicleStateMessage | null): LocalPoint | null {
  if (!vehicle || vehicle.localPosition.eastM === null || vehicle.localPosition.northM === null) return null;
  return { eastM: vehicle.localPosition.eastM, northM: vehicle.localPosition.northM, upM: vehicle.localPosition.upM };
}

function missionLocalPoints(vehicle: VehicleStateMessage): LocalPoint[] {
  if (!vehicle.mission?.waypoints || vehicle.globalPosition.originLatDeg === null || vehicle.globalPosition.originLonDeg === null) return [];
  const originAlt = vehicle.globalPosition.originAltitudeM ?? 0;
  return vehicle.mission.waypoints.map((waypoint) => geoToLocal(waypoint.latDeg, waypoint.lonDeg, waypoint.altitudeM ?? originAlt, vehicle.globalPosition.originLatDeg!, vehicle.globalPosition.originLonDeg!, originAlt));
}

function geoToLocal(latDeg: number, lonDeg: number, altitudeM: number, originLatDeg: number, originLonDeg: number, originAltitudeM: number): LocalPoint {
  const earthRadiusM = 6378137;
  const lat0Rad = (originLatDeg * Math.PI) / 180;
  return {
    northM: (((latDeg - originLatDeg) * Math.PI) / 180) * earthRadiusM,
    eastM: (((lonDeg - originLonDeg) * Math.PI) / 180) * earthRadiusM * Math.cos(lat0Rad),
    upM: altitudeM - originAltitudeM
  };
}
