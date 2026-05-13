import * as THREE from 'three';
import { parseVehicleState, TrailBuffer, trailPointFromState, type VehicleStateMessage } from './state';
import './styles.css';

const root = document.querySelector<HTMLDivElement>('#app');
if (!root) {
  throw new Error('missing #app');
}

root.innerHTML = `
  <main class="shell">
    <section class="viewport">
      <canvas id="scene"></canvas>
      <div class="toolbar">
        <button id="pause" type="button">Pause</button>
        <button id="clear" type="button">Clear Trail</button>
        <button id="camera" type="button">Follow</button>
      </div>
      <div class="status" id="status">Disconnected</div>
      <div class="axis axis-east">E</div>
      <div class="axis axis-north">N</div>
    </section>
    <aside class="metrics">
      <h1>Altair Live</h1>
      <dl>
        <div><dt>Altitude</dt><dd id="altitude">--</dd></div>
        <div><dt>Airspeed</dt><dd id="airspeed">--</dd></div>
        <div><dt>Groundspeed</dt><dd id="groundspeed">--</dd></div>
        <div><dt>Climb</dt><dd id="climb">--</dd></div>
        <div><dt>Heading</dt><dd id="heading">--</dd></div>
        <div><dt>Heartbeat</dt><dd id="heartbeat">--</dd></div>
        <div><dt>Position</dt><dd id="position">--</dd></div>
      </dl>
    </aside>
  </main>
`;

const canvas = document.querySelector<HTMLCanvasElement>('#scene')!;
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setClearColor(0x0b1116);

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0x0b1116, 400, 1400);

const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 4000);
camera.position.set(-80, -140, 80);
camera.up.set(0, 0, 1);

const light = new THREE.DirectionalLight(0xffffff, 2.4);
light.position.set(-180, -100, 250);
scene.add(light);
scene.add(new THREE.AmbientLight(0x8aa1b2, 1.5));

const grid = new THREE.GridHelper(600, 24, 0x497287, 0x20323d);
grid.rotation.x = Math.PI / 2;
scene.add(grid);

const axes = new THREE.AxesHelper(80);
scene.add(axes);

const aircraft = new THREE.Group();
const fuselage = new THREE.Mesh(
  new THREE.ConeGeometry(5, 28, 4),
  new THREE.MeshStandardMaterial({ color: 0xf4f8fb, roughness: 0.38, metalness: 0.08 })
);
fuselage.rotation.z = -Math.PI / 2;
aircraft.add(fuselage);

const wing = new THREE.Mesh(
  new THREE.BoxGeometry(7, 42, 1.2),
  new THREE.MeshStandardMaterial({ color: 0x3aa0ff, roughness: 0.45 })
);
aircraft.add(wing);

const tail = new THREE.Mesh(
  new THREE.BoxGeometry(5, 14, 6),
  new THREE.MeshStandardMaterial({ color: 0xffc857, roughness: 0.5 })
);
tail.position.x = -11;
aircraft.add(tail);
scene.add(aircraft);

const headingCue = new THREE.ArrowHelper(new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0, 8), 42, 0xffc857, 10, 5);
scene.add(headingCue);

const trail = new TrailBuffer(1600);
const trailGeometry = new THREE.BufferGeometry();
const trailMaterial = new THREE.LineBasicMaterial({ color: 0x66e0a3 });
const trailLine = new THREE.Line(trailGeometry, trailMaterial);
scene.add(trailLine);

const state = {
  paused: false,
  follow: true,
  lastMessage: null as VehicleStateMessage | null
};

function fmt(value: number | null | undefined, suffix = '', digits = 1): string {
  return value === null || value === undefined || Number.isNaN(value) ? '--' : `${value.toFixed(digits)}${suffix}`;
}

function setText(id: string, value: string): void {
  document.querySelector<HTMLElement>(`#${id}`)!.textContent = value;
}

function updateMetrics(message: VehicleStateMessage): void {
  setText('altitude', fmt(message.globalPosition.altitudeM, ' m'));
  setText('airspeed', fmt(message.metrics.airspeedMps, ' m/s'));
  setText('groundspeed', fmt(message.metrics.groundspeedMps, ' m/s'));
  setText('climb', fmt(message.metrics.climbMps, ' m/s'));
  setText('heading', fmt(message.metrics.headingDeg, ' deg', 0));
  setText('heartbeat', fmt(message.heartbeatAgeS, ' s'));
  setText('position', `${fmt(message.localPosition.northM, ' N')} / ${fmt(message.localPosition.eastM, ' E')} / ${fmt(message.localPosition.upM, ' U')}`);
  const status = document.querySelector<HTMLElement>('#status')!;
  status.textContent = message.connected ? `Connected sys ${message.systemId ?? '--'}` : 'Waiting for MAVLink';
  status.classList.toggle('online', message.connected);
}

function updateTrail(): void {
  const positions = new Float32Array(trail.values().length * 3);
  trail.values().forEach((point, index) => {
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
  if (point && !state.paused) {
    trail.add(point);
    updateTrail();
  }
  if (point) {
    aircraft.position.set(point.eastM, point.northM, point.upM);
    headingCue.position.set(point.eastM, point.northM, point.upM + 8);
  }
  aircraft.rotation.order = 'ZYX';
  aircraft.rotation.set(message.attitude.pitchRad, -message.attitude.yawRad, -message.attitude.rollRad);
  headingCue.setDirection(new THREE.Vector3(Math.sin(message.attitude.yawRad), Math.cos(message.attitude.yawRad), 0).normalize());
}

function connect(): void {
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

document.querySelector<HTMLButtonElement>('#pause')!.addEventListener('click', (event) => {
  state.paused = !state.paused;
  (event.currentTarget as HTMLButtonElement).textContent = state.paused ? 'Resume' : 'Pause';
});

document.querySelector<HTMLButtonElement>('#clear')!.addEventListener('click', () => {
  trail.clear();
  updateTrail();
});

document.querySelector<HTMLButtonElement>('#camera')!.addEventListener('click', (event) => {
  state.follow = !state.follow;
  (event.currentTarget as HTMLButtonElement).textContent = state.follow ? 'Follow' : 'Free';
});

function resize(): void {
  const { clientWidth, clientHeight } = canvas.parentElement!;
  renderer.setSize(clientWidth, clientHeight, false);
  camera.aspect = clientWidth / clientHeight;
  camera.updateProjectionMatrix();
}

function animate(): void {
  resize();
  if (state.follow) {
    const target = aircraft.position;
    camera.position.lerp(new THREE.Vector3(target.x - 80, target.y - 150, target.z + 90), 0.04);
    camera.lookAt(target.x, target.y, target.z + 5);
  }
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

connect();
animate();
