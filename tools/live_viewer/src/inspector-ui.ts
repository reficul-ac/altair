import { fmt } from './hud-ui';
import type { InspectorMessage, SessionEvent, SessionSnapshotMessage } from './state';

let selectedKey: string | null = null;
let selectedChartField: string | null = null;
const chartSeries = new Map<string, { t: number; v: number }[]>();

export function updateInspector(snapshot: SessionSnapshotMessage): void {
  const table = document.querySelector<HTMLElement>('#inspector-table')!;
  if (selectedKey === null && snapshot.messages[0]) {
    selectedKey = snapshot.messages[0].key;
  }
  table.innerHTML = snapshot.messages.map((message) => {
    const active = message.key === selectedKey ? 'active' : '';
    return `<button type="button" class="inspector-row ${active}" data-message-key="${message.key}">
      <span>${message.name}</span><span>${message.systemId}:${message.componentId}</span><span>${fmt(message.rateHz, ' Hz', 1)}</span><span>${message.count}</span>
    </button>`;
  }).join('') || '<p class="empty">No decoded MAVLink messages</p>';
  table.querySelectorAll<HTMLButtonElement>('[data-message-key]').forEach((button) => {
    button.addEventListener('click', () => {
      selectedKey = button.dataset.messageKey ?? null;
      updateInspector(snapshot);
    });
  });
  const selected = snapshot.messages.find((message) => message.key === selectedKey) ?? snapshot.messages[0] ?? null;
  updateDetail(selected);
  updateEvents(snapshot.events);
}

function updateDetail(message: InspectorMessage | null): void {
  const detail = document.querySelector<HTMLElement>('#message-detail')!;
  const chartButtons = document.querySelector<HTMLElement>('#chart-fields')!;
  if (!message) {
    detail.innerHTML = '<p class="empty">Select a MAVLink message</p>';
    chartButtons.innerHTML = '';
    drawChart([]);
    return;
  }
  const fields = Object.entries(message.fields);
  detail.innerHTML = fields.map(([key, value]) => `<div><dt>${key}</dt><dd>${value ?? '--'}</dd></div>`).join('') || '<p class="empty">No decoded fields</p>';
  const numericFields = fields.filter(([, value]) => typeof value === 'number');
  if (!selectedChartField || !numericFields.some(([field]) => field === selectedChartField)) {
    selectedChartField = numericFields[0]?.[0] ?? null;
  }
  chartButtons.innerHTML = numericFields.map(([field]) => `<button type="button" class="${field === selectedChartField ? 'active' : ''}" data-chart-field="${field}">${field}</button>`).join('');
  chartButtons.querySelectorAll<HTMLButtonElement>('[data-chart-field]').forEach((button) => {
    button.addEventListener('click', () => {
      selectedChartField = button.dataset.chartField ?? null;
      updateDetail(message);
    });
  });
  if (selectedChartField) {
    const value = message.fields[selectedChartField];
    const key = `${message.key}:${selectedChartField}`;
    if (typeof value === 'number') {
      const series = chartSeries.get(key) ?? [];
      series.push({ t: performance.now(), v: value });
      while (series.length > 80) series.shift();
      chartSeries.set(key, series);
      drawChart(series);
    }
  } else {
    drawChart([]);
  }
}

function drawChart(series: { t: number; v: number }[]): void {
  const canvas = document.querySelector<HTMLCanvasElement>('#field-chart')!;
  const ctx = canvas.getContext('2d')!;
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#0b1116';
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = 'rgba(138, 161, 178, 0.28)';
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
  if (series.length < 2) return;
  const values = series.map((point) => point.v);
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = Math.max(0.0001, max - min);
  ctx.strokeStyle = '#66e0a3';
  ctx.lineWidth = 2;
  ctx.beginPath();
  series.forEach((point, index) => {
    const x = (index / (series.length - 1)) * (width - 16) + 8;
    const y = height - 8 - ((point.v - min) / span) * (height - 16);
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

function updateEvents(events: SessionEvent[]): void {
  const log = document.querySelector<HTMLElement>('#event-log')!;
  log.innerHTML = events.slice(0, 80).map((event) => `<li class="${event.level}"><span>${event.kind}</span><strong>${event.label}</strong><small>${event.vehicleId ?? 'session'}</small></li>`).join('') || '<li class="empty">No events</li>';
}
