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
OPTIONAL_COLUMNS = ("roll_rad", "pitch_rad", "yaw_rad", "airspeed_mps", "mode")


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
  background: #12161a;
  color: #f2f6f8;
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  min-height: 100vh;
  display: grid;
  grid-template-rows: auto 1fr auto;
}}
header, footer {{
  padding: 12px 18px;
  background: #1b2228;
  border-color: #2f3940;
}}
header {{ border-bottom: 1px solid #2f3940; }}
footer {{
  border-top: 1px solid #2f3940;
  display: flex;
  flex-wrap: wrap;
  gap: 10px 18px;
  align-items: center;
}}
h1 {{
  margin: 0;
  font-size: 18px;
  font-weight: 700;
}}
.subtle {{ color: #aebbc3; font-size: 12px; margin-top: 4px; }}
main {{ min-height: 0; }}
canvas {{
  display: block;
  width: 100%;
  height: 100%;
  background: #101417;
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
  accent-color: #6fc3df;
}}
.metric {{
  font-variant-numeric: tabular-nums;
  min-width: 110px;
}}
</style>
</head>
<body>
<header>
  <h1>{escaped_title}</h1>
  <div class="subtle" id="source"></div>
</header>
<main><canvas id="view"></canvas></main>
<footer>
  <button id="play" type="button">Play</button>
  <input id="scrub" type="range" min="0" max="0" value="0" step="1" aria-label="Playback frame">
  <label>Speed <select id="speed">
    <option value="0.25">0.25x</option>
    <option value="0.5">0.5x</option>
    <option value="1" selected>1x</option>
    <option value="2">2x</option>
    <option value="4">4x</option>
  </select></label>
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
const timeLabel = document.getElementById("time");
const altitudeLabel = document.getElementById("altitude");
const airspeedLabel = document.getElementById("airspeed");
document.getElementById("source").textContent = `${{data.csvName}} - ${{frames.length}} frames`;
scrub.max = String(frames.length - 1);

let frameIndex = 0;
let playing = false;
let lastTickMs = 0;
let simCarryS = 0;

function resize() {{
  const rect = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * scale));
  canvas.height = Math.max(1, Math.floor(rect.height * scale));
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  draw();
}}

function scene(frame) {{
  const b = data.bounds;
  const cx = (b.min_e + b.max_e) * 0.5;
  const cy = (b.min_alt + b.max_alt) * 0.5;
  const cz = (b.min_n + b.max_n) * 0.5;
  return {{
    x: frame.pos_e_m - cx,
    y: frame.altitude_m - cy,
    z: frame.pos_n_m - cz
  }};
}}

function scaleForScene() {{
  const b = data.bounds;
  const span = Math.max(b.max_e - b.min_e, b.max_n - b.min_n, b.max_alt - b.min_alt, 1);
  return 260 / span;
}}

function project(point) {{
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  const s = scaleForScene();
  const yaw = -0.75;
  const pitch = -0.55;
  const cy = Math.cos(yaw), sy = Math.sin(yaw);
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const x1 = point.x * cy - point.z * sy;
  const z1 = point.x * sy + point.z * cy;
  const y2 = point.y * cp - z1 * sp;
  const z2 = point.y * sp + z1 * cp;
  const depth = 900 + z2 * s;
  const perspective = 900 / Math.max(depth, 120);
  return {{
    x: w * 0.5 + x1 * s * perspective,
    y: h * 0.55 - y2 * s * perspective,
    visible: depth > 0
  }};
}}

function line(a, b, color, width = 1) {{
  const pa = project(a);
  const pb = project(b);
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
  let y1 = y * c - z * s, z1 = y * s + z * c;
  y = y1; z = z1;
  c = Math.cos(pitch); s = Math.sin(pitch);
  let x1 = x * c + z * s; z1 = -x * s + z * c;
  x = x1; z = z1;
  c = Math.cos(yaw); s = Math.sin(yaw);
  x1 = x * c - y * s; y1 = x * s + y * c;
  return {{x: x1, y: y1, z}};
}}

function add(a, b) {{
  return {{x: a.x + b.x, y: a.y + b.y, z: a.z + b.z}};
}}

function drawAxes(origin) {{
  const len = 80 / scaleForScene();
  line(origin, {{x: origin.x + len, y: origin.y, z: origin.z}}, "#d65b5b", 2);
  line(origin, {{x: origin.x, y: origin.y + len, z: origin.z}}, "#83c779", 2);
  line(origin, {{x: origin.x, y: origin.y, z: origin.z + len}}, "#6fc3df", 2);
}}

function drawAircraft(frame) {{
  const center = scene(frame);
  const size = Math.max(6, 28 / scaleForScene());
  const nose = add(center, rotateBody({{x: 0, y: 0, z: size * 1.5}}, frame));
  const tail = add(center, rotateBody({{x: 0, y: 0, z: -size}}, frame));
  const left = add(center, rotateBody({{x: -size * 1.2, y: 0, z: -size * 0.1}}, frame));
  const right = add(center, rotateBody({{x: size * 1.2, y: 0, z: -size * 0.1}}, frame));
  const fin = add(center, rotateBody({{x: 0, y: size * 0.55, z: -size * 0.8}}, frame));
  line(tail, nose, "#f5f7fa", 3);
  line(left, right, "#f5f7fa", 3);
  line(tail, fin, "#f5f7fa", 2);
}}

function drawPath() {{
  ctx.strokeStyle = "#f0b35a";
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let i = 0; i < frames.length; i++) {{
    const p = project(scene(frames[i]));
    if (i === 0) ctx.moveTo(p.x, p.y);
    else ctx.lineTo(p.x, p.y);
  }}
  ctx.stroke();
  ctx.strokeStyle = "#76d7c4";
  ctx.lineWidth = 3;
  ctx.beginPath();
  for (let i = 0; i <= frameIndex; i++) {{
    const p = project(scene(frames[i]));
    if (i === 0) ctx.moveTo(p.x, p.y);
    else ctx.lineTo(p.x, p.y);
  }}
  ctx.stroke();
}}

function drawGroundGrid() {{
  const b = data.bounds;
  const minE = b.min_e - 20;
  const maxE = b.max_e + 20;
  const minN = b.min_n - 20;
  const maxN = b.max_n + 20;
  const step = Math.max(10, Math.pow(10, Math.floor(Math.log10(Math.max(maxE - minE, maxN - minN, 1))) - 1));
  ctx.strokeStyle = "#263039";
  ctx.lineWidth = 1;
  for (let e = Math.floor(minE / step) * step; e <= maxE; e += step) {{
    line(scene({{pos_e_m: e, pos_n_m: minN, altitude_m: b.min_alt}}), scene({{pos_e_m: e, pos_n_m: maxN, altitude_m: b.min_alt}}), "#263039");
  }}
  for (let n = Math.floor(minN / step) * step; n <= maxN; n += step) {{
    line(scene({{pos_e_m: minE, pos_n_m: n, altitude_m: b.min_alt}}), scene({{pos_e_m: maxE, pos_n_m: n, altitude_m: b.min_alt}}), "#263039");
  }}
}}

function draw() {{
  ctx.clearRect(0, 0, canvas.clientWidth, canvas.clientHeight);
  const frame = frames[frameIndex];
  drawGroundGrid();
  drawPath();
  drawAxes(scene(frames[0]));
  drawAircraft(frame);
  scrub.value = String(frameIndex);
  timeLabel.textContent = `t=${{frame.time_s.toFixed(2)}}s`;
  altitudeLabel.textContent = `alt=${{frame.altitude_m.toFixed(1)}}m`;
  airspeedLabel.textContent = frame.airspeed_mps === undefined ? "" : `air=${{frame.airspeed_mps.toFixed(1)}}m/s`;
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
        output_path.write_text(render_html(args.title, Path(args.csv_path).name, frames), encoding="utf-8")
        print(f"output={output_path}")
        print(f"frames={len(frames)}")
    except (OSError, ValueError) as exc:
        print(f"visualize_sitl_3d.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
