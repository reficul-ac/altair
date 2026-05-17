import type { SessionEvent, SessionSnapshotMessage, VehicleStateMessage } from './state';
import { ANIMUS_MAP_PACK_URL, createUnavailableMapPackStatus, normalizeMapPackStatus, type MapPackStatus } from './map-pack';
import maplibregl, { type GeoJSONSource, type LngLatLike, type Map as MapLibreMap, type StyleSpecification } from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { PMTiles, Protocol, type RangeResponse, type Source } from 'pmtiles';
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
type GeoPoint = { lon: number; lat: number; altitudeM?: number | null };
type FeatureGeometry =
  | { type: 'Point'; coordinates: [number, number] }
  | { type: 'LineString'; coordinates: [number, number][] }
  | { type: 'Polygon'; coordinates: [number, number][][] };
type Feature = { type: 'Feature'; geometry: FeatureGeometry; properties: Record<string, string | number | boolean | null> };
type FeatureCollection = { type: 'FeatureCollection'; features: Feature[] };
export type MapMode = 'satellite' | 'terrain-3d';
export type TerrainSample = LocalPoint & { terrainHeightM: number; currentHeightM: number | null };
export type TerrainModel = {
  center: LocalPoint;
  samples: TerrainSample[];
  minTerrainM: number;
  maxTerrainM: number;
  spacingM: number;
};
export type MapOverlayModel = {
  center: GeoPoint | null;
  vehicles: FeatureCollection;
  trails: FeatureCollection;
  mission: FeatureCollection;
  home: FeatureCollection;
  geofences: FeatureCollection;
  rally: FeatureCollection;
  events: FeatureCollection;
};

const view: MapViewState = { scale: 2, panEastM: 0, panNorthM: 0, followSelected: true, mode: 'satellite' };
let bound = false;
let map: MapLibreMap | null = null;
let mapReady = false;
let mapFailed = false;
let latestSnapshot: SessionSnapshotMessage | null = null;
let terrain3d: TerrainRenderer | null = null;
let mapPackStatusPromise: Promise<MapPackStatus> | null = null;
let mapFollowChanged: ((follow: boolean) => void) | null = null;
let registeredMapPackUrl: string | null = null;

const overlaySourceIds = ['vehicles', 'trails', 'mission', 'home', 'geofences', 'rally', 'events'] as const;
const fallbackLocalOrigin = { originLat: 37, originLon: -122, originAlt: 0, originEastM: 0, originNorthM: 0 };

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

