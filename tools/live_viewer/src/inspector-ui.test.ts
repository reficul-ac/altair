import { describe, expect, it } from 'vitest';
import { addInspectorSamples, buildInspectorCsv, filterInspectorMessages, numericFieldNames } from './inspector-ui';
import type { InspectorMessage } from './state';

const attitude: InspectorMessage = {
  key: '1:1:30-test',
  msgId: 30,
  name: 'ATTITUDE',
  systemId: 1,
  componentId: 1,
  lastAgeS: 0,
  rateHz: 10,
  count: 2,
  fields: { rollRad: 0.1, pitchRad: -0.2, label: 'ok', missing: null }
};

const gps: InspectorMessage = {
  ...attitude,
  key: '2:1:24-test',
  msgId: 24,
  name: 'GPS_RAW_INT',
  systemId: 2,
  fields: { latDeg: 37.5, fix: '3D fix' }
};

describe('inspector helpers', () => {
  it('filters messages by name, source, and message id', () => {
    expect(filterInspectorMessages([attitude, gps], 'att').map((message) => message.name)).toEqual(['ATTITUDE']);
    expect(filterInspectorMessages([attitude, gps], '2:1').map((message) => message.name)).toEqual(['GPS_RAW_INT']);
    expect(filterInspectorMessages([attitude, gps], '30').map((message) => message.name)).toEqual(['ATTITUDE']);
  });

  it('extracts numeric fields for selectable chart overlays', () => {
    expect(numericFieldNames(attitude)).toEqual(['rollRad', 'pitchRad']);
    expect(numericFieldNames(null)).toEqual([]);
  });

  it('exports selected numeric samples as csv', () => {
    addInspectorSamples(attitude, ['rollRad', 'pitchRad'], 100);
    addInspectorSamples({ ...attitude, fields: { ...attitude.fields, rollRad: 0.2, pitchRad: -0.3 } }, ['rollRad'], 150);
    const csv = buildInspectorCsv(attitude, ['rollRad']);
    expect(csv).toContain('message,source,field,t_ms,value');
    expect(csv).toContain('"ATTITUDE","1:1","rollRad",100,0.1');
    expect(csv).toContain('"ATTITUDE","1:1","rollRad",150,0.2');
    expect(csv).not.toContain('pitchRad');
  });
});
