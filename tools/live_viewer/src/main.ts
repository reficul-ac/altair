import * as THREE from 'three';
import {
  headingDegFromYaw,
  parseVehicleState,
  TrailBuffer,
  trailPointFromState,
  yawDegFromRad,
  type TrailPoint,
  type VehicleStateMessage
} from './state';
import './styles.css';

type VisualizerConfig = {
  listenHost: string;
  listenPort: number;
  qgcForwarding: boolean;
  qgcEndpoints: { host: string; port: number }[];
};

type VisualizerApi = {
  onVehicleState: (callback: (message: VehicleStateMessage) => void) => () => void;
  onConfig: (callback: (config: VisualizerConfig) => void) => () => void;
  getConfig: () => Promise<VisualizerConfig>;
  setQgcForwarding: (enabled: boolean) => Promise<VisualizerConfig>;
  setListenPort: (port: number) => Promise<VisualizerConfig>;
};

type CameraMode = 'chase' | 'fpv' | 'free';
type HudMode = 'console' | 'tactical' | 'off';
type ThemeName = 'grid' | 'rez' | 'snow';

declare global {
  interface Window {
    altairVisualizer?: VisualizerApi;
  }
}

const root = document.querySelector<HTMLDivElement>('#app');
if (!root) {
  throw new Error('missing #app');
}

root.innerHTML = `
  <main class="shell theme-grid">
    <section class="viewport">
      <canvas id="scene"></canvas>
      <div class="topbar">
        <div class="segmented" aria-label="Camera mode">
          <button class="active" data-camera="chase" type="button">Chase</button>
          <button data-camera="fpv" type="button">FPV</button>
          <button data-camera="free" type="button">Free</button>
        </div>
        <div class="segmented" aria-label="HUD mode">
          <button class="active" data-hud="console" type="button">Console</button>
          <button data-hud="tactical" type="button">Tactical</button>
          <button data-hud="off" type="button">Off</button>
        </div>
        <button id="ortho-toggle" type="button">Ortho</button>
        <button id="debug-toggle" type="button">Debug</button>
        <button id="theme-toggle" type="button">Grid</button>
      </div>
      <div class="status" id="status">Disconnected</div>
      <div class="hud hud-console" id="hud-console">
        <div class="attitude" aria-hidden="true">
          <div class="attitude-sky"></div>
          <div class="attitude-ground"></div>
          <div class="attitude-bank" id="attitude-bank"></div>
          <div class="attitude-cross"></div>
        </div>
        <div class="heading-tape" id="heading-tape"></div>
        <div class="telemetry-row">
          <div><span id="heading-label">HDG</span><strong id="heading">--</strong></div>
          <div><span>ROLL</span><strong id="roll">--</strong></div>
          <div><span>PITCH</span><strong id="pitch">--</strong></div>
          <div><span>ALT</span><strong id="altitude">--</strong></div>
          <div><span>GS</span><strong id="groundspeed">--</strong></div>
          <div><span>AS</span><strong id="airspeed">--</strong></div>
          <div><span>VS</span><strong id="climb">--</strong></div>
        </div>
      </div>
      <div class="hud hud-tactical hidden" id="hud-tactical">
        <div class="tactical-tag left"><span>GS</span><strong id="tactical-gs">--</strong></div>
        <div class="tactical-tag right"><span>ALT</span><strong id="tactical-alt">--</strong></div>
        <div class="gimbal-ring roll"></div>
        <div class="gimbal-ring pitch"></div>
        <div class="gimbal-ring yaw"></div>
        <canvas id="radar" width="180" height="180"></canvas>
      </div>
      <canvas class="ortho hidden" id="ortho" width="220" height="220"></canvas>
      <div class="debug hidden" id="debug">
        <h2>Debug</h2>
        <dl>
          <div><dt>FPS</dt><dd id="debug-fps">--</dd></div>
          <div><dt>Frame</dt><dd id="debug-frame">--</dd></div>
          <div><dt>Trail</dt><dd id="debug-trail">--</dd></div>
          <div><dt>Camera</dt><dd id="debug-camera">--</dd></div>
          <div><dt>Position</dt><dd id="debug-position">--</dd></div>
        </dl>
      </div>
      <div class="axis axis-east">E</div>
      <div class="axis axis-north">N</div>
    </section>
    <aside class="metrics">
      <h1>Altair Visualizer</h1>
      <section class="controls" aria-label="MAVLink controls">
        <label>
          <span>MAVLink port</span>
          <input id="listen-port" type="number" min="1" max="65535" value="14551" />
        </label>
        <label>
          <span>SITL profile</span>
          <select id="profile">
            <option value="cruise">Cruise</option>
            <option value="takeoff">Takeoff</option>
            <option value="turn">Turn</option>
            <option value="descent">Descent</option>
            <option value="failsafe">Failsafe</option>
            <option value="mission">Mission</option>
          </select>
        </label>
        <label class="toggle">
          <input id="qgc" type="checkbox" checked />
          <span>Forward QGC</span>
        </label>
        <p id="config">UDP 127.0.0.1:14551</p>
      </section>
      <div class="command-strip">
        <button id="pause" type="button">Pause</button>
        <button id="clear" type="button">Clear Trail</button>
        <button id="heading-mode" type="button">HDG</button>
      </div>
      <section class="vehicle-card">
        <span id="vehicle-type">Unknown vehicle</span>
        <strong id="vehicle-id">sys --</strong>
      </section>
      <dl>
        <div><dt>Heartbeat</dt><dd id="heartbeat">--</dd></div>
        <div><dt>Packet Age</dt><dd id="packet">--</dd></div>
        <div><dt>Throttle</dt><dd id="throttle">--</dd></div>
        <div><dt>Lat / Lon</dt><dd id="latlon">--</dd></div>
        <div><dt>Position</dt><dd id="position">--</dd></div>
        <div><dt>Velocity</dt><dd id="velocity">--</dd></div>
      </dl>
    </aside>
  </main>
`;

