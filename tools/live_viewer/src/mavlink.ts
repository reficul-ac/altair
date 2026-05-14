import dgram, { type RemoteInfo, type Socket } from 'node:dgram';
import { EventEmitter } from 'node:events';

export const MAVLINK_V1_STX = 0xfe;
export const DEFAULT_LISTEN_HOST = '127.0.0.1';
export const DEFAULT_LISTEN_PORT = 14551;
export const DEFAULT_QGC_HOST = '127.0.0.1';
export const DEFAULT_QGC_PORT = 14550;

export const MAVLINK_CRC_EXTRA: Record<number, number> = {
  0: 50,
  30: 39,
  33: 104,
  74: 20
};

export type Endpoint = {
  host: string;
  port: number;
};

export type MavlinkMessage = {
  msgId: number;
  payload: Buffer;
  seq: number;
  systemId: number;
  componentId: number;
};

export type MavlinkServiceConfig = {
  listenHost: string;
  listenPort: number;
  qgcForwarding: boolean;
  qgcEndpoints: Endpoint[];
};

export type VehicleStatePayload = {
  type: 'vehicle_state';
  connected: boolean;
  packetAgeS: number | null;
  heartbeatAgeS: number | null;
  systemId: number | null;
  componentId: number | null;
  vehicleType: string | null;
  attitude: {
    rollRad: number;
    pitchRad: number;
    yawRad: number;
    rollRateRps: number;
    pitchRateRps: number;
    yawRateRps: number;
  };
  globalPosition: {
    latDeg: number | null;
    lonDeg: number | null;
    altitudeM: number | null;
    relativeAltitudeM: number | null;
    originLatDeg: number | null;
    originLonDeg: number | null;
    originAltitudeM: number | null;
  };
  localPosition: {
    northM: number | null;
    eastM: number | null;
    upM: number | null;
  };
  velocity: {
    northMps: number;
    eastMps: number;
    downMps: number;
  };
  metrics: {
    headingDeg: number | null;
    airspeedMps: number | null;
    groundspeedMps: number | null;
    climbMps: number | null;
    throttlePct: number | null;
  };
};

