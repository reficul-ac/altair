import dgram from 'node:dgram';
import { afterEach, describe, expect, it } from 'vitest';
import {
  decodeCommandAck,
  decodeMavlinkMessage,
  decodeMissionAck,
  decodeMissionRequestInt,
  encodeCommandLong,
  encodeMissionClearAll,
  encodeMissionCount,
  encodeMissionItemInt,
  LiveVehicleState,
  MavlinkTelemetryService,
  MavlinkV1Parser,
  mavlinkV1Frame,
  mavTypeLabel,
  VehicleRegistry
} from './mavlink';
import type { CommandAuditEntry } from './state';

const sockets: dgram.Socket[] = [];

afterEach(() => {
  while (sockets.length > 0) {
    sockets.pop()?.close();
  }
});

function heartbeat(seq = 1, options: { autopilot?: number; baseMode?: number; customMode?: number; systemStatus?: number; systemId?: number; componentId?: number } = {}): Buffer {
  const payload = Buffer.alloc(9);
  payload.writeUInt32LE(options.customMode ?? 0, 0);
  payload.writeUInt8(1, 4);
  payload.writeUInt8(options.autopilot ?? 0, 5);
  payload.writeUInt8(options.baseMode ?? 0, 6);
  payload.writeUInt8(options.systemStatus ?? 4, 7);
  payload.writeUInt8(3, 8);
  return mavlinkV1Frame(0, payload, seq, options.systemId ?? 1, options.componentId ?? 1);
}

function commandAck(command: number, result: number, seq = 20, systemId = 1, componentId = 1): Buffer {
  const payload = Buffer.alloc(3);
  payload.writeUInt16LE(command, 0);
  payload.writeUInt8(result, 2);
  return mavlinkV1Frame(77, payload, seq, systemId, componentId);
}

function makeWritable(service: MavlinkTelemetryService): Buffer[] {
  const sent: Buffer[] = [];
  const writable = service as unknown as {
    socket: { send: (frame: Buffer, port: number, host: string) => void };
    lastRemote: { host: string; port: number };
  };
  writable.socket = { send: (frame: Buffer) => sent.push(frame) };
  writable.lastRemote = { host: '127.0.0.1', port: 14540 };
  return sent;
}

function attitude(seq = 2): Buffer {
  const payload = Buffer.alloc(28);
  payload.writeUInt32LE(100, 0);
  payload.writeFloatLE(0.1, 4);
  payload.writeFloatLE(-0.2, 8);
  payload.writeFloatLE(1.3, 12);
  payload.writeFloatLE(0.01, 16);
  payload.writeFloatLE(0.02, 20);
  payload.writeFloatLE(0.03, 24);
  return mavlinkV1Frame(30, payload, seq);
}

function globalPosition(latDeg: number, lonDeg: number, altM: number, seq = 3): Buffer {
  const payload = Buffer.alloc(28);
  payload.writeUInt32LE(100, 0);
  payload.writeInt32LE(Math.round(latDeg * 1e7), 4);
  payload.writeInt32LE(Math.round(lonDeg * 1e7), 8);
  payload.writeInt32LE(Math.round(altM * 1000), 12);
  payload.writeInt32LE(Math.round(altM * 1000), 16);
  payload.writeInt16LE(1800, 20);
  payload.writeInt16LE(100, 22);
  payload.writeInt16LE(-20, 24);
  payload.writeUInt16LE(13000, 26);
  return mavlinkV1Frame(33, payload, seq);
}

function vfrHud(seq = 4): Buffer {
  const payload = Buffer.alloc(20);
  payload.writeFloatLE(18.5, 0);
  payload.writeFloatLE(18.1, 4);
  payload.writeInt16LE(130, 8);
  payload.writeUInt16LE(0, 10);
  payload.writeFloatLE(151, 12);
  payload.writeFloatLE(0.2, 16);
  return mavlinkV1Frame(74, payload, seq);
}

function localPosition(seq = 5, systemId = 1, componentId = 1): Buffer {
  const payload = Buffer.alloc(28);
  payload.writeUInt32LE(120, 0);
  payload.writeFloatLE(12, 4);
  payload.writeFloatLE(3, 8);
  payload.writeFloatLE(-4, 12);
  payload.writeFloatLE(1.5, 16);
  payload.writeFloatLE(0.5, 20);
  payload.writeFloatLE(-0.2, 24);
  return mavlinkV1Frame(32, payload, seq, systemId, componentId);
}