const shell = document.querySelector<HTMLElement>('.shell')!;
const canvas = document.querySelector<HTMLCanvasElement>('#scene')!;
const radarCanvas = document.querySelector<HTMLCanvasElement>('#radar')!;
const orthoCanvas = document.querySelector<HTMLCanvasElement>('#ortho')!;
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setClearColor(0x0b1116);

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0x0b1116, 420, 1500);

const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 4000);
camera.position.set(-80, -140, 80);
camera.up.set(0, 0, 1);

const freeControls = {
  yaw: Math.atan2(camera.position.x, camera.position.y),
  pitch: -0.35,
  keys: new Set<string>(),
  dragging: false,
  lastX: 0,
  lastY: 0
};

const light = new THREE.DirectionalLight(0xffffff, 2.4);
light.position.set(-180, -100, 250);
scene.add(light);
scene.add(new THREE.AmbientLight(0x8aa1b2, 1.5));

const grid = new THREE.GridHelper(700, 28, 0x497287, 0x20323d);
grid.rotation.x = Math.PI / 2;
scene.add(grid);

const runwayMaterial = new THREE.MeshStandardMaterial({ color: 0x253640, roughness: 0.9 });
const runway = new THREE.Mesh(
  new THREE.PlaneGeometry(240, 26),
  runwayMaterial
);
runway.rotation.x = Math.PI / 2;
runway.position.z = -0.04;
scene.add(runway);

const axes = new THREE.AxesHelper(90);
scene.add(axes);

