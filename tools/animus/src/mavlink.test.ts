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

const sockets: dgram.Socket[] = [];

afterEach(() => {
  while (sockets.length > 0) {
    sockets.pop()?.close();
  }
});

function heartbeat(seq = 1): Buffer {
  return mavlinkV1Frame(0, Buffer.from([0, 0, 0, 0, 1, 0, 0, 4, 3]), seq);
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

function sysStatus(seq = 6): Buffer {
  const payload = Buffer.alloc(31);
  payload.writeUInt32LE(7, 0);
  payload.writeUInt32LE(7, 4);
  payload.writeUInt32LE(5, 8);
  payload.writeUInt16LE(320, 12);
  payload.writeUInt16LE(12100, 14);
  payload.writeInt16LE(340, 16);
  payload.writeInt8(72, 30);
  return mavlinkV1Frame(1, payload, seq);
}

function gpsRaw(seq = 7): Buffer {
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
  return mavlinkV1Frame(24, payload, seq);
}

function missionCurrent(seq = 8): Buffer {
  const payload = Buffer.alloc(2);
  payload.writeUInt16LE(4, 0);
  return mavlinkV1Frame(42, payload, seq);
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
});