export function x25Crc(data: Uint8Array, crc = 0xffff): number {
  let next = crc;
  for (const byte of data) {
    let tmp = byte ^ (next & 0xff);
    tmp ^= (tmp << 4) & 0xff;
    next = ((next >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xffff;
  }
  return next;
}

export function mavlinkV1Frame(msgId: number, payload: Buffer, seq = 1, systemId = 1, componentId = 1): Buffer {
  const header = Buffer.from([MAVLINK_V1_STX, payload.length, seq, systemId, componentId, msgId]);
  const extra = MAVLINK_CRC_EXTRA[msgId];
  if (extra === undefined) {
    throw new Error(`unsupported MAVLink message id ${msgId}`);
  }
  const crc = x25Crc(Buffer.from([extra]), x25Crc(Buffer.concat([header.subarray(1), payload])));
  return Buffer.concat([header, payload, Buffer.from([crc & 0xff, crc >> 8])]);
}

export class MavlinkV1Parser {
  private readonly buffer: number[] = [];

  feed(data: Uint8Array): MavlinkMessage[] {
    this.buffer.push(...data);
    const messages: MavlinkMessage[] = [];
    for (;;) {
      const start = this.buffer.indexOf(MAVLINK_V1_STX);
      if (start < 0) {
        this.buffer.length = 0;
        break;
      }
      if (start > 0) {
        this.buffer.splice(0, start);
      }
      if (this.buffer.length < 8) {
        break;
      }
      const payloadLen = this.buffer[1];
      const frameLen = 6 + payloadLen + 2;
      if (this.buffer.length < frameLen) {
        break;
      }
      const frame = Buffer.from(this.buffer.splice(0, frameLen));
      const msgId = frame[5];
      const extra = MAVLINK_CRC_EXTRA[msgId];
      if (extra === undefined) {
        continue;
      }
      const expected = x25Crc(Buffer.from([extra]), x25Crc(frame.subarray(1, 6 + payloadLen)));
      const actual = frame[6 + payloadLen] | (frame[7 + payloadLen] << 8);
      if (expected !== actual) {
        continue;
      }
      messages.push({
        msgId,
        payload: frame.subarray(6, 6 + payloadLen),
        seq: frame[2],
        systemId: frame[3],
        componentId: frame[4]
      });
    }
    return messages;
  }
}

function nullishNan(value: number): number | null {
  return Number.isFinite(value) ? value : null;
}

function readInt16(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 2 ? payload.readInt16LE(offset) : null;
}

function readUInt16(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 2 ? payload.readUInt16LE(offset) : null;
}

function readInt32(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 4 ? payload.readInt32LE(offset) : null;
}

function readFloat(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 4 ? payload.readFloatLE(offset) : null;
}

export class LiveVehicleState {
  connected = false;
  lastPacketTimeS: number | null = null;
  lastHeartbeatTimeS: number | null = null;
  systemId: number | null = null;
  componentId: number | null = null;
  rollRad = 0;
  pitchRad = 0;
  yawRad = 0;
  rollRateRps = 0;
  pitchRateRps = 0;
  yawRateRps = 0;
  latDeg: number | null = null;
  lonDeg: number | null = null;
  altitudeM: number | null = null;
  relativeAltitudeM: number | null = null;
  velNMps = 0;
  velEMps = 0;
  velDMps = 0;
  headingDeg: number | null = null;
  airspeedMps: number | null = null;
  groundspeedMps: number | null = null;
  climbMps: number | null = null;
  throttlePct: number | null = null;
  originLatDeg: number | null = null;
  originLonDeg: number | null = null;
  originAltitudeM: number | null = null;
  vehicleType: string | null = null;

  apply(message: MavlinkMessage, nowS = performanceNowS()): void {
    this.connected = true;
    this.lastPacketTimeS = nowS;
    this.systemId = message.systemId;
    this.componentId = message.componentId;
    if (message.msgId === 0) {
      this.lastHeartbeatTimeS = nowS;
      this.applyHeartbeat(message.payload);
      return;
    }
    if (message.msgId === 30) {
      this.applyAttitude(message.payload);
      return;
    }
    if (message.msgId === 33) {
      this.applyGlobalPosition(message.payload);
      return;
    }
    if (message.msgId === 74) {
      this.applyVfrHud(message.payload);
    }
  }

  toJsonable(nowS = performanceNowS()): VehicleStatePayload {
    const packetAgeS = this.lastPacketTimeS === null ? null : nowS - this.lastPacketTimeS;
    const heartbeatAgeS = this.lastHeartbeatTimeS === null ? null : nowS - this.lastHeartbeatTimeS;
    const [northM, eastM, upM] = this.localPosition();
    return {
      type: 'vehicle_state',
      connected: this.connected && (packetAgeS === null || packetAgeS < 2),
      packetAgeS,
      heartbeatAgeS,
      systemId: this.systemId,
      componentId: this.componentId,
      vehicleType: this.vehicleType,
      attitude: {
        rollRad: this.rollRad,
        pitchRad: this.pitchRad,
        yawRad: this.yawRad,
        rollRateRps: this.rollRateRps,
        pitchRateRps: this.pitchRateRps,
        yawRateRps: this.yawRateRps
      },
      globalPosition: {
        latDeg: this.latDeg,
        lonDeg: this.lonDeg,
        altitudeM: this.altitudeM,
        relativeAltitudeM: this.relativeAltitudeM,
        originLatDeg: this.originLatDeg,
        originLonDeg: this.originLonDeg,
        originAltitudeM: this.originAltitudeM
      },
      localPosition: { northM, eastM, upM },
      velocity: {
        northMps: this.velNMps,
        eastMps: this.velEMps,
        downMps: this.velDMps
      },
      metrics: {
        headingDeg: this.headingDeg,
        airspeedMps: this.airspeedMps,
        groundspeedMps: this.groundspeedMps,
        climbMps: this.climbMps,
        throttlePct: this.throttlePct
      }
    };
  }

  private applyHeartbeat(payload: Buffer): void {
    if (payload.length < 9) {
      return;
    }
    this.vehicleType = mavTypeLabel(payload.readUInt8(4));
  }

  localPosition(): [number | null, number | null, number | null] {
    if (
      this.latDeg === null ||
      this.lonDeg === null ||
      this.altitudeM === null ||
      this.originLatDeg === null ||
      this.originLonDeg === null ||
      this.originAltitudeM === null
    ) {
      return [null, null, null];
    }
    const earthRadiusM = 6378137;
    const lat0Rad = (this.originLatDeg * Math.PI) / 180;
    const northM = (((this.latDeg - this.originLatDeg) * Math.PI) / 180) * earthRadiusM;
    const eastM = (((this.lonDeg - this.originLonDeg) * Math.PI) / 180) * earthRadiusM * Math.cos(lat0Rad);
    const upM = this.altitudeM - this.originAltitudeM;
    return [northM, eastM, upM];
  }

  private applyAttitude(payload: Buffer): void {
    if (payload.length < 28) {
      return;
    }
    this.rollRad = payload.readFloatLE(4);
    this.pitchRad = payload.readFloatLE(8);
    this.yawRad = payload.readFloatLE(12);
    this.rollRateRps = payload.readFloatLE(16);
    this.pitchRateRps = payload.readFloatLE(20);
    this.yawRateRps = payload.readFloatLE(24);
  }

  private applyGlobalPosition(payload: Buffer): void {
    if (payload.length < 28) {
      return;
    }
    const lat = readInt32(payload, 4);
    const lon = readInt32(payload, 8);
    const alt = readInt32(payload, 12);
    const relAlt = readInt32(payload, 16);
    const vx = readInt16(payload, 20);
    const vy = readInt16(payload, 22);
    const vz = readInt16(payload, 24);
    const hdg = readUInt16(payload, 26);
    if (lat === null || lon === null || alt === null || relAlt === null || vx === null || vy === null || vz === null || hdg === null) {
      return;
    }
    this.latDeg = lat / 10000000;
    this.lonDeg = lon / 10000000;
    this.altitudeM = alt / 1000;
    this.relativeAltitudeM = relAlt / 1000;
    this.velNMps = vx / 100;
    this.velEMps = vy / 100;
    this.velDMps = vz / 100;
    this.headingDeg = hdg === 65535 ? null : hdg / 100;
    if (this.originLatDeg === null) {
      this.originLatDeg = this.latDeg;
      this.originLonDeg = this.lonDeg;
      this.originAltitudeM = this.altitudeM;
    }
  }

  private applyVfrHud(payload: Buffer): void {
    if (payload.length < 20) {
      return;
    }
    this.airspeedMps = nullishNan(readFloat(payload, 0) ?? Number.NaN);
    this.groundspeedMps = nullishNan(readFloat(payload, 4) ?? Number.NaN);
    this.headingDeg = readInt16(payload, 8);
    this.throttlePct = readUInt16(payload, 10);
    this.altitudeM = nullishNan(readFloat(payload, 12) ?? Number.NaN);
    this.climbMps = nullishNan(readFloat(payload, 16) ?? Number.NaN);
  }
}

export function mavTypeLabel(mavType: number): string {
  switch (mavType) {
    case 1:
      return 'Fixed-wing';
    case 2:
      return 'Quadrotor';
    case 3:
      return 'Coaxial';
    case 4:
      return 'Helicopter';
    case 10:
      return 'Ground rover';
    case 11:
      return 'Surface boat';
    case 12:
      return 'Submarine';
    case 13:
      return 'Hexarotor';
    case 14:
      return 'Octorotor';
    case 19:
      return 'VTOL';
    case 20:
      return 'Airship';
    case 21:
      return 'Free balloon';
    case 22:
      return 'Rocket';
    default:
      return `MAV_TYPE ${mavType}`;
  }
}

export class UdpForwarder {
  private readonly socket: Socket;

  constructor(private endpoints: Endpoint[] = []) {
    this.socket = dgram.createSocket('udp4');
  }

  configure(endpoints: Endpoint[]): void {
    this.endpoints = endpoints;
  }

  forward(packet: Buffer): void {
    for (const endpoint of this.endpoints) {
      this.socket.send(packet, endpoint.port, endpoint.host);
    }
  }

  close(): void {
    this.socket.close();
  }
}

export class MavlinkTelemetryService extends EventEmitter {
  readonly parser = new MavlinkV1Parser();
  readonly state = new LiveVehicleState();
  private socket: Socket | null = null;
  private forwarder = new UdpForwarder();
  private config: MavlinkServiceConfig;

  constructor(config: Partial<MavlinkServiceConfig> = {}) {
    super();
    this.config = normalizeConfig(config);
    this.forwarder.configure(this.activeForwardTargets());
  }

  getConfig(): MavlinkServiceConfig {
    return {
      ...this.config,
      qgcEndpoints: this.config.qgcEndpoints.map((endpoint) => ({ ...endpoint }))
    };
  }

  async start(): Promise<void> {
    if (this.socket) {
      return;
    }
    this.socket = dgram.createSocket('udp4');
    this.socket.on('message', (packet: Buffer, remote: RemoteInfo) => {
      this.handlePacket(packet, remote);
    });
    await new Promise<void>((resolve, reject) => {
      const socket = this.socket;
      if (!socket) {
        reject(new Error('socket was not created'));
        return;
      }
      const onError = (error: Error): void => {
        socket.off('listening', onListening);
        reject(error);
      };
      const onListening = (): void => {
        socket.off('error', onError);
        const address = socket.address();
        if (typeof address !== 'string') {
          this.config.listenPort = address.port;
        }
        resolve();
      };
      socket.once('error', onError);
      socket.once('listening', onListening);
      socket.bind(this.config.listenPort, this.config.listenHost);
    });
    this.emit('config', this.getConfig());
  }

  async stop(): Promise<void> {
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      await new Promise<void>((resolve) => socket.close(() => resolve()));
    }
    this.forwarder.close();
    this.forwarder = new UdpForwarder(this.activeForwardTargets());
  }

  async setListenPort(port: number): Promise<void> {
    if (!Number.isInteger(port) || port <= 0 || port > 65535) {
      throw new Error(`invalid MAVLink listen port ${port}`);
    }
    if (port === this.config.listenPort) {
      return;
    }
    const wasRunning = this.socket !== null;
    if (wasRunning) {
      await this.stop();
    }
    this.config.listenPort = port;
    if (wasRunning) {
      await this.start();
    }
    this.emit('config', this.getConfig());
  }

  setQgcForwarding(enabled: boolean): void {
    this.config.qgcForwarding = enabled;
    this.forwarder.configure(this.activeForwardTargets());
    this.emit('config', this.getConfig());
  }

  handlePacket(packet: Buffer, _remote?: RemoteInfo): VehicleStatePayload {
    this.forwarder.forward(packet);
    const nowS = performanceNowS();
    for (const message of this.parser.feed(packet)) {
      this.state.apply(message, nowS);
    }
    const payload = this.state.toJsonable(nowS);
    this.emit('vehicle-state', payload);
    return payload;
  }

  private activeForwardTargets(): Endpoint[] {
    return this.config.qgcForwarding ? this.config.qgcEndpoints : [];
  }
}

export function parseEndpoint(text: string): Endpoint {
  const [host, portText] = text.split(':');
  const port = Number(portText);
  if (!host || !Number.isInteger(port) || port <= 0 || port > 65535) {
    throw new Error(`expected host:port endpoint, got ${text}`);
  }
  return { host, port };
}

export function normalizeConfig(config: Partial<MavlinkServiceConfig>): MavlinkServiceConfig {
  return {
    listenHost: config.listenHost ?? DEFAULT_LISTEN_HOST,
    listenPort: config.listenPort ?? DEFAULT_LISTEN_PORT,
    qgcForwarding: config.qgcForwarding ?? true,
    qgcEndpoints: config.qgcEndpoints ?? [{ host: DEFAULT_QGC_HOST, port: DEFAULT_QGC_PORT }]
  };
}

function performanceNowS(): number {
  return Number(process.hrtime.bigint()) / 1000000000;
}