function makeAircraft(): THREE.Group {
  const group = new THREE.Group();
  const fuselage = new THREE.Mesh(
    new THREE.CapsuleGeometry(3.6, 24, 8, 16),
    new THREE.MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.38, metalness: 0.08 })
  );
  fuselage.rotation.z = Math.PI / 2;
  group.add(fuselage);

  const wing = new THREE.Mesh(
    new THREE.BoxGeometry(5, 46, 1.1),
    new THREE.MeshStandardMaterial({ color: 0x3aa0ff, roughness: 0.45 })
  );
  group.add(wing);

  const tail = new THREE.Mesh(
    new THREE.BoxGeometry(4, 15, 5),
    new THREE.MeshStandardMaterial({ color: 0xffc857, roughness: 0.5 })
  );
  tail.position.x = -11;
  group.add(tail);

  const nose = new THREE.Mesh(
    new THREE.ConeGeometry(3.7, 8, 24),
    new THREE.MeshStandardMaterial({ color: 0xfffbf0, roughness: 0.36 })
  );
  nose.rotation.z = -Math.PI / 2;
  nose.position.x = 16;
  group.add(nose);
  return group;
}

const aircraft = makeAircraft();
scene.add(aircraft);

const headingCue = new THREE.ArrowHelper(new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0, 8), 42, 0xffc857, 10, 5);
scene.add(headingCue);

const trail = new TrailBuffer(2200);
const trailGeometry = new THREE.BufferGeometry();
const trailMaterial = new THREE.LineBasicMaterial({ color: 0x66e0a3 });
const trailLine = new THREE.Line(trailGeometry, trailMaterial);
scene.add(trailLine);

const state = {
  paused: false,
  cameraMode: 'chase' as CameraMode,
  hudMode: 'console' as HudMode,
  theme: 'grid' as ThemeName,
  ortho: true,
  debug: false,
  showYaw: false,
  lastMessage: null as VehicleStateMessage | null,
  lastPoint: null as TrailPoint | null,
  frames: 0,
  fps: 0,
  lastFpsMs: performance.now(),
  lastFrameMs: 0
};

function fmt(value: number | null | undefined, suffix = '', digits = 1): string {
  return value === null || value === undefined || Number.isNaN(value) ? '--' : `${value.toFixed(digits)}${suffix}`;
}

function setText(id: string, value: string): void {
  document.querySelector<HTMLElement>(`#${id}`)!.textContent = value;
}

function updateMetrics(message: VehicleStateMessage): void {
  const heading = state.showYaw ? yawDegFromRad(message.attitude.yawRad) : (message.metrics.headingDeg ?? headingDegFromYaw(message.attitude.yawRad));
  setText('heading-label', state.showYaw ? 'YAW' : 'HDG');
  setText('heading', fmt(heading, ' deg', 0));
  setText('roll', fmt((message.attitude.rollRad * 180) / Math.PI, ' deg', 0));
  setText('pitch', fmt((message.attitude.pitchRad * 180) / Math.PI, ' deg', 0));
  setText('altitude', fmt(message.globalPosition.altitudeM, ' m'));
  setText('airspeed', fmt(message.metrics.airspeedMps, ' m/s'));
  setText('groundspeed', fmt(message.metrics.groundspeedMps, ' m/s'));
  setText('climb', fmt(message.metrics.climbMps, ' m/s'));
  setText('tactical-gs', fmt(message.metrics.groundspeedMps, ' m/s'));
  setText('tactical-alt', fmt(message.globalPosition.altitudeM, ' m'));
  setText('heartbeat', fmt(message.heartbeatAgeS, ' s'));
  setText('packet', fmt(message.packetAgeS, ' s'));
  setText('throttle', fmt(message.metrics.throttlePct, '%', 0));
  setText('latlon', `${fmt(message.globalPosition.latDeg, '', 6)} / ${fmt(message.globalPosition.lonDeg, '', 6)}`);
  setText('position', `${fmt(message.localPosition.northM, ' N')} / ${fmt(message.localPosition.eastM, ' E')} / ${fmt(message.localPosition.upM, ' U')}`);
  setText('velocity', `${fmt(message.velocity.northMps, ' N')} / ${fmt(message.velocity.eastMps, ' E')} / ${fmt(-message.velocity.downMps, ' U')}`);
  setText('vehicle-type', message.vehicleType ?? 'Unknown vehicle');
  setText('vehicle-id', `sys ${message.systemId ?? '--'} comp ${message.componentId ?? '--'}`);

  const status = document.querySelector<HTMLElement>('#status')!;
  status.textContent = message.connected ? `Connected ${message.vehicleType ?? 'MAVLink'} sys ${message.systemId ?? '--'}` : 'Waiting for MAVLink';
  status.classList.toggle('online', message.connected);
  document.querySelector<HTMLElement>('#attitude-bank')!.style.transform = `rotate(${message.attitude.rollRad}rad)`;
  document.querySelector<HTMLElement>('#heading-tape')!.style.setProperty('--heading-offset', `${heading * -1}px`);
}

