import { describe, expect, it, vi } from 'vitest';
import { frameIndexAtOrBefore, importLogAsReplay, normalizeReplay, parseAltairReplayJson, ReplaySession, serializeAltairReplay, type ReplayFrame } from './replay';
import type { SessionSnapshotMessage, VehicleStateMessage } from './state';

const vehicle: VehicleStateMessage = {
  type: 'vehicle_state',
  id: '1:1',
  connected: true,
  packetAgeS: 0,
  heartbeatAgeS: 0,
  systemId: 1,
  componentId: 1,
  vehicleType: 'Fixed-wing',
  attitude: { rollRad: 0, pitchRad: 0, yawRad: 0, rollRateRps: 0, pitchRateRps: 0, yawRateRps: 0 },
  globalPosition: { latDeg: 37, lonDeg: -122, altitudeM: 100, relativeAltitudeM: 10, originLatDeg: 37, originLonDeg: -122, originAltitudeM: 90 },
  localPosition: { northM: 0, eastM: 0, upM: 10 },
  velocity: { northMps: 0, eastMps: 0, downMps: 0 },
  metrics: { headingDeg: 90, airspeedMps: 12, groundspeedMps: 11, climbMps: 0, throttlePct: 0.4 },
  status: { armed: false, mode: 'MANUAL', baseMode: 0, customMode: 0, gpsFix: '3D fix', satellitesVisible: 10, batteryRemainingPct: 90, batteryVoltageV: 12.1, onboardControlSensorsHealth: 1, missionSeq: 0, lastStatusText: null }
};

function snapshot(index: number): SessionSnapshotMessage {
  return {
    type: 'session_snapshot',
    vehicles: [{ ...vehicle, localPosition: { northM: index, eastM: index * 2, upM: 10 }, status: { ...vehicle.status!, armed: index > 0 } }],
    selectedVehicleId: '1:1',
    messages: [{
      key: '1:1:30',
      msgId: 30,
      name: 'ATTITUDE',
      systemId: 1,
      componentId: 1,
      lastAgeS: 0,
      rateHz: 10,
      count: index + 1,
      fields: { rollRad: index / 10 }
    }],
    events: index === 1 ? [{ id: 'marker-1', timestampS: 1, vehicleId: '1:1', level: 'info', kind: 'marker', label: 'Gate', position: { eastM: 2, northM: 1, upM: 10 } }] : [],
    packetCount: index + 1,
    decodedCount: index + 1
  };
}

function frames(): ReplayFrame[] {
  return [
    { timestampS: 0, snapshot: snapshot(0) },
    { timestampS: 1, snapshot: snapshot(1) },
    { timestampS: 3, snapshot: snapshot(2) }
  ];
}

