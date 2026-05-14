import type { SessionEvent, SessionSnapshotMessage, VehicleStateMessage } from './state';

export type MapViewState = {
  scale: number;
  panEastM: number;
  panNorthM: number;
  followSelected: boolean;
};

type ScreenPoint = { x: number; y: number };
type LocalPoint = { eastM: number; northM: number; upM?: number | null };

const view: MapViewState = { scale: 2, panEastM: 0, panNorthM: 0, followSelected: true };
let bound = false;
let dragging = false;
let lastDrag: ScreenPoint | null = null;

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
}

export function drawMap(snapshot: SessionSnapshotMessage): void {
  const canvas = document.querySelector<HTMLCanvasElement>('#map-canvas')!;
  resizeCanvas(canvas);
  const ctx = canvas.getContext('2d')!;
  const width = canvas.width;
  const height = canvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#0b1116';
  ctx.fillRect(0, 0, width, height);
  const centerVehicle = selectedMapVehicle(snapshot);
  const center = view.followSelected ? pointFromVehicle(centerVehicle) ?? { eastM: 0, northM: 0 } : { eastM: 0, northM: 0 };
  drawGrid(ctx, center, width, height);
  drawMission(ctx, snapshot, center, width, height);
  drawHome(ctx, snapshot, center, width, height);
  for (const vehicle of snapshot.vehicles) {
    drawTrail(ctx, vehicle, center, width, height, vehicle.id === snapshot.selectedVehicleId);
  }
  drawEvents(ctx, snapshot.events, center, width, height);
  drawScale(ctx, width, height);
}

function redraw(snapshotProvider: () => SessionSnapshotMessage | null): void {
  const snapshot = snapshotProvider();
  if (snapshot) drawMap(snapshot);
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

function pointFromVehicle(vehicle: VehicleStateMessage | null): LocalPoint | null {
  if (!vehicle || vehicle.localPosition.eastM === null || vehicle.localPosition.northM === null) return null;
  return { eastM: vehicle.localPosition.eastM, northM: vehicle.localPosition.northM, upM: vehicle.localPosition.upM };
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