function updateConfig(config: VisualizerConfig): void {
  const qgc = document.querySelector<HTMLInputElement>('#qgc')!;
  const listenPort = document.querySelector<HTMLInputElement>('#listen-port')!;
  const configText = document.querySelector<HTMLElement>('#config')!;
  listenPort.value = String(config.listenPort);
  qgc.checked = config.qgcForwarding;
  const endpoints = config.qgcEndpoints.map((endpoint) => `${endpoint.host}:${endpoint.port}`).join(', ');
  configText.textContent = `UDP ${config.listenHost}:${config.listenPort} / QGC ${config.qgcForwarding ? endpoints : 'off'}`;
}

function updateTrail(): void {
  const points = trail.values();
  const positions = new Float32Array(points.length * 3);
  points.forEach((point, index) => {
    positions[index * 3] = point.eastM;
    positions[index * 3 + 1] = point.northM;
    positions[index * 3 + 2] = point.upM;
  });
  trailGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  trailGeometry.computeBoundingSphere();
}

function applyMessage(message: VehicleStateMessage): void {
  state.lastMessage = message;
  updateMetrics(message);
  const point = trailPointFromState(message);
  if (point) {
    state.lastPoint = point;
    if (!state.paused) {
      trail.add(point);
      updateTrail();
    }
    aircraft.position.set(point.eastM, point.northM, point.upM);
    headingCue.position.set(point.eastM, point.northM, point.upM + 8);
  }
  aircraft.rotation.order = 'ZYX';
  aircraft.rotation.set(message.attitude.pitchRad, -message.attitude.yawRad, -message.attitude.rollRad);
  headingCue.setDirection(new THREE.Vector3(Math.sin(message.attitude.yawRad), Math.cos(message.attitude.yawRad), 0).normalize());
}

function connect(): void {
  if (window.altairVisualizer) {
    window.altairVisualizer.onVehicleState((message) => applyMessage(message));
    window.altairVisualizer.onConfig((config) => updateConfig(config));
    window.altairVisualizer.getConfig().then(updateConfig).catch(() => {
      document.querySelector<HTMLElement>('#status')!.textContent = 'MAVLink service unavailable';
    });
    document.querySelector<HTMLElement>('#status')!.textContent = 'Listening for MAVLink';
    return;
  }
  const wsUrl = new URLSearchParams(window.location.search).get('ws') ?? 'ws://127.0.0.1:8765';
  const socket = new WebSocket(wsUrl);
  socket.addEventListener('open', () => {
    document.querySelector<HTMLElement>('#status')!.textContent = 'WebSocket connected';
  });
  socket.addEventListener('message', (event) => {
    const message = parseVehicleState(String(event.data));
    if (message) {
      applyMessage(message);
    }
  });
  socket.addEventListener('close', () => {
    document.querySelector<HTMLElement>('#status')!.textContent = 'Reconnecting';
    setTimeout(connect, 1000);
  });
}

function setCameraMode(mode: CameraMode): void {
  state.cameraMode = mode;
  document.querySelectorAll<HTMLButtonElement>('[data-camera]').forEach((button) => {
    button.classList.toggle('active', button.dataset.camera === mode);
  });
}

