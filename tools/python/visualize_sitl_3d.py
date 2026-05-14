#!/usr/bin/env python3
"""Generate a standalone 3D playback page from an Altair SITL CSV log."""

import argparse
import csv
import html
import json
import math
import sys
from pathlib import Path

REQUIRED_COLUMNS = ("time_s", "pos_n_m", "pos_e_m", "altitude_m")
OPTIONAL_COLUMNS = (
    "roll_rad",
    "pitch_rad",
    "yaw_rad",
    "airspeed_mps",
    "mode",
    "lat_deg",
    "lon_deg",
    "pos_d_m",
    "vel_n_mps",
    "vel_e_mps",
    "vel_d_mps",
    "p_rps",
    "q_rps",
    "r_rps",
    "accel_x_mps2",
    "accel_y_mps2",
    "accel_z_mps2",
    "force_x_n",
    "force_y_n",
    "force_z_n",
    "moment_x_nm",
    "moment_y_nm",
    "moment_z_nm",
)


def parse_args():
    parser = argparse.ArgumentParser(description="Generate a 3D SITL CSV playback HTML file.")
    parser.add_argument("csv_path")
    parser.add_argument("--output", default="sitl_3d.html")
    parser.add_argument("--title", default="Altair SITL 3D Playback")
    return parser.parse_args()


def finite_float(row, column, row_number, default=None):
    text = row.get(column)
    if text is None or text == "":
        if default is not None:
            return default
        raise ValueError(f"row {row_number}: missing required column: {column}")
    try:
        value = float(text)
    except ValueError as exc:
        raise ValueError(f"row {row_number}: column {column} is not numeric: {text}") from exc
    if not math.isfinite(value):
        raise ValueError(f"row {row_number}: column {column} is not finite: {text}")
    return value