describe('replay sessions', () => {
  it('parses and normalizes Altair replay metadata and markers', () => {
    const replay = parseAltairReplayJson(JSON.stringify(serializeAltairReplay([...frames()].reverse(), { createdAt: '2026-05-14T00:00:00.000Z' })));
    expect(replay.metadata.frameCount).toBe(3);
    expect(replay.metadata.durationS).toBe(3);
    expect(replay.metadata.vehicleIds).toEqual(['1:1']);
    expect(replay.frames.map((frame) => frame.timestampS)).toEqual([0, 1, 3]);
    expect(replay.markers.map((event) => event.label)).toEqual(['Gate']);
  });

  it('rejects unsupported Altair replay schema versions', () => {
    const replay = { ...serializeAltairReplay(frames()), schemaVersion: 2 };
    expect(() => parseAltairReplayJson(JSON.stringify(replay))).toThrow('unsupported replay schema version 2');
  });

  it('rejects replay frames with missing required snapshot fields', () => {
    const replay = serializeAltairReplay(frames());
    delete (replay.frames[0].snapshot as Partial<SessionSnapshotMessage>).packetCount;
    expect(() => normalizeReplay(replay)).toThrow('replay frame 0: session snapshot packetCount must be a finite number');
  });

  it('normalizes snapshots that omit optional session domains', () => {
    const replay = parseAltairReplayJson(JSON.stringify({
      type: 'altair_session_replay',
      schemaVersion: 1,
      frames: frames()
    }));
    expect(replay.frames[0].snapshot.logSources).toEqual([]);
    expect(replay.frames[0].snapshot.commandAudit).toEqual([]);
    expect(replay.compatibilityWarnings).toContain('replay frame 0: session snapshot missing optional logSources; normalized to empty array');
  });

  it('finds the frame at or before a seek timestamp', () => {
    expect(frameIndexAtOrBefore(frames(), 0.5)).toBe(0);
    expect(frameIndexAtOrBefore(frames(), 1)).toBe(1);
    expect(frameIndexAtOrBefore(frames(), 2.9)).toBe(1);
    expect(frameIndexAtOrBefore(frames(), 9)).toBe(2);
  });

  it('seeks, resets, and navigates marker timestamps', () => {
    const session = new ReplaySession();
    session.load(serializeAltairReplay(frames()));
    expect(session.seek(2).frameIndex).toBe(1);
    expect(session.seekMarker(-1).timestampS).toBe(1);
    expect(session.reset().timestampS).toBe(0);
    expect(session.seekMarker(1).timestampS).toBe(1);
  });

  it('keeps selected vehicles aligned across replay frames', () => {
    const secondVehicle = { ...vehicle, id: '2:1', systemId: 2 };
    const replayFrames = frames().map((frame) => ({
      ...frame,
      snapshot: { ...frame.snapshot, vehicles: [...frame.snapshot.vehicles, secondVehicle] }
    }));
    const session = new ReplaySession();
    session.load(serializeAltairReplay(replayFrames));
    expect(session.selectVehicle('2:1')?.selectedVehicleId).toBe('2:1');
    expect(session.seek(3).frameIndex).toBe(2);
    expect(session.selectVehicle('missing')?.selectedVehicleId).toBe('2:1');
  });

  it('plays frames according to speed and pauses deterministically', () => {
    vi.useFakeTimers();
    try {
      const session = new ReplaySession();
      const emitted: number[] = [];
      session.on('session-snapshot', (frame) => emitted.push(frame.packetCount));
      session.load(serializeAltairReplay(frames()));
      expect(session.setSpeed(2).speed).toBe(2);
      expect(session.play().playing).toBe(true);
      vi.advanceTimersByTime(499);
      expect(emitted).toEqual([1]);
      vi.advanceTimersByTime(1);
      expect(emitted).toEqual([1, 2]);
      expect(session.pause().playing).toBe(false);
      vi.advanceTimersByTime(2000);
      expect(emitted).toEqual([1, 2]);
    } finally {
      vi.useRealTimers();
    }
  });

  it('imports ULog-style delimited samples into deterministic replay frames', () => {
    const replay = importLogAsReplay({
      name: 'fixture.ulg',
      sourceType: 'ulog-import',
      importedAt: '2026-05-14T00:00:00.000Z',
      text: 'timestamp_s,vehicle_id,north_m,east_m,up_m,roll_rad,mode\n0,1:1,0,0,10,0,MANUAL\n1.5,1:1,3,4,12,0.2,AUTO\n'
    });
    expect(replay.metadata?.sourceType).toBe('ulog-import');
    expect(replay.metadata?.importedFrom).toBe('fixture.ulg');
    expect(replay.frames.map((frame) => frame.timestampS)).toEqual([0, 1.5]);
    expect(replay.frames[1].snapshot.vehicles[0].localPosition).toEqual({ northM: 3, eastM: 4, upM: 12 });
    expect(replay.frames[1].snapshot.vehicles[0].status?.mode).toBe('AUTO');
    expect(() => normalizeReplay(replay)).not.toThrow();
  });
});