function setHudMode(mode: HudMode): void {
  state.hudMode = mode;
  document.querySelectorAll<HTMLButtonElement>('[data-hud]').forEach((button) => {
    button.classList.toggle('active', button.dataset.hud === mode);
  });
  document.querySelector<HTMLElement>('#hud-console')!.classList.toggle('hidden', mode !== 'console');
  document.querySelector<HTMLElement>('#hud-tactical')!.classList.toggle('hidden', mode !== 'tactical');
}

function setTheme(theme: ThemeName): void {
  state.theme = theme;
  shell.classList.remove('theme-grid', 'theme-rez', 'theme-snow');
  shell.classList.add(`theme-${theme}`);
  document.querySelector<HTMLButtonElement>('#theme-toggle')!.textContent = theme[0].toUpperCase() + theme.slice(1);
  const palette: Record<ThemeName, { bg: number; fog: number; grid: number; runway: number; trail: number }> = {
    grid: { bg: 0x0b1116, fog: 0x0b1116, grid: 0x497287, runway: 0x253640, trail: 0x66e0a3 },
    rez: { bg: 0x050909, fog: 0x050909, grid: 0x1ed1b2, runway: 0x071313, trail: 0xe464ff },
    snow: { bg: 0xeef3f5, fog: 0xeef3f5, grid: 0x8297a1, runway: 0xd8e3e8, trail: 0x007a5a }
  };
  const next = palette[theme];
  renderer.setClearColor(next.bg);
  scene.fog = new THREE.Fog(next.fog, 420, 1500);
  const gridMaterials = Array.isArray(grid.material) ? grid.material : [grid.material];
  gridMaterials.forEach((material) => {
    if ('color' in material) {
      material.color.setHex(next.grid);
    }
  });
  runwayMaterial.color.setHex(next.runway);
  trailMaterial.color.setHex(next.trail);
}

function drawRadar(): void {
  const ctx = radarCanvas.getContext('2d')!;
  const size = radarCanvas.width;
  ctx.clearRect(0, 0, size, size);
  ctx.strokeStyle = 'rgba(102, 224, 163, 0.45)';
  ctx.fillStyle = 'rgba(10, 18, 24, 0.72)';
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
  ctx.fillStyle = '#66e0a3';
  ctx.beginPath();
  ctx.arc(size / 2, size / 2, 5, 0, Math.PI * 2);
  ctx.fill();
}

