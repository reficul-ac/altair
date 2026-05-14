import dgram from 'node:dgram';
import { afterEach, describe, expect, it } from 'vitest';
import { LiveVehicleState, MavlinkTelemetryService, MavlinkV1Parser, mavlinkV1Frame, mavTypeLabel } from './mavlink';

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
});
