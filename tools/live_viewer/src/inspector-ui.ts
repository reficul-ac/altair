import { fmt } from './hud-ui';
import type { InspectorMessage, SessionEvent, SessionSnapshotMessage } from './state';

type ChartPoint = { t: number; v: number };
type InspectorLogRow = {
  snapshotTimeS: number;
  message: InspectorMessage;
  field: string;
  value: number | string | null;
};

let selectedKey: string | null = null;
let lastSnapshot: SessionSnapshotMessage | null = null;
let selectedMessage: InspectorMessage | null = null;
const selectedChartFields = new Set<string>();
const chartSeries = new Map<string, ChartPoint[]>();
const inspectorLog: InspectorLogRow[] = [];

export function filterInspectorMessages(messages: readonly InspectorMessage[], query: string): InspectorMessage[] {
  const needle = query.trim().toLowerCase();
  if (!needle) return [...messages];
  return messages.filter((message) => {
    const src = `${message.systemId}:${message.componentId}`;
    return message.name.toLowerCase().includes(needle) || src.includes(needle) || String(message.msgId).includes(needle);
  });
}

export function numericFieldNames(message: InspectorMessage | null): string[] {
  if (!message) return [];
  return Object.entries(message.fields).filter(([, value]) => typeof value === 'number' && Number.isFinite(value)).map(([key]) => key);
}

export function addInspectorSamples(message: InspectorMessage, fields: Iterable<string>, nowMs = performance.now()): void {
  for (const field of fields) {
    const value = message.fields[field];
    if (typeof value !== 'number' || !Number.isFinite(value)) continue;
    const key = `${message.key}:${field}`;
    const series = chartSeries.get(key) ?? [];
    series.push({ t: nowMs, v: value });
    while (series.length > 160) series.shift();
    chartSeries.set(key, series);
  }
}

export function buildInspectorCsv(message: InspectorMessage | null, fields: readonly string[]): string {
  if (!message || fields.length === 0) return 'message,source,field,t_ms,value\n';
  const rows = ['message,source,field,t_ms,value'];
  for (const field of fields) {
    const series = chartSeries.get(`${message.key}:${field}`) ?? [];
    for (const point of series) {
      rows.push([csvCell(message.name), csvCell(`${message.systemId}:${message.componentId}`), csvCell(field), point.t.toFixed(0), String(point.v)].join(','));
    }
  }
  return `${rows.join('\n')}\n`;
}

export function recordInspectorSnapshot(snapshot: SessionSnapshotMessage, timestampS = performance.now() / 1000): void {
  for (const message of snapshot.messages) {
    for (const [field, value] of Object.entries(message.fields)) {
      inspectorLog.push({ snapshotTimeS: timestampS, message, field, value });
    }
  }
  while (inspectorLog.length > 120_000) inspectorLog.splice(0, inspectorLog.length - 120_000);
}

export function clearInspectorLog(): void {
  inspectorLog.length = 0;
}

export function buildInspectorLogCsv(): string {
  const rows = ['snapshot_s,message,source,msg_id,field,value,count,rate_hz'];
  for (const row of inspectorLog) {
    rows.push([
      row.snapshotTimeS.toFixed(3),
      csvCell(row.message.name),
      csvCell(`${row.message.systemId}:${row.message.componentId}`),
      String(row.message.msgId),
      csvCell(row.field),
      csvCell(String(row.value ?? '')),
      String(row.message.count),
      row.message.rateHz.toFixed(3)
    ].join(','));
  }
  return `${rows.join('\n')}\n`;
}

export function updateInspector(snapshot: SessionSnapshotMessage): void {
  bindInspectorChrome();
  lastSnapshot = snapshot;
  const table = document.querySelector<HTMLElement>('#inspector-table')!;
  const filter = document.querySelector<HTMLInputElement>('#inspector-filter')?.value ?? '';
  const visibleMessages = filterInspectorMessages(snapshot.messages, filter);
  if (selectedKey === null && visibleMessages[0]) {
    selectedKey = visibleMessages[0].key;
  }
  if (selectedKey !== null && !visibleMessages.some((message) => message.key === selectedKey)) {
    selectedKey = visibleMessages[0]?.key ?? null;
  }
  table.innerHTML = visibleMessages.map((message) => {
    const active = message.key === selectedKey ? 'active' : '';
    return `<button type="button" class="inspector-row ${active}" data-message-key="${escapeAttr(message.key)}">
      <span>${escapeHtml(message.name)}</span><span>${message.systemId}:${message.componentId}</span><span>${fmt(message.rateHz, ' Hz', 1)}</span><span>${message.count}</span>
    </button>`;
  }).join('') || '<p class="empty">No decoded MAVLink messages</p>';
  table.querySelectorAll<HTMLButtonElement>('[data-message-key]').forEach((button) => {
    button.addEventListener('click', () => {
      selectedKey = button.dataset.messageKey ?? null;
      selectedChartFields.clear();
      updateInspector(snapshot);
    });
  });
  const selected = snapshot.messages.find((message) => message.key === selectedKey) ?? visibleMessages[0] ?? null;
  updateDetail(selected);
  updateEvents(snapshot.events);
}