export function setMapFollowSelected(followSelected: boolean): void {
  view.followSelected = followSelected;
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
      samples.push({
        eastM: report.eastM + east * spacing,
        northM: report.northM + north * spacing,
        upM: report.upM ?? 0,
        terrainHeightM: vehicle.terrain.terrainHeightM,
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

export function buildMapOverlayModel(snapshot: SessionSnapshotMessage): MapOverlayModel {
  const selected = selectedMapVehicle(snapshot);
  const vehicles: Feature[] = [];
  const trails: Feature[] = [];
  const mission: Feature[] = [];
  const home: Feature[] = [];
  const geofences: Feature[] = [];
  const rally: Feature[] = [];
  const events: Feature[] = [];
  const selectedContext = selected ? geoContextForVehicle(selected) : null;

  for (const vehicle of snapshot.vehicles) {
    const context = geoContextForVehicle(vehicle) ?? selectedContext;
    const vehiclePoint = geoPointFromVehicle(vehicle, context);
    if (vehiclePoint) {
      vehicles.push(pointFeature(vehiclePoint, {
        id: vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`,
        selected: vehicle.id === snapshot.selectedVehicleId
      }));
    }
    const trailPoints = (vehicle.trail ?? []).map((point) => geoPointFromLocal(point, context)).filter((point): point is GeoPoint => Boolean(point));
    if (trailPoints.length >= 2) {
      trails.push(lineFeature(trailPoints, { selected: vehicle.id === snapshot.selectedVehicleId }));
    }
  }

  if (selected) {
    const context = selectedContext;
    const homePoint = geoHomePoint(selected, context);
    if (homePoint) home.push(pointFeature(homePoint, { kind: 'home' }));
    for (const point of selected.rallyPoints ?? []) {
      rally.push(pointFeature({ lat: point.latDeg, lon: point.lonDeg, altitudeM: point.altitudeM }, { id: point.id }));
    }
    const missionPoints = (selected.mission?.waypoints ?? []).map((waypoint) => ({ lat: waypoint.latDeg, lon: waypoint.lonDeg, altitudeM: waypoint.altitudeM }));
    if (missionPoints.length >= 2) mission.push(lineFeature(missionPoints, { activeSeq: selected.mission?.activeSeq ?? null }));
    for (const waypoint of missionPoints) {
      mission.push(pointFeature(waypoint, { kind: 'waypoint' }));
    }
    for (const zone of selected.geofences ?? []) {
      if (zone.kind === 'circle') {
        geofences.push(polygonFeature(circlePolygon({ lat: zone.center.latDeg, lon: zone.center.lonDeg }, zone.radiusM), {
          id: zone.id,
          inclusion: zone.inclusion
        }));
      } else {
        geofences.push(polygonFeature(zone.vertices.map((vertex) => ({ lat: vertex.latDeg, lon: vertex.lonDeg })), {
          id: zone.id,
          inclusion: zone.inclusion
        }));
      }
    }
  }

  for (const event of snapshot.events) {
    const point = geoPointFromEvent(event, selectedContext);
    if (point) events.push(pointFeature(point, { level: event.level, label: event.label }));
  }

  return {
    center: selected ? geoPointFromVehicle(selected, selectedContext) ?? geoHomePoint(selected, selectedContext) : null,
    vehicles: collection(vehicles),
    trails: collection(trails),
    mission: collection(mission),
    home: collection(home),
    geofences: collection(geofences),
    rally: collection(rally),
    events: collection(events)
  };
}

export function bindMapControls(snapshotProvider: () => SessionSnapshotMessage | null, onFollowChanged?: (follow: boolean) => void): void {
  mapFollowChanged = onFollowChanged ?? null;
  if (bound) return;
  bound = true;
  document.querySelector<HTMLButtonElement>('#map-focus')?.addEventListener('click', () => {
    view.panEastM = 0;
    view.panNorthM = 0;
    setFollowSelected(true);
    redraw(snapshotProvider);
  });
  document.querySelector<HTMLButtonElement>('#map-zoom-in')?.addEventListener('click', () => {
    if (view.mode === 'terrain-3d') view.scale = Math.min(18, view.scale * 1.25);
    else map?.zoomIn();
    redraw(snapshotProvider);
  });
  document.querySelector<HTMLButtonElement>('#map-zoom-out')?.addEventListener('click', () => {
    if (view.mode === 'terrain-3d') view.scale = Math.max(0.2, view.scale / 1.25);
    else map?.zoomOut();
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
  latestSnapshot = snapshot;
  const mapContainer = document.querySelector<HTMLElement>('#map-container');
  const overlayCanvas = document.querySelector<HTMLCanvasElement>('#map-overlay-canvas');
  const terrainCanvas = document.querySelector<HTMLCanvasElement>('#terrain-canvas');
  mapContainer?.classList.toggle('hidden', view.mode === 'terrain-3d');
  overlayCanvas?.classList.toggle('hidden', view.mode === 'terrain-3d');
  terrainCanvas?.classList.toggle('hidden', view.mode !== 'terrain-3d');
  if (view.mode === 'terrain-3d' && terrainCanvas) {
    void drawTerrainMap(terrainCanvas, snapshot);
    return;
  }
  void drawSatelliteMap(snapshot);
}

export function refreshMapLayout(): void {
  map?.resize();
  if (latestSnapshot) drawMap(latestSnapshot);
}

async function drawTerrainMap(canvas: HTMLCanvasElement, snapshot: SessionSnapshotMessage): Promise<void> {
  const status = await getMapPackStatus();
  updateMapPackStatus(status);
  if (!status.terrain.available) {
    showMapUnavailable(status.terrain.error ?? 'Terrain DEM PMTiles is missing or unreadable.');
    return;
  }
  hideMapUnavailable();
  drawTerrain3d(canvas, snapshot);
}

function drawTerrain3d(canvas: HTMLCanvasElement, snapshot: SessionSnapshotMessage): void {
  resizeCanvas(canvas);
  terrain3d ??= new TerrainRenderer(canvas);
  terrain3d.render(snapshot);
}

async function drawSatelliteMap(snapshot: SessionSnapshotMessage): Promise<void> {
  const status = await getMapPackStatus();
  updateMapPackStatus(status);
  if (!status.satellite.available) {
    showMapUnavailable(status.satellite.error ?? 'Satellite imagery PMTiles is missing or unreadable.');
    return;
  }
  hideMapUnavailable();
  const mapInstance = await ensureMap(status);
  if (!mapInstance || !mapReady) return;
  const overlays = buildMapOverlayModel(snapshot);
  updateOverlaySources(mapInstance, overlays);
  drawProjectedOverlay(mapInstance, overlays);
  if (view.followSelected && overlays.center) {
    mapInstance.easeTo({ center: [overlays.center.lon, overlays.center.lat], duration: 220, zoom: Math.max(mapInstance.getZoom(), 13) });
  }
}

async function ensureMap(status: MapPackStatus): Promise<MapLibreMap | null> {
  if (map || mapFailed) return map;
  const container = document.querySelector<HTMLElement>('#map-container');
  if (!container) return null;
  try {
    registerMapPack(status);
    map = new maplibregl.Map({
      container,
      style: satelliteStyle(status),
      center: [-122.0, 37.0],
      zoom: 12,
      attributionControl: false,
      dragRotate: false,
      pitchWithRotate: false
    });
    map.on('load', () => {
      mapReady = true;
      if (latestSnapshot) void drawSatelliteMap(latestSnapshot);
    });
    map.on('dragstart', () => setFollowSelected(false));
    map.on('zoomstart', (event) => {
      if (event.originalEvent) setFollowSelected(false);
    });
    map.on('move', () => {
      if (latestSnapshot && map) drawProjectedOverlay(map, buildMapOverlayModel(latestSnapshot));
    });
    map.on('error', (event) => {
      const message = event.error?.message ?? 'MapLibre failed to load the offline map pack.';
      showMapUnavailable(message);
    });
  } catch (error) {
    mapFailed = true;
    showMapUnavailable(error instanceof Error ? error.message : 'MapLibre failed to initialize.');
  }
  return map;
}

function registerMapPack(status: MapPackStatus): void {
  const key = [status.satellite.url, status.terrain.url].join('|');
  if (registeredMapPackUrl === key) return;
  const protocol = new Protocol();
  if (status.satellite.url) protocol.add(new PMTiles(new BundlePmtilesSource(status.satellite.url)));
  if (status.terrain.url) protocol.add(new PMTiles(new BundlePmtilesSource(status.terrain.url)));
  maplibregl.addProtocol('pmtiles', protocol.tile);
  registeredMapPackUrl = key;
}

export function satelliteStyle(status: MapPackStatus): StyleSpecification {
  const empty = collection([]);
  const sources: StyleSpecification['sources'] = {
    satellite: {
      type: 'raster',
      url: `pmtiles://${status.satellite.url ?? ANIMUS_MAP_PACK_URL}`,
      tileSize: 256,
      attribution: status.attribution ?? status.satellite.attribution ?? 'Offline satellite imagery'
    },
    vehicles: { type: 'geojson', data: empty },
    trails: { type: 'geojson', data: empty },
    mission: { type: 'geojson', data: empty },
    home: { type: 'geojson', data: empty },
    geofences: { type: 'geojson', data: empty },
    rally: { type: 'geojson', data: empty },
    events: { type: 'geojson', data: empty }
  };
  if (status.terrain.available && status.terrain.url) {
    sources.terrain = {
      type: 'raster-dem',
      url: `pmtiles://${status.terrain.url}`,
      tileSize: 256,
      encoding: 'terrarium'
    };
  }
  return {
    version: 8,
    sources,
    ...(status.terrain.available ? { terrain: { source: 'terrain', exaggeration: 1 } } : {}),
    layers: [
      { id: 'background', type: 'background', paint: { 'background-color': '#223129' } },
      { id: 'satellite', type: 'raster', source: 'satellite', paint: { 'raster-opacity': 1, 'raster-contrast': 0.08, 'raster-saturation': -0.08 } },
      ...(status.terrain.available ? [{ id: 'terrain-shade', type: 'hillshade' as const, source: 'terrain', paint: { 'hillshade-shadow-color': '#05080b', 'hillshade-highlight-color': '#ffffff', 'hillshade-accent-color': '#000000', 'hillshade-exaggeration': 0.22 } }] : []),
      { id: 'geofence-fill', type: 'fill', source: 'geofences', paint: { 'fill-color': ['case', ['==', ['get', 'inclusion'], true], '#66e0a3', '#ff6b7a'], 'fill-opacity': 0.16 } },
      { id: 'geofence-line', type: 'line', source: 'geofences', paint: { 'line-color': ['case', ['==', ['get', 'inclusion'], true], '#66e0a3', '#ff6b7a'], 'line-width': 2 } },
      { id: 'mission-line', type: 'line', source: 'mission', filter: ['==', ['geometry-type'], 'LineString'], paint: { 'line-color': '#ffc857', 'line-width': 2.4, 'line-opacity': 0.88 } },
      { id: 'trail-line', type: 'line', source: 'trails', paint: { 'line-color': ['case', ['==', ['get', 'selected'], true], '#66e0a3', '#3aa0ff'], 'line-width': ['case', ['==', ['get', 'selected'], true], 3, 1.8], 'line-opacity': 0.82 } },
      { id: 'mission-points', type: 'circle', source: 'mission', filter: ['==', ['geometry-type'], 'Point'], paint: { 'circle-radius': 5, 'circle-color': '#0b1116', 'circle-stroke-color': '#ffc857', 'circle-stroke-width': 2 } },
      { id: 'home-points', type: 'circle', source: 'home', paint: { 'circle-radius': 7, 'circle-color': '#ffc857', 'circle-stroke-color': '#0b1116', 'circle-stroke-width': 2 } },
      { id: 'rally-points', type: 'circle', source: 'rally', paint: { 'circle-radius': 6, 'circle-color': '#e464ff', 'circle-stroke-color': '#0b1116', 'circle-stroke-width': 2 } },
      { id: 'event-points', type: 'circle', source: 'events', paint: { 'circle-radius': 5, 'circle-color': ['match', ['get', 'level'], 'warning', '#ffc857', 'error', '#ff6b7a', '#3aa0ff'], 'circle-stroke-color': '#0b1116', 'circle-stroke-width': 1.5 } },
      { id: 'vehicle-points', type: 'circle', source: 'vehicles', paint: { 'circle-radius': ['case', ['==', ['get', 'selected'], true], 8, 5], 'circle-color': ['case', ['==', ['get', 'selected'], true], '#ffc857', '#3aa0ff'], 'circle-stroke-color': '#0b1116', 'circle-stroke-width': 2 } }
    ]
  };
}

function updateOverlaySources(mapInstance: MapLibreMap, overlays: MapOverlayModel): void {
  for (const id of overlaySourceIds) {
    const source = mapInstance.getSource(id) as GeoJSONSource | undefined;
    source?.setData(overlays[id]);
  }
}

function drawProjectedOverlay(mapInstance: MapLibreMap, overlays: MapOverlayModel): void {
  const canvas = document.querySelector<HTMLCanvasElement>('#map-overlay-canvas');
  if (!canvas) return;
  resizeCanvas(canvas);
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  const rect = canvas.getBoundingClientRect();
  const ratio = canvas.width / Math.max(1, rect.width);
  ctx.save();
  ctx.scale(ratio, ratio);
  drawPolygonFeatures(ctx, mapInstance, overlays.geofences);
  drawLineFeatures(ctx, mapInstance, overlays.trails, (feature) => feature.properties.selected === true ? '#66e0a3' : '#3aa0ff', 2.4);
  drawLineFeatures(ctx, mapInstance, overlays.mission, () => '#ffc857', 2);
  drawPointFeatures(ctx, mapInstance, overlays.mission, () => '#ffc857', 5, true);
  drawPointFeatures(ctx, mapInstance, overlays.home, () => '#ffc857', 7, false);
  drawPointFeatures(ctx, mapInstance, overlays.rally, () => '#e464ff', 6, true);
  drawPointFeatures(ctx, mapInstance, overlays.events, (feature) => feature.properties.level === 'warning' ? '#ffc857' : feature.properties.level === 'error' ? '#ff6b7a' : '#3aa0ff', 5, false);
  drawPointFeatures(ctx, mapInstance, overlays.vehicles, (feature) => feature.properties.selected === true ? '#ffc857' : '#3aa0ff', 8, false);
  if (view.followSelected && latestSnapshot?.vehicles.length) {
    drawFollowMarker(ctx, rect.width / 2, rect.height / 2);
  }
  ctx.restore();
}

function drawFollowMarker(ctx: CanvasRenderingContext2D, x: number, y: number): void {
  ctx.beginPath();
  ctx.arc(x, y, 9, 0, Math.PI * 2);
  ctx.fillStyle = '#ffc857';
  ctx.strokeStyle = '#0b1116';
  ctx.lineWidth = 2;
  ctx.fill();
  ctx.stroke();
}

function drawPolygonFeatures(ctx: CanvasRenderingContext2D, mapInstance: MapLibreMap, collection: FeatureCollection): void {
  for (const feature of collection.features) {
    if (feature.geometry.type !== 'Polygon') continue;
    const color = feature.properties.inclusion === true ? '#66e0a3' : '#ff6b7a';
    ctx.beginPath();
    feature.geometry.coordinates[0]?.forEach((coordinate, index) => {
      const point = mapInstance.project(coordinate as LngLatLike);
      if (index === 0) ctx.moveTo(point.x, point.y);
      else ctx.lineTo(point.x, point.y);
    });
    ctx.closePath();
    ctx.fillStyle = colorWithAlpha(color, 0.14);
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.8;
    ctx.fill();
    ctx.stroke();
  }
}

function drawLineFeatures(ctx: CanvasRenderingContext2D, mapInstance: MapLibreMap, collection: FeatureCollection, color: (feature: Feature) => string, width: number): void {
  for (const feature of collection.features) {
    if (feature.geometry.type !== 'LineString') continue;
    ctx.beginPath();
    feature.geometry.coordinates.forEach((coordinate, index) => {
      const point = mapInstance.project(coordinate as LngLatLike);
      if (index === 0) ctx.moveTo(point.x, point.y);
      else ctx.lineTo(point.x, point.y);
    });
    ctx.strokeStyle = color(feature);
    ctx.lineWidth = width;
    ctx.stroke();
  }
}

function drawPointFeatures(ctx: CanvasRenderingContext2D, mapInstance: MapLibreMap, collection: FeatureCollection, color: (feature: Feature) => string, radius: number, hollow: boolean): void {
  for (const feature of collection.features) {
    if (feature.geometry.type !== 'Point') continue;
    const point = mapInstance.project(feature.geometry.coordinates as LngLatLike);
    ctx.beginPath();
    ctx.arc(point.x, point.y, feature.properties.selected === true ? radius + 1 : radius, 0, Math.PI * 2);
    ctx.fillStyle = hollow ? '#0b1116' : color(feature);
    ctx.strokeStyle = color(feature);
    ctx.lineWidth = 2;
    ctx.fill();
    ctx.stroke();
  }
}

function colorWithAlpha(color: string, alpha: number): string {
  const hex = color.replace('#', '');
  const r = Number.parseInt(hex.slice(0, 2), 16);
  const g = Number.parseInt(hex.slice(2, 4), 16);
  const b = Number.parseInt(hex.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
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

function setFollowSelected(followSelected: boolean): void {
  if (view.followSelected === followSelected) return;
  view.followSelected = followSelected;
  mapFollowChanged?.(followSelected);
}

async function getMapPackStatus(): Promise<MapPackStatus> {
  if (!mapPackStatusPromise) {
    mapPackStatusPromise = window.altairAnimus?.getMapPackStatus
      ? window.altairAnimus.getMapPackStatus().then(normalizeMapPackStatus)
      : probeBrowserMapPackStatus();
  }
  return mapPackStatusPromise;
}

export async function refreshMapPackStatus(): Promise<MapPackStatus> {
  mapPackStatusPromise = null;
  const status = await getMapPackStatus();
  updateMapPackStatus(status);
  return status;
}

async function probeBrowserMapPackStatus(): Promise<MapPackStatus> {
  try {
    const response = await fetch(ANIMUS_MAP_PACK_URL, { headers: { Range: 'bytes=0-1' } });
    if (!response.ok) return createUnavailableMapPackStatus(`HTTP ${response.status}`);
    return normalizeMapPackStatus({
      available: true,
      satellite: { available: true, url: ANIMUS_MAP_PACK_URL, path: ANIMUS_MAP_PACK_URL, label: 'Development satellite placeholder' },
      terrain: { available: true, url: ANIMUS_MAP_PACK_URL, path: ANIMUS_MAP_PACK_URL, label: 'Development terrain placeholder' },
      label: 'Altair bundled development map',
      attribution: 'Generated Altair offline development basemap'
    });
  } catch (error) {
    return createUnavailableMapPackStatus(error instanceof Error ? error.message : 'map pack is missing or unreadable');
  }
}

function updateMapPackStatus(status: MapPackStatus): void {
  const target = document.querySelector<HTMLElement>('#map-pack-status');
  if (target) target.textContent = status.available ? status.label : status.satellite.available ? 'Terrain DEM unavailable' : 'Satellite map unavailable';
}

function showMapUnavailable(detail: string): void {
  document.querySelector<HTMLElement>('#map-unavailable')?.classList.remove('hidden');
  const target = document.querySelector<HTMLElement>('#map-unavailable-detail');
  if (target) target.textContent = detail;
}

function hideMapUnavailable(): void {
  document.querySelector<HTMLElement>('#map-unavailable')?.classList.add('hidden');
}

class BundlePmtilesSource implements Source {
  private bytes: Promise<ArrayBuffer> | null = null;

  constructor(private readonly url: string) {}

  getKey(): string {
    return this.url;
  }

  async getBytes(offset: number, length: number): Promise<RangeResponse> {
    const bytes = await this.read();
    return { data: bytes.slice(offset, offset + length) };
  }

  private async read(): Promise<ArrayBuffer> {
    this.bytes ??= fetch(this.url).then((response) => {
      if (!response.ok) throw new Error(`offline map unavailable: HTTP ${response.status}`);
      return response.arrayBuffer();
    });
    return this.bytes;
  }
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

function geoContextForVehicle(vehicle: VehicleStateMessage): { originLat: number; originLon: number; originAlt: number; originEastM: number; originNorthM: number } | null {
  if (vehicle.globalPosition.originLatDeg !== null && vehicle.globalPosition.originLonDeg !== null) {
    return {
      originLat: vehicle.globalPosition.originLatDeg,
      originLon: vehicle.globalPosition.originLonDeg,
      originAlt: vehicle.globalPosition.originAltitudeM ?? 0,
      originEastM: 0,
      originNorthM: 0
    };
  }
  if (
    vehicle.globalPosition.latDeg !== null &&
    vehicle.globalPosition.lonDeg !== null &&
    vehicle.localPosition.eastM !== null &&
    vehicle.localPosition.northM !== null
  ) {
    return {
      originLat: vehicle.globalPosition.latDeg,
      originLon: vehicle.globalPosition.lonDeg,
      originAlt: vehicle.globalPosition.altitudeM ?? 0,
      originEastM: vehicle.localPosition.eastM,
      originNorthM: vehicle.localPosition.northM
    };
  }
  if (vehicle.home) {
    return {
      originLat: vehicle.home.latDeg,
      originLon: vehicle.home.lonDeg,
      originAlt: vehicle.home.altitudeM,
      originEastM: 0,
      originNorthM: 0
    };
  }
  if (vehicle.localPosition.eastM !== null && vehicle.localPosition.northM !== null) return fallbackLocalOrigin;
  return null;
}

function geoPointFromVehicle(vehicle: VehicleStateMessage, context: ReturnType<typeof geoContextForVehicle>): GeoPoint | null {
  if (vehicle.globalPosition.latDeg !== null && vehicle.globalPosition.lonDeg !== null) {
    return { lat: vehicle.globalPosition.latDeg, lon: vehicle.globalPosition.lonDeg, altitudeM: vehicle.globalPosition.altitudeM };
  }
  const local = pointFromVehicle(vehicle);
  return local ? geoPointFromLocal(local, context) : null;
}

function geoHomePoint(vehicle: VehicleStateMessage, context: ReturnType<typeof geoContextForVehicle>): GeoPoint | null {
  if (vehicle.home) return { lat: vehicle.home.latDeg, lon: vehicle.home.lonDeg, altitudeM: vehicle.home.altitudeM };
  return geoPointFromLocal({ eastM: 0, northM: 0, upM: 0 }, context);
}

function geoPointFromEvent(event: SessionEvent, context: ReturnType<typeof geoContextForVehicle>): GeoPoint | null {
  return event.position ? geoPointFromLocal(event.position, context) : null;
}

function geoPointFromLocal(point: LocalPoint, context: ReturnType<typeof geoContextForVehicle>): GeoPoint | null {
  if (!context) return null;
  const earthRadiusM = 6378137;
  const lat = context.originLat + ((point.northM - context.originNorthM) / earthRadiusM) * (180 / Math.PI);
  const lon = context.originLon + ((point.eastM - context.originEastM) / (earthRadiusM * Math.cos((context.originLat * Math.PI) / 180))) * (180 / Math.PI);
  return { lat, lon, altitudeM: context.originAlt + (point.upM ?? 0) };
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

function circlePolygon(center: GeoPoint, radiusM: number): GeoPoint[] {
  const points: GeoPoint[] = [];
  const earthRadiusM = 6378137;
  for (let index = 0; index < 48; index += 1) {
    const theta = (index / 48) * Math.PI * 2;
    const northM = Math.cos(theta) * radiusM;
    const eastM = Math.sin(theta) * radiusM;
    points.push({
      lat: center.lat + (northM / earthRadiusM) * (180 / Math.PI),
      lon: center.lon + (eastM / (earthRadiusM * Math.cos((center.lat * Math.PI) / 180))) * (180 / Math.PI)
    });
  }
  return points;
}

function pointFeature(point: GeoPoint, properties: Feature['properties']): Feature {
  return { type: 'Feature', geometry: { type: 'Point', coordinates: [point.lon, point.lat] }, properties };
}

function lineFeature(points: readonly GeoPoint[], properties: Feature['properties']): Feature {
  return { type: 'Feature', geometry: { type: 'LineString', coordinates: points.map((point) => [point.lon, point.lat]) }, properties };
}

function polygonFeature(points: readonly GeoPoint[], properties: Feature['properties']): Feature {
  const coordinates = points.map((point) => [point.lon, point.lat] as [number, number]);
  if (coordinates.length > 0) coordinates.push(coordinates[0]);
  return { type: 'Feature', geometry: { type: 'Polygon', coordinates: [coordinates] }, properties };
}

function collection(features: Feature[]): FeatureCollection {
  return { type: 'FeatureCollection', features };
}