function drawOrtho(): void {
  const ctx = orthoCanvas.getContext('2d')!;
  const size = orthoCanvas.width;
  ctx.clearRect(0, 0, size, size);
  ctx.fillStyle = getComputedStyle(shell).getPropertyValue('--panel-bg').trim() || '#101922';
  ctx.fillRect(0, 0, size, size);
  ctx.strokeStyle = 'rgba(138, 161, 178, 0.22)';
  ctx.lineWidth = 1;
  for (let i = 20; i < size; i += 20) {
    ctx.beginPath();
    ctx.moveTo(i, 0);
    ctx.lineTo(i, size);
    ctx.moveTo(0, i);
    ctx.lineTo(size, i);
    ctx.stroke();
  }
  const points = trail.values();
  const center = state.lastPoint ?? { eastM: 0, northM: 0, upM: 0, timestampMs: 0 };
  const scale = 2.2;
  ctx.strokeStyle = '#66e0a3';
  ctx.lineWidth = 2;
  ctx.beginPath();
  points.forEach((point, index) => {
    const x = size / 2 + (point.eastM - center.eastM) * scale;
    const y = size / 2 - (point.northM - center.northM) * scale;
    if (index === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  });
  ctx.stroke();
  ctx.fillStyle = '#ffc857';
  ctx.beginPath();
  ctx.arc(size / 2, size / 2, 5, 0, Math.PI * 2);
  ctx.fill();
}

function updateCamera(deltaS: number): void {
  const target = aircraft.position;
  if (state.cameraMode === 'chase') {
    const yaw = state.lastMessage?.attitude.yawRad ?? 0;
    const offset = new THREE.Vector3(-Math.sin(yaw) * 110, -Math.cos(yaw) * 110, 62);
    camera.position.lerp(target.clone().add(offset), 0.05);
    camera.lookAt(target.x, target.y, target.z + 6);
    return;
  }
  if (state.cameraMode === 'fpv') {
    const yaw = state.lastMessage?.attitude.yawRad ?? 0;
    const pitch = state.lastMessage?.attitude.pitchRad ?? 0;
    const eye = target.clone().add(new THREE.Vector3(Math.sin(yaw) * 16, Math.cos(yaw) * 16, 4));
    const look = eye.clone().add(new THREE.Vector3(Math.sin(yaw) * Math.cos(pitch), Math.cos(yaw) * Math.cos(pitch), Math.sin(pitch)).multiplyScalar(120));
    camera.position.lerp(eye, 0.18);
    camera.lookAt(look);
    return;
  }
  const boost = freeControls.keys.has('ShiftLeft') || freeControls.keys.has('ShiftRight') ? 3 : 1;
  const speed = 70 * boost * deltaS;
  const forward = new THREE.Vector3(Math.sin(freeControls.yaw), Math.cos(freeControls.yaw), 0);
  const right = new THREE.Vector3(forward.y, -forward.x, 0);
  if (freeControls.keys.has('KeyW')) camera.position.addScaledVector(forward, speed);
  if (freeControls.keys.has('KeyS')) camera.position.addScaledVector(forward, -speed);
  if (freeControls.keys.has('KeyA')) camera.position.addScaledVector(right, -speed);
  if (freeControls.keys.has('KeyD')) camera.position.addScaledVector(right, speed);
  if (freeControls.keys.has('KeyQ')) camera.position.z -= speed;
  if (freeControls.keys.has('KeyE')) camera.position.z += speed;
  const look = camera.position.clone().add(
    new THREE.Vector3(
      Math.sin(freeControls.yaw) * Math.cos(freeControls.pitch),
      Math.cos(freeControls.yaw) * Math.cos(freeControls.pitch),
      Math.sin(freeControls.pitch)
    )
  );
  camera.lookAt(look);
}

document.querySelector<HTMLButtonElement>('#pause')!.addEventListener('click', (event) => {
  state.paused = !state.paused;
  (event.currentTarget as HTMLButtonElement).textContent = state.paused ? 'Resume' : 'Pause';
});

document.querySelector<HTMLButtonElement>('#clear')!.addEventListener('click', () => {
  trail.clear();
  updateTrail();
});

document.querySelector<HTMLButtonElement>('#heading-mode')!.addEventListener('click', (event) => {
  state.showYaw = !state.showYaw;
  (event.currentTarget as HTMLButtonElement).textContent = state.showYaw ? 'YAW' : 'HDG';
  if (state.lastMessage) {
    updateMetrics(state.lastMessage);
  }
});

document.querySelector<HTMLButtonElement>('#ortho-toggle')!.addEventListener('click', () => {
  state.ortho = !state.ortho;
  orthoCanvas.classList.toggle('hidden', !state.ortho);
});

document.querySelector<HTMLButtonElement>('#debug-toggle')!.addEventListener('click', () => {
  state.debug = !state.debug;
  document.querySelector<HTMLElement>('#debug')!.classList.toggle('hidden', !state.debug);
});

document.querySelector<HTMLButtonElement>('#theme-toggle')!.addEventListener('click', () => {
  const next: Record<ThemeName, ThemeName> = { grid: 'rez', rez: 'snow', snow: 'grid' };
  setTheme(next[state.theme]);
});

document.querySelectorAll<HTMLButtonElement>('[data-camera]').forEach((button) => {
  button.addEventListener('click', () => setCameraMode(button.dataset.camera as CameraMode));
});

document.querySelectorAll<HTMLButtonElement>('[data-hud]').forEach((button) => {
  button.addEventListener('click', () => setHudMode(button.dataset.hud as HudMode));
});

document.querySelector<HTMLInputElement>('#qgc')!.addEventListener('change', (event) => {
  if (!window.altairVisualizer) {
    return;
  }
  window.altairVisualizer.setQgcForwarding((event.currentTarget as HTMLInputElement).checked).then(updateConfig).catch(() => {
    document.querySelector<HTMLElement>('#status')!.textContent = 'QGC update failed';
  });
});

document.querySelector<HTMLInputElement>('#listen-port')!.addEventListener('change', (event) => {
  if (!window.altairVisualizer) {
    return;
  }
  const port = Number((event.currentTarget as HTMLInputElement).value);
  window.altairVisualizer.setListenPort(port).then(updateConfig).catch(() => {
    document.querySelector<HTMLElement>('#status')!.textContent = 'Port update failed';
  });
});

window.addEventListener('keydown', (event) => {
  freeControls.keys.add(event.code);
  if (event.code === 'KeyC') setCameraMode(state.cameraMode === 'chase' ? 'fpv' : state.cameraMode === 'fpv' ? 'free' : 'chase');
  if (event.code === 'KeyH') setHudMode(state.hudMode === 'console' ? 'tactical' : state.hudMode === 'tactical' ? 'off' : 'console');
  if (event.code === 'KeyO') {
    state.ortho = !state.ortho;
    orthoCanvas.classList.toggle('hidden', !state.ortho);
  }
  if (event.code === 'KeyY') document.querySelector<HTMLButtonElement>('#heading-mode')!.click();
  if (event.code === 'KeyV') document.querySelector<HTMLButtonElement>('#theme-toggle')!.click();
});

window.addEventListener('keyup', (event) => {
  freeControls.keys.delete(event.code);
});

canvas.addEventListener('pointerdown', (event) => {
  if (state.cameraMode !== 'free') {
    return;
  }
  freeControls.dragging = true;
  freeControls.lastX = event.clientX;
  freeControls.lastY = event.clientY;
  canvas.setPointerCapture(event.pointerId);
});

canvas.addEventListener('pointermove', (event) => {
  if (!freeControls.dragging) {
    return;
  }
  freeControls.yaw -= (event.clientX - freeControls.lastX) * 0.005;
  freeControls.pitch = Math.max(-1.25, Math.min(1.25, freeControls.pitch - (event.clientY - freeControls.lastY) * 0.005));
  freeControls.lastX = event.clientX;
  freeControls.lastY = event.clientY;
});

canvas.addEventListener('pointerup', (event) => {
  freeControls.dragging = false;
  canvas.releasePointerCapture(event.pointerId);
});

function resize(): void {
  const { clientWidth, clientHeight } = canvas.parentElement!;
  renderer.setSize(clientWidth, clientHeight, false);
  camera.aspect = clientWidth / clientHeight;
  camera.updateProjectionMatrix();
}

let lastAnimationMs = performance.now();
function animate(nowMs = performance.now()): void {
  const deltaS = Math.min(0.05, (nowMs - lastAnimationMs) / 1000);
  state.lastFrameMs = nowMs - lastAnimationMs;
  lastAnimationMs = nowMs;
  resize();
  updateCamera(deltaS);
  drawRadar();
  if (state.ortho) {
    drawOrtho();
  }
  state.frames += 1;
  if (nowMs - state.lastFpsMs >= 500) {
    state.fps = (state.frames * 1000) / (nowMs - state.lastFpsMs);
    state.frames = 0;
    state.lastFpsMs = nowMs;
    setText('debug-fps', fmt(state.fps, '', 0));
    setText('debug-frame', fmt(state.lastFrameMs, ' ms', 1));
    setText('debug-trail', String(trail.values().length));
    setText('debug-camera', state.cameraMode);
    setText('debug-position', state.lastPoint ? `${fmt(state.lastPoint.eastM)} E / ${fmt(state.lastPoint.northM)} N / ${fmt(state.lastPoint.upM)} U` : '--');
  }
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

setHudMode('console');
setTheme('grid');
orthoCanvas.classList.toggle('hidden', !state.ortho);
connect();
animate();