function bindInspectorChrome(): void {
  const filter = document.querySelector<HTMLInputElement>('#inspector-filter');
  if (filter && filter.dataset.bound !== 'true') {
    filter.dataset.bound = 'true';
    filter.addEventListener('input', () => {
      selectedKey = null;
      if (lastSnapshot) updateInspector(lastSnapshot);
    });
  }
  const exportButton = document.querySelector<HTMLButtonElement>('#inspector-export');
  if (exportButton && exportButton.dataset.bound !== 'true') {
    exportButton.dataset.bound = 'true';
    exportButton.addEventListener('click', () => exportSelectedCsv());
  }
  const logExportButton = document.querySelector<HTMLButtonElement>('#inspector-log-export');
  if (logExportButton && logExportButton.dataset.bound !== 'true') {
    logExportButton.dataset.bound = 'true';
    logExportButton.addEventListener('click', () => exportInspectorLogCsv());
  }
}

function updateDetail(message: InspectorMessage | null): void {
  selectedMessage = message;
  const detail = document.querySelector<HTMLElement>('#message-detail')!;
  const chartButtons = document.querySelector<HTMLElement>('#chart-fields')!;
  if (!message) {
    detail.innerHTML = '<p class="empty">Select a MAVLink message</p>';
    chartButtons.innerHTML = '';
    drawChart([]);
    return;
  }
  const fields = Object.entries(message.fields);
  detail.innerHTML = fields.map(([key, value]) => `<div><dt>${escapeHtml(key)}</dt><dd>${escapeHtml(String(value ?? '--'))}</dd></div>`).join('') || '<p class="empty">No decoded fields</p>';
  const numericFields = numericFieldNames(message);
  if (selectedChartFields.size === 0 && numericFields[0]) {
    selectedChartFields.add(numericFields[0]);
  }
  for (const field of [...selectedChartFields]) {
    if (!numericFields.includes(field)) selectedChartFields.delete(field);
  }
  addInspectorSamples(message, selectedChartFields);
  chartButtons.innerHTML = numericFields.map((field) => `<button type="button" class="${selectedChartFields.has(field) ? 'active' : ''}" data-chart-field="${escapeAttr(field)}">${escapeHtml(field)}</button>`).join('');
  chartButtons.querySelectorAll<HTMLButtonElement>('[data-chart-field]').forEach((button) => {
    button.addEventListener('click', () => {
      const field = button.dataset.chartField;
      if (!field) return;
      if (selectedChartFields.has(field)) selectedChartFields.delete(field);
      else selectedChartFields.add(field);
      updateDetail(message);
    });
  });
  const overlays = [...selectedChartFields].map((field) => ({ field, series: chartSeries.get(`${message.key}:${field}`) ?? [] }));
  drawChart(overlays);
}

function drawChart(overlays: { field: string; series: ChartPoint[] }[]): void {
  const canvas = document.querySelector<HTMLCanvasElement>('#field-chart')!;
  const ctx = canvas.getContext('2d')!;
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#0b1116';
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = 'rgba(138, 161, 178, 0.28)';
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
  const populated = overlays.filter((overlay) => overlay.series.length >= 2);
  if (populated.length === 0) return;
  const values = populated.flatMap((overlay) => overlay.series.map((point) => point.v));
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = Math.max(0.0001, max - min);
  const colors = ['#66e0a3', '#3aa0ff', '#ffc857', '#ff6b7a', '#e464ff'];
  populated.forEach((overlay, overlayIndex) => {
    ctx.strokeStyle = colors[overlayIndex % colors.length];
    ctx.lineWidth = 2;
    ctx.beginPath();
    overlay.series.forEach((point, index) => {
      const x = (index / (overlay.series.length - 1)) * (width - 16) + 8;
      const y = height - 8 - ((point.v - min) / span) * (height - 16);
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.fillStyle = colors[overlayIndex % colors.length];
    ctx.fillText(overlay.field, 10, 18 + overlayIndex * 14);
  });
}

function exportSelectedCsv(): void {
  const csv = buildInspectorCsv(selectedMessage, [...selectedChartFields]);
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = `${selectedMessage?.name ?? 'mavlink'}-inspector.csv`;
  link.click();
  URL.revokeObjectURL(url);
}

function exportInspectorLogCsv(): void {
  const csv = buildInspectorLogCsv();
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = 'altair-inspector-log.csv';
  link.click();
  URL.revokeObjectURL(url);
}

function updateEvents(events: SessionEvent[]): void {
  const log = document.querySelector<HTMLElement>('#event-log')!;
  log.innerHTML = events.slice(0, 80).map((event) => `<li class="${event.level}"><span>${escapeHtml(event.kind)}</span><strong>${escapeHtml(event.label)}</strong><small>${escapeHtml(event.vehicleId ?? 'session')}</small></li>`).join('') || '<li class="empty">No events</li>';
}

function csvCell(value: string): string {
  return `"${value.replaceAll('"', '""')}"`;
}

function escapeHtml(value: string): string {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&#39;');
}

function escapeAttr(value: string): string {
  return escapeHtml(value);
}