function sysStatus(seq = 6, options: { sensorHealth?: number; voltageMv?: number; batteryRemainingPct?: number; systemId?: number; componentId?: number } = {}): Buffer {
  const payload = Buffer.alloc(31);
  payload.writeUInt32LE(7, 0);
  payload.writeUInt32LE(7, 4);
  payload.writeUInt32LE(options.sensorHealth ?? 5, 8);
  payload.writeUInt16LE(320, 12);
  payload.writeUInt16LE(options.voltageMv ?? 12100, 14);
  payload.writeInt16LE(340, 16);
  payload.writeInt8(options.batteryRemainingPct ?? 72, 30);
  return mavlinkV1Frame(1, payload, seq, options.systemId ?? 1, options.componentId ?? 1);
}

function gpsRaw(seq = 7, systemId = 1, componentId = 1): Buffer {
  const payload = Buffer.alloc(30);
  payload.writeBigUInt64LE(1000n, 0);
  payload.writeInt32LE(Math.round(37.5 * 1e7), 8);
  payload.writeInt32LE(Math.round(-122.2 * 1e7), 12);
  payload.writeInt32LE(150000, 16);
  payload.writeUInt16LE(120, 20);
  payload.writeUInt16LE(130, 22);
  payload.writeUInt16LE(2100, 24);
  payload.writeUInt16LE(9000, 26);
  payload.writeUInt8(3, 28);
  payload.writeUInt8(11, 29);
  return mavlinkV1Frame(24, payload, seq, systemId, componentId);
}

function missionCurrent(seq = 8, systemId = 1, componentId = 1): Buffer {
  const payload = Buffer.alloc(2);
  payload.writeUInt16LE(4, 0);
  return mavlinkV1Frame(42, payload, seq, systemId, componentId);
}

function missionCount(count = 6, seq = 11, systemId = 1, componentId = 1): Buffer {
  const payload = Buffer.alloc(4);
  payload.writeUInt16LE(count, 0);
  payload.writeUInt8(systemId, 2);
  payload.writeUInt8(componentId, 3);
  return mavlinkV1Frame(44, payload, seq, systemId, componentId);
}

function missionAck(type = 0, seq = 12): Buffer {
  return mavlinkV1Frame(47, Buffer.from([1, 1, type]), seq);
}

function batteryStatus(seq = 9): Buffer {
  const payload = Buffer.alloc(36);
  payload.writeUInt8(0, 0);
  payload.writeUInt8(0, 1);
  payload.writeUInt8(3, 2);
  payload.writeInt16LE(2350, 3);
  payload.writeUInt16LE(3980, 5);
  for (let offset = 7; offset < 25; offset += 2) payload.writeUInt16LE(65535, offset);
  payload.writeInt16LE(420, 25);
  payload.writeInt32LE(1200, 27);
  payload.writeInt32LE(300, 31);
  payload.writeInt8(64, 35);
  return mavlinkV1Frame(147, payload, seq);
}

function statustext(seq = 10): Buffer {
  const payload = Buffer.alloc(51);
  payload.writeUInt8(4, 0);
  payload.write('Ready', 1, 'utf8');
  return mavlinkV1Frame(253, payload, seq);
}

function readyVehicle(systemId = 1, componentId = 1, seq = 1): Buffer {
  return Buffer.concat([
    heartbeat(seq, { autopilot: 12, systemStatus: 4, systemId, componentId }),
    gpsRaw(seq + 1, systemId, componentId),
    sysStatus(seq + 2, { sensorHealth: 5, batteryRemainingPct: 72, voltageMv: 12100, systemId, componentId }),
    missionCount(2, seq + 3, systemId, componentId)
  ]);
}