def load_frames(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{csv_path}: no data rows")

    available = set(rows[0].keys())
    missing = [column for column in REQUIRED_COLUMNS if column not in available]
    if missing:
        raise ValueError(f"missing required column(s): {', '.join(missing)}")

    frames = []
    for row_number, row in enumerate(rows, start=2):
        frame = {column: finite_float(row, column, row_number) for column in REQUIRED_COLUMNS}
        for column in OPTIONAL_COLUMNS:
            if column in available:
                frame[column] = finite_float(row, column, row_number, default=0.0)
        frames.append(frame)
    return frames


def bounds_for(frames):
    east = [frame["pos_e_m"] for frame in frames]
    north = [frame["pos_n_m"] for frame in frames]
    altitude = [frame["altitude_m"] for frame in frames]
    return {
        "min_e": min(east),
        "max_e": max(east),
        "min_n": min(north),
        "max_n": max(north),
        "min_alt": min(altitude),
        "max_alt": max(altitude),
    }


def render_html(title, csv_name, frames):
    payload = {
        "title": title,
        "csvName": csv_name,
        "frames": frames,
        "bounds": bounds_for(frames),
    }
    data_json = json.dumps(payload, separators=(",", ":"))
    escaped_title = html.escape(title)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{escaped_title}</title>
<style>
:root {{
  color-scheme: dark;
  font-family: Arial, Helvetica, sans-serif;
  background: #0f1418;
  color: #f3f6f8;
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  height: 100vh;
  display: grid;
  grid-template-columns: minmax(0, 1fr) 280px;
  grid-template-rows: minmax(0, 1fr) auto;
  overflow: hidden;
}}
.viewer {{
  min-width: 0;
  min-height: 0;
  grid-column: 1;
  grid-row: 1;
  position: relative;
}}
canvas {{
  display: block;
  width: 100%;
  height: 100%;
  background: #10161a;
}}
.titlebar {{
  position: absolute;
  left: 16px;
  top: 14px;
  padding: 10px 12px;
  background: rgba(19, 27, 32, 0.82);
  border: 1px solid rgba(100, 117, 127, 0.45);
  border-radius: 6px;
  pointer-events: none;
}}
h1 {{
  margin: 0;
  font-size: 17px;
  font-weight: 700;
}}
.subtle {{ color: #b6c2c9; font-size: 12px; margin-top: 4px; }}
.controls {{
  grid-column: 1 / -1;
  grid-row: 2;
  display: flex;
  flex-wrap: wrap;
  gap: 10px 16px;
  align-items: center;
  min-height: 58px;
  padding: 10px 14px;
  background: #1a2228;
  border-top: 1px solid #34424b;
}}
.metrics-panel {{
  grid-column: 2;
  grid-row: 1;
  min-height: 0;
  overflow: auto;
  background: #172027;
  border-left: 1px solid #34424b;
  padding: 14px;
}}
.metrics-panel h2 {{
  margin: 0 0 12px;
  font-size: 14px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: #d9e2e7;
}}
.metric-grid {{
  display: grid;
  grid-template-columns: 1fr;
  gap: 8px;
}}
.metric-row {{
  display: grid;
  grid-template-columns: 92px minmax(0, 1fr);
  gap: 10px;
  padding: 8px 0;
  border-bottom: 1px solid rgba(80, 96, 106, 0.45);
  font-variant-numeric: tabular-nums;
}}
.metric-row span:first-child {{ color: #9fb0ba; }}
.metric-row span:last-child {{
  color: #f3f6f8;
  text-align: right;
  overflow-wrap: anywhere;
}}
button, select {{
  height: 32px;
  border: 1px solid #52616a;
  background: #263139;
  color: #f2f6f8;
  border-radius: 4px;
  padding: 0 10px;
}}
input[type="range"] {{
  width: min(520px, 45vw);
  accent-color: #8b5cf6;
}}
.toggle {{
  display: inline-flex;
  gap: 7px;
  align-items: center;
  min-height: 32px;
}}
.zoom {{
  display: inline-flex;
  gap: 8px;
  align-items: center;
}}
#zoom {{
  width: 140px;
}}
.metric {{
  font-variant-numeric: tabular-nums;
  min-width: 110px;
}}
@media (max-width: 760px) {{
  body {{
    grid-template-columns: 1fr;
    grid-template-rows: minmax(0, 1fr) auto auto;
  }}
  .metrics-panel {{
    grid-column: 1;
    grid-row: 2;
    max-height: 34vh;
    border-left: 0;
    border-top: 1px solid #34424b;
  }}
  .controls {{
    grid-column: 1;
    grid-row: 3;
  }}
  input[type="range"] {{ width: min(360px, 52vw); }}
}}
</style>
</head>
<body>
<main class="viewer">
  <canvas id="view"></canvas>
  <div class="titlebar">
    <h1>{escaped_title}</h1>
    <div class="subtle" id="source"></div>
  </div>
</main>
<aside class="metrics-panel" aria-label="Metrics panel">
  <h2>Metrics</h2>
  <div class="metric-grid" id="metrics"></div>
</aside>
<footer class="controls">
  <button id="play" type="button">Play</button>
  <input id="scrub" type="range" min="0" max="0" value="0" step="1" aria-label="Playback frame">
  <label>Speed <select id="speed">
    <option value="0.25">0.25x</option>
    <option value="0.5">0.5x</option>
    <option value="1" selected>1x</option>
    <option value="2">2x</option>
    <option value="4">4x</option>
  </select></label>
  <label class="toggle"><input id="lock" type="checkbox" checked> Lock on aircraft</label>
  <label class="zoom">Zoom <input id="zoom" type="range" min="0" max="8" value="2" step="1" aria-label="Zoom"><span id="zoomLabel">1x</span></label>
  <span class="metric" id="time"></span>
  <span class="metric" id="altitude"></span>
  <span class="metric" id="airspeed"></span>
</footer>
<script id="sitl-data" type="application/json">{data_json}</script>
<script>
const data = JSON.parse(document.getElementById("sitl-data").textContent);
const frames = data.frames;
const canvas = document.getElementById("view");
const ctx = canvas.getContext("2d");
const scrub = document.getElementById("scrub");
const play = document.getElementById("play");
const speed = document.getElementById("speed");
const lock = document.getElementById("lock");
const zoom = document.getElementById("zoom");
const zoomLabel = document.getElementById("zoomLabel");
const metrics = document.getElementById("metrics");
const timeLabel = document.getElementById("time");
const altitudeLabel = document.getElementById("altitude");
const airspeedLabel = document.getElementById("airspeed");
document.getElementById("source").textContent = `${{data.csvName}} - ${{frames.length}} frames`;
scrub.max = String(frames.length - 1);

let frameIndex = 0;
let playing = false;
let lastTickMs = 0;
let simCarryS = 0;
const zoomMultipliers = [0.5, 0.75, 1, 1.5, 2, 3, 5, 7.5, 10];
const TRAIL_COLOR = "#8b5cf6";

const metricRows = [
  ["time", "Time", f => fmt(f.time_s, 2, " s")],
  ["mode", "Mode", f => fmtValue(f.mode)],
  ["speed", "Speed", f => fmt(f.airspeed_mps, 1, " m/s")],
  ["latlon", "Lat/Lon", f => has(f.lat_deg) && has(f.lon_deg) ? `${{f.lat_deg.toFixed(7)}}, ${{f.lon_deg.toFixed(7)}}` : "--"],
  ["ned", "N/E/D", f => `${{fmt(f.pos_n_m, 1, "")}} / ${{fmt(f.pos_e_m, 1, "")}} / ${{fmt(has(f.pos_d_m) ? f.pos_d_m : -f.altitude_m, 1, " m")}}`],
  ["alt", "Altitude", f => fmt(f.altitude_m, 1, " m")],
  ["att", "Attitude", f => `${{deg(f.roll_rad)}} / ${{deg(f.pitch_rad)}} / ${{deg(f.yaw_rad)}} deg`],
  ["vel", "Velocity", f => vector(f, ["vel_n_mps", "vel_e_mps", "vel_d_mps"], 2, " m/s")],
  ["rates", "Rates", f => vector(f, ["p_rps", "q_rps", "r_rps"], 3, " rad/s")],
  ["accel", "Accel", f => vector(f, ["accel_x_mps2", "accel_y_mps2", "accel_z_mps2"], 2, " m/s2")],
  ["force", "Force", f => vector(f, ["force_x_n", "force_y_n", "force_z_n"], 2, " N")],
  ["moment", "Moment", f => vector(f, ["moment_x_nm", "moment_y_nm", "moment_z_nm"], 3, " Nm")]
];
metrics.innerHTML = metricRows.map(([id, label]) => `<div class="metric-row"><span>${{label}}</span><span id="metric-${{id}}">--</span></div>`).join("");

function resize() {{
  const rect = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * scale));
  canvas.height = Math.max(1, Math.floor(rect.height * scale));
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  draw();
}}

function has(value) {{
  return value !== undefined && value !== null && Number.isFinite(Number(value));
}}

function fmt(value, digits, unit) {{
  return has(value) ? `${{Number(value).toFixed(digits)}}${{unit}}` : "--";
}}

function fmtValue(value) {{
  return value === undefined || value === null || value === "" ? "--" : String(value);
}}

function deg(value) {{
  return has(value) ? (Number(value) * 180 / Math.PI).toFixed(1) : "--";
}}

function vector(frame, fields, digits, unit) {{
  if (!fields.every(field => has(frame[field]))) return "--";
  return `${{fields.map(field => Number(frame[field]).toFixed(digits)).join(" / ")}}${{unit}}`;
}}

function worldPoint(frame) {{
  return {{
    x: frame.pos_e_m,
    y: frame.altitude_m,
    z: frame.pos_n_m
  }};
}}

function sceneCenter() {{
  if (lock.checked) return worldPoint(frames[frameIndex]);
  const b = data.bounds;
  return {{
    x: (b.min_e + b.max_e) * 0.5,
    y: (b.min_alt + b.max_alt) * 0.5,
    z: (b.min_n + b.max_n) * 0.5
  }};
}}

function scaleForScene() {{
  const b = data.bounds;
  const routeSpan = Math.max(b.max_e - b.min_e, b.max_n - b.min_n, b.max_alt - b.min_alt, 1);
  const lockedSpan = Math.max(80, routeSpan * 0.18);
  const span = lock.checked ? lockedSpan : routeSpan;
  return (260 / span) * zoomMultipliers[Number(zoom.value)];
}}

function project(point, center) {{
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  const s = scaleForScene();
  const yaw = -0.75;
  const pitch = -0.55;
  const cy = Math.cos(yaw), sy = Math.sin(yaw);
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const local = center ? {{x: point.x - center.x, y: point.y - center.y, z: point.z - center.z}} : point;
  const x1 = local.x * cy - local.z * sy;
  const z1 = local.x * sy + local.z * cy;
  const y2 = local.y * cp - z1 * sp;
  const z2 = local.y * sp + z1 * cp;
  const depth = 900 + z2 * s;
  const perspective = 900 / Math.max(depth, 120);
  return {{
    x: w * 0.5 + x1 * s * perspective,
    y: h * 0.55 - y2 * s * perspective,
    visible: depth > 0
  }};
}}

function line(a, b, color, width = 1, center) {{
  const pa = project(a, center);
  const pb = project(b, center);
  if (!pa.visible || !pb.visible) return;
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  ctx.moveTo(pa.x, pa.y);
  ctx.lineTo(pb.x, pb.y);
  ctx.stroke();
}}

function rotateBody(point, frame) {{
  const roll = frame.roll_rad || 0;
  const pitch = frame.pitch_rad || 0;
  const yaw = frame.yaw_rad || 0;
  let x = point.x, y = point.y, z = point.z;
  let c = Math.cos(roll), s = Math.sin(roll);
  let x1 = x * c - y * s, y1 = x * s + y * c;
  x = x1; y = y1;
  c = Math.cos(pitch); s = Math.sin(pitch);
  y1 = y * c + z * s;
  let z1 = -y * s + z * c;
  y = y1; z = z1;
  c = Math.cos(yaw); s = Math.sin(yaw);
  x1 = x * c + z * s;
  z1 = -x * s + z * c;
  return {{x: x1, y, z: z1}};
}}

function add(a, b) {{
  return {{x: a.x + b.x, y: a.y + b.y, z: a.z + b.z}};
}}

function drawAxes(origin, center) {{
  const len = 80 / scaleForScene();
  line(origin, {{x: origin.x + len, y: origin.y, z: origin.z}}, "#d65b5b", 2, center);
  line(origin, {{x: origin.x, y: origin.y + len, z: origin.z}}, "#83c779", 2, center);
  line(origin, {{x: origin.x, y: origin.y, z: origin.z + len}}, "#6fc3df", 2, center);
}}

function drawMeshLine(base, a, b, frame, color, width, center) {{
  line(add(base, rotateBody(a, frame)), add(base, rotateBody(b, frame)), color, width, center);
}}

function drawAircraftMesh(frame, center) {{
  const base = worldPoint(frame);
  const size = Math.max(6, 28 / scaleForScene());
  const nose = {{x: 0, y: 0, z: size * 1.7}};
  const tail = {{x: 0, y: 0, z: -size * 1.15}};
  const wingL = {{x: -size * 1.65, y: 0, z: -size * 0.12}};
  const wingR = {{x: size * 1.65, y: 0, z: -size * 0.12}};
  const tailL = {{x: -size * 0.75, y: 0, z: -size * 0.95}};
  const tailR = {{x: size * 0.75, y: 0, z: -size * 0.95}};
  const fin = {{x: 0, y: size * 0.72, z: -size * 1.0}};
  const cue = {{x: 0, y: 0, z: size * 2.25}};
  drawMeshLine(base, tail, nose, frame, "#f6f7fb", 3.5, center);
  drawMeshLine(base, wingL, wingR, frame, "#f6f7fb", 4, center);
  drawMeshLine(base, tailL, tailR, frame, "#dbe5eb", 3, center);
  drawMeshLine(base, tail, fin, frame, "#dbe5eb", 2.5, center);
  drawMeshLine(base, nose, cue, frame, "#f59e0b", 2, center);
  const p = project(base, center);
  if (p.visible) {{
    ctx.fillStyle = "#f59e0b";
    ctx.beginPath();
    ctx.arc(p.x, p.y, 4.5, 0, Math.PI * 2);
    ctx.fill();
  }}
}}

function drawPathSegment(start, end, color, width, center) {{
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  for (let i = start; i <= end; i++) {{
    const p = project(worldPoint(frames[i]), center);
    if (i === start) ctx.moveTo(p.x, p.y);
    else ctx.lineTo(p.x, p.y);
  }}
  ctx.stroke();
}}

function drawPath(center) {{
  drawPathSegment(0, frames.length - 1, "rgba(182, 194, 201, 0.26)", 2, center);
  drawPathSegment(0, frameIndex, TRAIL_COLOR, 3, center);
}}

function drawGroundGrid(center) {{
  const b = data.bounds;
  const minE = b.min_e - 20;
  const maxE = b.max_e + 20;
  const minN = b.min_n - 20;
  const maxN = b.max_n + 20;
  const step = Math.max(10, Math.pow(10, Math.floor(Math.log10(Math.max(maxE - minE, maxN - minN, 1))) - 1));
  ctx.strokeStyle = "#263039";
  ctx.lineWidth = 1;
  for (let e = Math.floor(minE / step) * step; e <= maxE; e += step) {{
    line({{x: e, y: b.min_alt, z: minN}}, {{x: e, y: b.min_alt, z: maxN}}, "#263039", 1, center);
  }}
  for (let n = Math.floor(minN / step) * step; n <= maxN; n += step) {{
    line({{x: minE, y: b.min_alt, z: n}}, {{x: maxE, y: b.min_alt, z: n}}, "#263039", 1, center);
  }}
}}

function updateMetrics(frame) {{
  for (const [id, , formatter] of metricRows) {{
    document.getElementById(`metric-${{id}}`).textContent = formatter(frame);
  }}
  zoomLabel.textContent = `${{zoomMultipliers[Number(zoom.value)]}}x`;
}}

function draw() {{
  ctx.clearRect(0, 0, canvas.clientWidth, canvas.clientHeight);
  const frame = frames[frameIndex];
  const center = sceneCenter();
  drawGroundGrid(center);
  drawPath(center);
  drawAxes(worldPoint(frames[0]), center);
  drawAircraftMesh(frame, center);
  scrub.value = String(frameIndex);
  timeLabel.textContent = `t=${{frame.time_s.toFixed(2)}}s`;
  altitudeLabel.textContent = `alt=${{frame.altitude_m.toFixed(1)}}m`;
  airspeedLabel.textContent = frame.airspeed_mps === undefined ? "" : `air=${{frame.airspeed_mps.toFixed(1)}}m/s`;
  updateMetrics(frame);
}}

function stepTo(index) {{
  frameIndex = Math.max(0, Math.min(frames.length - 1, index));
  draw();
}}

function tick(nowMs) {{
  if (!lastTickMs) lastTickMs = nowMs;
  const elapsedS = ((nowMs - lastTickMs) / 1000) * Number(speed.value);
  lastTickMs = nowMs;
  if (playing && frames.length > 1) {{
    simCarryS += elapsedS;
    while (frameIndex < frames.length - 1) {{
      const nextDt = Math.max(0.001, frames[frameIndex + 1].time_s - frames[frameIndex].time_s);
      if (simCarryS < nextDt) break;
      simCarryS -= nextDt;
      frameIndex += 1;
    }}
    if (frameIndex >= frames.length - 1) {{
      playing = false;
      play.textContent = "Play";
      simCarryS = 0;
    }}
    draw();
  }}
  requestAnimationFrame(tick);
}}

play.addEventListener("click", () => {{
  if (frameIndex >= frames.length - 1) stepTo(0);
  playing = !playing;
  play.textContent = playing ? "Pause" : "Play";
  lastTickMs = 0;
}});
scrub.addEventListener("input", () => {{
  playing = false;
  play.textContent = "Play";
  simCarryS = 0;
  stepTo(Number(scrub.value));
}});
lock.addEventListener("change", draw);
zoom.addEventListener("input", draw);
window.addEventListener("resize", resize);
resize();
requestAnimationFrame(tick);
</script>
</body>
</html>
"""


def main():
    args = parse_args()
    try:
        frames = load_frames(args.csv_path)
        output_path = Path(args.output)
        output_path.write_text(
            render_html(args.title, Path(args.csv_path).name, frames), encoding="utf-8"
        )
        print(f"output={output_path}")
        print(f"frames={len(frames)}")
    except (OSError, ValueError) as exc:
        print(f"visualize_sitl_3d.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
