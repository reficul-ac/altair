import type { SessionSnapshotMessage, VehicleStateMessage } from './state';

export function drawMap(snapshot: SessionSnapshotMessage): void {
  const canvas = document.querySelector<HTMLCanvasElement>('#map-canvas')!;
  const ctx = canvas.getContext('2d')!;
  const width = canvas.width;
  const height = canvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#0b1116';
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = 'rgba(138, 161, 178, 0.18)';
  ctx.lineWidth = 1;
  for (let x = 0; x <= width; x += 40) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
  for (let y = 0; y <= height; y += 40) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
  const center = selectedVehicle(snapshot) ?? snapshot.vehicles[0] ?? null;
  const centerEast = center?.localPosition.eastM ?? 0;
  const centerNorth = center?.localPosition.northM ?? 0;
  const scale = 2;
  for (const vehicle of snapshot.vehicles) {
    drawTrail(ctx, vehicle, centerEast, centerNorth, scale, width, height, vehicle.id === snapshot.selectedVehicleId);
  }
  for (const event of snapshot.events.slice().reverse()) {
    if (!event.position) continue;
    const x = width / 2 + (event.position.eastM - centerEast) * scale;
    const y = height / 2 - (event.position.northM - centerNorth) * scale;
    if (x < -10 || x > width + 10 || y < -10 || y > height + 10) continue;
    ctx.fillStyle = event.level === 'warning' ? '#ffc857' : event.level === 'error' ? '#ff6b7a' : '#3aa0ff';
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();
  }
}

function selectedVehicle(snapshot: SessionSnapshotMessage): VehicleStateMessage | null {
  return snapshot.vehicles.find((vehicle) => vehicle.id === snapshot.selectedVehicleId) ?? null;
}

function drawTrail(
  ctx: CanvasRenderingContext2D,
  vehicle: VehicleStateMessage,
  centerEast: number,
  centerNorth: number,
  scale: number,
  width: number,
  height: number,
  selected: boolean
): void {
  const trail = vehicle.trail ?? [];
  ctx.strokeStyle = selected ? '#66e0a3' : 'rgba(58, 160, 255, 0.55)';
  ctx.lineWidth = selected ? 2 : 1.2;
  ctx.beginPath();
  trail.forEach((point, index) => {
    const x = width / 2 + (point.eastM - centerEast) * scale;
    const y = height / 2 - (point.northM - centerNorth) * scale;
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
  const east = vehicle.localPosition.eastM;
  const north = vehicle.localPosition.northM;
  if (east === null || north === null) return;
  const x = width / 2 + (east - centerEast) * scale;
  const y = height / 2 - (north - centerNorth) * scale;
  ctx.fillStyle = selected ? '#ffc857' : '#3aa0ff';
  ctx.beginPath();
  ctx.arc(x, y, selected ? 6 : 4, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = '#e7eef3';
  ctx.font = '12px Inter, sans-serif';
  ctx.fillText(vehicle.id ?? `${vehicle.systemId}:${vehicle.componentId}`, x + 8, y - 8);
}