describe('Electron MAVLink service', () => {
  it('parses MAVLink v1 frames and rejects bad CRCs', () => {
    const parser = new MavlinkV1Parser();
    const frame = attitude();
    const bad = Buffer.from(frame);
    bad[bad.length - 1] ^= 0xff;
    expect(parser.feed(bad)).toHaveLength(0);
    expect(parser.feed(frame)[0]).toMatchObject({ msgId: 30, systemId: 1, componentId: 1 });
  });

  it('decodes supported messages and anchors local position at first global fix', () => {
    const parser = new MavlinkV1Parser();
    const state = new LiveVehicleState();
    for (const message of parser.feed(Buffer.concat([heartbeat(7), attitude(8), globalPosition(37.4275, -122.1697, 151, 9), vfrHud(10)]))) {
      state.apply(message, 10);
    }
    for (const message of parser.feed(globalPosition(37.4276, -122.1696, 153, 11))) {
      state.apply(message, 11);
    }
    const payload = state.toJsonable(11.5);
    expect(payload.heartbeatAgeS).toBe(1.5);
    expect(payload.vehicleType).toBe('Fixed-wing');
    expect(payload.attitude.rollRad).toBeCloseTo(0.1);
    expect(payload.globalPosition.latDeg).toBeCloseTo(37.4276);
    expect(payload.metrics.airspeedMps).toBeCloseTo(18.5);
    expect(payload.localPosition.northM).toBeGreaterThan(10);
    expect(payload.localPosition.eastM).toBeGreaterThan(8);
    expect(payload.localPosition.upM).toBeCloseTo(2);
  });

  it('decodes live debugging MAVLink messages with useful units', () => {
    const parser = new MavlinkV1Parser();
    const frames = [sysStatus(), gpsRaw(), localPosition(), missionCurrent(), batteryStatus(), statustext()];
    const decoded = parser.feed(Buffer.concat(frames)).map((message) => decodeMavlinkMessage(message));
    expect(decoded.map((message) => message.name)).toEqual(['SYS_STATUS', 'GPS_RAW_INT', 'LOCAL_POSITION_NED', 'MISSION_CURRENT', 'BATTERY_STATUS', 'STATUSTEXT']);
    expect(decoded[0].fields.voltageBatteryV).toBeCloseTo(12.1);
    expect(decoded[1].fields.fix).toBe('3D fix');
    expect(decoded[2].fields.zDownM).toBeCloseTo(-4);
    expect(decoded[3].fields.seq).toBe(4);
    expect(decoded[4].fields.batteryRemainingPct).toBe(64);
    expect(decoded[5].fields.text).toBe('Ready');
  });

  it('decodes heartbeat system status and mission status messages', () => {
    const parser = new MavlinkV1Parser();
    const decoded = parser.feed(Buffer.concat([
      heartbeat(1, { autopilot: 12, systemStatus: 5 }),
      missionCount(6, 2),
      missionCurrent(3),
      missionAck(0, 4)
    ])).map((message) => decodeMavlinkMessage(message));
    expect(decoded.map((message) => message.name)).toEqual(['HEARTBEAT', 'MISSION_COUNT', 'MISSION_CURRENT', 'MISSION_ACK']);
    expect(decoded[0].fields.systemStatus).toBe(5);
    expect(decoded[1].fields.count).toBe(6);
    expect(decoded[2].fields.seq).toBe(4);
    expect(decoded[3].fields.type).toBe(0);
  });

  it('propagates MAVLink failsafe and mission state into live vehicle payloads', () => {
    const parser = new MavlinkV1Parser();
    const state = new LiveVehicleState();
    const autoMission = (4 << 16) | (4 << 24);
    for (const message of parser.feed(Buffer.concat([
      heartbeat(1, { autopilot: 12, baseMode: 0x80, customMode: autoMission, systemStatus: 5 }),
      missionCount(6, 2),
      missionCurrent(3),
      missionAck(0, 4)
    ]))) {
      state.apply(message, 10);
    }
    const payload = state.toJsonable(10.5);
    expect(payload.status?.failsafeState).toMatchObject({ status: 'critical', commandBlocking: true });
    expect(payload.status?.missionState).toMatchObject({ activeSeq: 4, totalItems: 6, state: 'active', valid: true });
    expect(payload.status?.missionState?.progressPct).toBeCloseTo(83.333, 2);
    expect(payload.status?.readiness?.overall).toBe('blocked');
    expect(payload.status?.readiness?.checks.find((check) => check.key === 'failsafe')).toMatchObject({ state: 'blocked' });
  });

  it('propagates SYS_STATUS estimator and power gates into readiness and command capabilities', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    const payload = service.handlePacket(Buffer.concat([
      heartbeat(1, { autopilot: 12, systemStatus: 4 }),
      gpsRaw(2),
      sysStatus(3, { sensorHealth: 0, batteryRemainingPct: 8, voltageMv: 12100 }),
      missionCount(2, 4)
    ]));
    expect(payload.status?.readiness?.checks.find((check) => check.key === 'estimator')).toMatchObject({ state: 'blocked' });
    expect(payload.status?.readiness?.checks.find((check) => check.key === 'battery')).toMatchObject({ state: 'blocked' });
    expect(payload.commandCapabilities?.supported).toEqual(expect.arrayContaining(['disarm', 'emergency-stop']));
    expect(payload.commandCapabilities?.supported).not.toContain('arm');
    expect(payload.commandCapabilities?.blockedCommands?.arm).toContain('Estimator blocks commands');
  });

  it('encodes command and mission write packets behind MAVLink v1 CRCs', () => {
    const parser = new MavlinkV1Parser();
    const frames = [
      encodeCommandLong(400, 1, 1, [1]),
      encodeMissionClearAll(1, 1),
      encodeMissionCount(1, 1, 1),
      encodeMissionItemInt({ seq: 0, lat_deg: 37.5, lon_deg: -122.2, alt_m: 120, throttle: 0.55, acceptance_radius_m: 20 }, 1, 1)
    ];
    const decoded = parser.feed(Buffer.concat(frames)).map((message) => decodeMavlinkMessage(message));
    expect(decoded.map((message) => message.name)).toEqual(['COMMAND_LONG', 'MISSION_CLEAR_ALL', 'MISSION_COUNT', 'MISSION_ITEM_INT']);
    expect(decoded[0].fields.command).toBe(400);
    expect(decoded[2].fields.count).toBe(1);
    expect(decoded[3].fields.xLatE7).toBe(375000000);
    expect(decoded[3].fields.throttle).toBeCloseTo(0.55);
  });

  it('decodes command and mission acknowledgements', () => {
    const commandAckPayload = Buffer.alloc(3);
    commandAckPayload.writeUInt16LE(400, 0);
    commandAckPayload.writeUInt8(0, 2);
    const missionRequestPayload = Buffer.alloc(4);
    missionRequestPayload.writeUInt16LE(3, 0);
    missionRequestPayload.writeUInt8(255, 2);
    missionRequestPayload.writeUInt8(190, 3);
    const missionAckPayload = Buffer.from([255, 190, 0]);
    const parser = new MavlinkV1Parser();
    const [commandAck, missionRequest, missionAck] = parser.feed(Buffer.concat([
      mavlinkV1Frame(77, commandAckPayload),
      mavlinkV1Frame(51, missionRequestPayload),
      mavlinkV1Frame(47, missionAckPayload)
    ]));
    expect(decodeCommandAck(commandAck)).toEqual({ command: 400, result: 0, label: 'accepted' });
    expect(decodeMissionRequestInt(missionRequest)).toEqual({ seq: 3, targetSystem: 255, targetComponent: 190 });
    expect(decodeMissionAck(missionAck)).toEqual({ targetSystem: 255, targetComponent: 190, type: 0 });
  });

  it('keeps vehicles separate and computes deterministic message rates', () => {
    const parser = new MavlinkV1Parser();
    const registry = new VehicleRegistry();
    for (const message of parser.feed(localPosition(1, 1, 1))) registry.apply(message, 10);
    for (const message of parser.feed(localPosition(2, 2, 1))) registry.apply(message, 10.5);
    for (const message of parser.feed(localPosition(3, 1, 1))) registry.apply(message, 11);
    const snapshot = registry.snapshot(12);
    expect(snapshot.vehicles.map((vehicle) => vehicle.id).sort()).toEqual(['1:1', '2:1']);
    const stats = snapshot.messages.find((message) => message.key === '1:1:32');
    expect(stats?.count).toBe(2);
    expect(stats?.rateHz).toBeCloseTo(1);
  });

  it('generates marker events once per state transition', () => {
    const parser = new MavlinkV1Parser();
    const registry = new VehicleRegistry();
    const armed = Buffer.from([0, 0, 0, 0, 1, 0, 0x80, 4, 3]);
    const disarmed = Buffer.from([0, 0, 0, 0, 1, 0, 0, 4, 3]);
    for (const message of parser.feed(mavlinkV1Frame(0, disarmed, 1))) registry.apply(message, 1);
    for (const message of parser.feed(mavlinkV1Frame(0, armed, 2))) registry.apply(message, 2);
    for (const message of parser.feed(mavlinkV1Frame(0, armed, 3))) registry.apply(message, 3);
    registry.addMarker('Check', 4);
    const events = registry.snapshot(4).events;
    expect(events.filter((event) => event.kind === 'arming')).toHaveLength(1);
    expect(events.filter((event) => event.kind === 'marker')).toHaveLength(1);
  });

  it('labels MAVLink heartbeat vehicle types', () => {
    expect(mavTypeLabel(1)).toBe('Fixed-wing');
    expect(mavTypeLabel(2)).toBe('Quadrotor');
    expect(mavTypeLabel(19)).toBe('VTOL');
    expect(mavTypeLabel(99)).toBe('MAV_TYPE 99');
  });

  it('forwards raw packets only when QGC forwarding is enabled', async () => {
    const sink = dgram.createSocket('udp4');
    sockets.push(sink);
    await new Promise<void>((resolve) => sink.bind(0, '127.0.0.1', resolve));
    const address = sink.address();
    if (typeof address === 'string') {
      throw new Error('unexpected pipe address');
    }
    const service = new MavlinkTelemetryService({
      listenHost: '127.0.0.1',
      listenPort: 0,
      qgcForwarding: true,
      qgcEndpoints: [{ host: '127.0.0.1', port: address.port }]
    });
    service.handlePacket(attitude());
    const forwarded = await new Promise<Buffer>((resolve) => sink.once('message', resolve));
    expect(forwarded).toEqual(attitude());
    service.setQgcForwarding(false);
    expect(service.getConfig().qgcForwarding).toBe(false);
    await service.stop();
  });

  it('rejects writes unless the live link is explicitly writable', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: false });
    service.handlePacket(heartbeat());
    expect(service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true })).toMatchObject({
      accepted: false,
      reason: 'Animus writes require a SITL session started with --writable-animus.'
    });
    expect(service.uploadMissionToSitl({
      schemaVersion: 1,
      source: 'bayek-v1',
      waypoints: [{ seq: 0, lat_deg: 37, lon_deg: -122, alt_m: 120, throttle: 0.5, acceptance_radius_m: 25 }]
    }, '1:1')).toMatchObject({ accepted: false, state: 'rejected' });
  });

  it('creates a sent command transaction for writable SITL commands', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    let snapshot = service.handlePacket(readyVehicle());
    expect(snapshot.connected).toBe(true);
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('session-snapshot', (next) => {
      session = next;
    });
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });
    expect(result).toMatchObject({ accepted: true, state: 'sent', transactionId: expect.any(String), ack: null });
    expect(session?.commandTransactions?.[0]).toMatchObject({
      id: result.transactionId,
      vehicleId: '1:1',
      commandName: 'arm',
      commandId: 400,
      params: [1],
      state: 'sent'
    });
  });

  it('emits audit event for successful command dispatch with transaction id', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    const audit: CommandAuditEntry[] = [];
    service.on('command-audit', (entry) => audit.push(entry as CommandAuditEntry));

    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });

    expect(audit[0]).toMatchObject({
      eventKind: 'dispatch-sent',
      transactionId: result.transactionId,
      commandName: 'arm',
      commandId: 400,
      params: [1],
      accepted: true,
      state: 'sent',
      confirmationType: 'browser-confirm',
      writable: true
    });
  });

  it('emits audit event for blocked dispatch', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: false });
    service.handlePacket(heartbeat());
    const audit: CommandAuditEntry[] = [];
    service.on('command-audit', (entry) => audit.push(entry as CommandAuditEntry));

    service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });

    expect(audit[0]).toMatchObject({
      eventKind: 'dispatch-blocked',
      transactionId: null,
      commandName: 'arm',
      accepted: false,
      state: 'blocked',
      writable: false
    });
  });

  it('marks accepted COMMAND_ACK as acknowledged on the matching transaction', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('session-snapshot', (next) => {
      session = next;
    });
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });
    service.handlePacket(commandAck(400, 0));
    expect(session?.commandTransactions?.[0]).toMatchObject({
      id: result.transactionId,
      state: 'acknowledged',
      ack: { command: 400, result: 0, label: 'accepted' },
      failureReason: null
    });
  });

  it('emits audit event for accepted COMMAND_ACK', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    const audit: CommandAuditEntry[] = [];
    service.on('command-audit', (entry) => audit.push(entry as CommandAuditEntry));
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true, confirmationType: 'typed-vehicle-id' });

    service.handlePacket(commandAck(400, 0));

    expect(audit.at(-1)).toMatchObject({
      eventKind: 'ack',
      transactionId: result.transactionId,
      accepted: true,
      state: 'acknowledged',
      ack: { command: 400, result: 0, label: 'accepted' },
      confirmationType: 'typed-vehicle-id'
    });
  });

  it('maps rejected COMMAND_ACK results to failed transactions with labels', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('session-snapshot', (next) => {
      session = next;
    });
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });
    service.handlePacket(commandAck(400, 2));
    expect(session?.commandTransactions?.[0]).toMatchObject({
      id: result.transactionId,
      state: 'failed',
      ack: { command: 400, result: 2, label: 'denied' },
      failureReason: 'denied'
    });
  });

  it('emits audit event for rejected COMMAND_ACK', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    const audit: CommandAuditEntry[] = [];
    service.on('command-audit', (entry) => audit.push(entry as CommandAuditEntry));
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });

    service.handlePacket(commandAck(400, 2));

    expect(audit.at(-1)).toMatchObject({
      eventKind: 'ack',
      transactionId: result.transactionId,
      accepted: false,
      state: 'failed',
      reason: 'denied',
      ack: { command: 400, result: 2, label: 'denied' }
    });
  });

  it('times out stale sent command transactions', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('session-snapshot', (next) => {
      session = next;
    });
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });
    const sentAtS = session?.commandTransactions?.[0]?.sentAtS;
    expect(sentAtS).toEqual(expect.any(Number));
    service.expireCommandTransactions((sentAtS ?? 0) + 2.1);
    expect(session?.commandTransactions?.[0]).toMatchObject({
      id: result.transactionId,
      state: 'timeout',
      ack: null,
      failureReason: 'no COMMAND_ACK received within 2s'
    });
  });

  it('emits audit event for command timeout', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(readyVehicle());
    const audit: CommandAuditEntry[] = [];
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('command-audit', (entry) => audit.push(entry as CommandAuditEntry));
    service.on('session-snapshot', (next) => {
      session = next;
    });
    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });
    const transactionSentAtS = session?.commandTransactions?.[0]?.sentAtS;
    expect(transactionSentAtS).toEqual(expect.any(Number));

    service.expireCommandTransactions((transactionSentAtS ?? 0) + 2.1);

    expect(audit.at(-1)).toMatchObject({
      eventKind: 'timeout',
      transactionId: result.transactionId,
      accepted: false,
      state: 'timeout',
      reason: 'no COMMAND_ACK received within 2s'
    });
  });

  it('records send failure as failed audit state if sendFrame throws after transaction creation', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    const writable = service as unknown as {
      socket: { send: () => void };
      lastRemote: { host: string; port: number };
    };
    writable.socket = { send: () => { throw new Error('socket send failed'); } };
    writable.lastRemote = { host: '127.0.0.1', port: 14540 };
    service.handlePacket(readyVehicle());
    const audit: CommandAuditEntry[] = [];
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('command-audit', (entry) => audit.push(entry as CommandAuditEntry));
    service.on('session-snapshot', (next) => {
      session = next;
    });

    const result = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });

    expect(result).toMatchObject({ accepted: false, state: 'failed', reason: 'socket send failed', transactionId: expect.any(String) });
    expect(session?.commandTransactions?.[0]).toMatchObject({ id: result.transactionId, state: 'failed', failureReason: 'socket send failed' });
    expect(audit.at(-1)).toMatchObject({
      eventKind: 'send-failed',
      transactionId: result.transactionId,
      accepted: false,
      state: 'failed',
      reason: 'socket send failed'
    });
  });

  it('matches COMMAND_ACK to the correct vehicle transaction', () => {
    const service = new MavlinkTelemetryService({ writableAnimus: true });
    makeWritable(service);
    service.handlePacket(Buffer.concat([readyVehicle(1, 1, 1), localPosition(5, 1, 1)]));
    service.handlePacket(Buffer.concat([readyVehicle(2, 1, 6), localPosition(10, 2, 1)]));
    let session = null as ReturnType<VehicleRegistry['snapshot']> | null;
    service.on('session-snapshot', (next) => {
      session = next;
    });
    const first = service.dispatchCommand({ command: 'arm', vehicleId: '1:1', confirmed: true });
    const second = service.dispatchCommand({ command: 'arm', vehicleId: '2:1', confirmed: true });
    service.handlePacket(commandAck(400, 0, 21, 2, 1));
    const transactions = session?.commandTransactions ?? [];
    expect(transactions.find((transaction) => transaction.id === second.transactionId)).toMatchObject({ vehicleId: '2:1', state: 'acknowledged' });
    expect(transactions.find((transaction) => transaction.id === first.transactionId)).toMatchObject({ vehicleId: '1:1', state: 'sent' });
  });
});
