import dgram, { type RemoteInfo, type Socket } from 'node:dgram';
import { EventEmitter } from 'node:events';
import { buildMultiVehicleAnalysis, defaultCommandCapabilities } from './parity.js';
import type { CameraStream, CommandCapabilityState, LinkDiagnostics, MockLinkState, MultiVehicleAnalysis } from './state.js';

export const MAVLINK_V1_STX = 0xfe;
export const DEFAULT_LISTEN_HOST = '127.0.0.1';
export const DEFAULT_LISTEN_PORT = 14551;
export const DEFAULT_QGC_HOST = '127.0.0.1';
export const DEFAULT_QGC_PORT = 14550;

export const MAVLINK_CRC_EXTRA: Record<number, number> = {
  0: 50,
  1: 124,
  24: 24,
  30: 39,
  32: 185,
  33: 104,
  36: 222,
  42: 28,
  65: 118,
  74: 20,
  115: 4,
  147: 154,
  253: 83
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

export type DecodedMavlinkMessage = MavlinkMessage & {
  name: string;
  fields: Record<string, number | string | null>;
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
  status?: {
    armed: boolean | null;
    mode: string | null;
    baseMode: number | null;
    customMode: number | null;
    gpsFix: string | null;
    satellitesVisible: number | null;
    batteryRemainingPct: number | null;
    batteryVoltageV: number | null;
    onboardControlSensorsHealth: number | null;
    missionSeq: number | null;
    lastStatusText: string | null;
  };
  commandCapabilities?: CommandCapabilityState;
  diagnostics?: LinkDiagnostics;
  cameraStreams?: CameraStream[];
  trail?: {
    eastM: number;
    northM: number;
    upM: number;
    timestampS: number;
  }[];
};

export type InspectorMessageStats = {
  key: string;
  msgId: number;
  name: string;
  systemId: number;
  componentId: number;
  lastAgeS: number;
  rateHz: number;
  count: number;
  fields: Record<string, number | string | null>;
};

export type SessionEvent = {
  id: string;
  timestampS: number;
  vehicleId: string | null;
  level: 'info' | 'warning' | 'error';
  kind: string;
  label: string;
  position: { eastM: number; northM: number; upM: number } | null;
};

export type SessionSnapshotPayload = {
  type: 'session_snapshot';
  vehicles: VehicleStatePayload[];
  selectedVehicleId: string | null;
  messages: InspectorMessageStats[];
  events: SessionEvent[];
  packetCount: number;
  decodedCount: number;
  analysis?: MultiVehicleAnalysis;
  mockLinks?: MockLinkState[];
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

function readUInt8(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 1 ? payload.readUInt8(offset) : null;
}

function readInt8(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 1 ? payload.readInt8(offset) : null;
}

function readUInt32(payload: Buffer, offset: number): number | null {
  return payload.length >= offset + 4 ? payload.readUInt32LE(offset) : null;
}

function readInt32Strict(payload: Buffer, offset: number): number {
  return payload.readInt32LE(offset);
}

function readCString(payload: Buffer, offset: number, length: number): string | null {
  if (payload.length < offset + length) {
    return null;
  }
  const end = payload.indexOf(0, offset);
  return payload.subarray(offset, end >= offset && end < offset + length ? end : offset + length).toString('utf8').trim();
}

export function vehicleId(systemId: number, componentId: number): string {
  return `${systemId}:${componentId}`;
}

type Decoder = (message: MavlinkMessage) => DecodedMavlinkMessage | null;

function decodeWithFields(message: MavlinkMessage, name: string, fields: Record<string, number | string | null>): DecodedMavlinkMessage {
  return { ...message, name, fields };
}

const DECODERS: Record<number, Decoder> = {
  0: (message) => {
    const p = message.payload;
    if (p.length < 9) return null;
    const baseMode = p.readUInt8(6);
    const customMode = p.readUInt32LE(0);
    return decodeWithFields(message, 'HEARTBEAT', {
      customMode,
      type: p.readUInt8(4),
      autopilot: p.readUInt8(5),
      baseMode,
      systemStatus: p.readUInt8(7),
      mavlinkVersion: p.readUInt8(8),
      armed: (baseMode & 0x80) !== 0 ? 1 : 0,
      mode: flightModeLabel(customMode, baseMode)
    });
  },
  1: (message) => {
    const p = message.payload;
    if (p.length < 31) return null;
    const voltageMv = readUInt16(p, 14);
    return decodeWithFields(message, 'SYS_STATUS', {
      onboardControlSensorsPresent: readUInt32(p, 0),
      onboardControlSensorsEnabled: readUInt32(p, 4),
      onboardControlSensorsHealth: readUInt32(p, 8),
      loadPct: (readUInt16(p, 12) ?? 0) / 10,
      voltageBatteryV: voltageMv === 65535 || voltageMv === null ? null : voltageMv / 1000,
      currentBatteryA: nullableScaled(readInt16(p, 16), -1, 100),
      batteryRemainingPct: readInt8(p, 30) === -1 ? null : readInt8(p, 30)
    });
  },
  24: (message) => {
    const p = message.payload;
    if (p.length < 30) return null;
    return decodeWithFields(message, 'GPS_RAW_INT', {
      timeUsec: Number(p.readBigUInt64LE(0)),
      fixType: readUInt8(p, 28),
      fix: gpsFixLabel(readUInt8(p, 28)),
      latDeg: readInt32Strict(p, 8) / 1e7,
      lonDeg: readInt32Strict(p, 12) / 1e7,
      altM: readInt32Strict(p, 16) / 1000,
      ephM: nullableScaled(readUInt16(p, 20), 65535, 100),
      epvM: nullableScaled(readUInt16(p, 22), 65535, 100),
      velMps: nullableScaled(readUInt16(p, 24), 65535, 100),
      cogDeg: nullableScaled(readUInt16(p, 26), 65535, 100),
      satellitesVisible: readUInt8(p, 29) === 255 ? null : readUInt8(p, 29)
    });
  },
  30: (message) => {
    const p = message.payload;
    if (p.length < 28) return null;
    return decodeWithFields(message, 'ATTITUDE', {
      timeBootMs: readUInt32(p, 0),
      rollRad: readFloat(p, 4),
      pitchRad: readFloat(p, 8),
      yawRad: readFloat(p, 12),
      rollspeedRps: readFloat(p, 16),
      pitchspeedRps: readFloat(p, 20),
      yawspeedRps: readFloat(p, 24)
    });
  },
  32: (message) => {
    const p = message.payload;
    if (p.length < 28) return null;
    return decodeWithFields(message, 'LOCAL_POSITION_NED', {
      timeBootMs: readUInt32(p, 0),
      xNorthM: readFloat(p, 4),
      yEastM: readFloat(p, 8),
      zDownM: readFloat(p, 12),
      vxNorthMps: readFloat(p, 16),
      vyEastMps: readFloat(p, 20),
      vzDownMps: readFloat(p, 24)
    });
  },
  33: (message) => {
    const p = message.payload;
    if (p.length < 28) return null;
    return decodeWithFields(message, 'GLOBAL_POSITION_INT', {
      timeBootMs: readUInt32(p, 0),
      latDeg: readInt32Strict(p, 4) / 1e7,
      lonDeg: readInt32Strict(p, 8) / 1e7,
      altM: readInt32Strict(p, 12) / 1000,
      relativeAltM: readInt32Strict(p, 16) / 1000,
      vxNorthMps: (readInt16(p, 20) ?? 0) / 100,
      vyEastMps: (readInt16(p, 22) ?? 0) / 100,
      vzDownMps: (readInt16(p, 24) ?? 0) / 100,
      headingDeg: nullableScaled(readUInt16(p, 26), 65535, 100)
    });
  },
  36: (message) => {
    const p = message.payload;
    if (p.length < 21) return null;
    return decodeWithFields(message, 'SERVO_OUTPUT_RAW', {
      timeUsec: readUInt32(p, 0),
      port: readUInt8(p, 4),
      servo1Raw: readUInt16(p, 5),
      servo2Raw: readUInt16(p, 7),
      servo3Raw: readUInt16(p, 9),
      servo4Raw: readUInt16(p, 11),
      servo5Raw: readUInt16(p, 13),
      servo6Raw: readUInt16(p, 15),
      servo7Raw: readUInt16(p, 17),
      servo8Raw: readUInt16(p, 19)
    });
  },
  42: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'MISSION_CURRENT', { seq: readUInt16(p, 0) });
  },
  65: (message) => {
    const p = message.payload;
    if (p.length < 42) return null;
    return decodeWithFields(message, 'RC_CHANNELS', {
      timeBootMs: readUInt32(p, 0),
      chancount: readUInt8(p, 4),
      chan1Raw: readUInt16(p, 5),
      chan2Raw: readUInt16(p, 7),
      chan3Raw: readUInt16(p, 9),
      chan4Raw: readUInt16(p, 11),
      chan5Raw: readUInt16(p, 13),
      chan6Raw: readUInt16(p, 15),
      chan7Raw: readUInt16(p, 17),
      chan8Raw: readUInt16(p, 19),
      rssi: readUInt8(p, 41)
    });
  },
  74: (message) => {
    const p = message.payload;
    if (p.length < 20) return null;
    return decodeWithFields(message, 'VFR_HUD', {
      airspeedMps: readFloat(p, 0),
      groundspeedMps: readFloat(p, 4),
      headingDeg: readInt16(p, 8),
      throttlePct: readUInt16(p, 10),
      altM: readFloat(p, 12),
      climbMps: readFloat(p, 16)
    });
  },
  115: (message) => {
    const p = message.payload;
    if (p.length < 64) return null;
    return decodeWithFields(message, 'HIL_STATE_QUATERNION', {
      timeUsec: Number(p.readBigUInt64LE(0)),
      attitudeQ0: readFloat(p, 8),
      attitudeQ1: readFloat(p, 12),
      attitudeQ2: readFloat(p, 16),
      attitudeQ3: readFloat(p, 20),
      rollspeedRps: readFloat(p, 24),
      pitchspeedRps: readFloat(p, 28),
      yawspeedRps: readFloat(p, 32),
      latDeg: readInt32Strict(p, 36) / 1e7,
      lonDeg: readInt32Strict(p, 40) / 1e7,
      altM: readInt32Strict(p, 44) / 1000,
      vxMps: (readInt16(p, 48) ?? 0) / 100,
      vyMps: (readInt16(p, 50) ?? 0) / 100,
      vzMps: (readInt16(p, 52) ?? 0) / 100,
      indAirspeedMps: (readUInt16(p, 54) ?? 0) / 100,
      trueAirspeedMps: (readUInt16(p, 56) ?? 0) / 100,
      xaccMps2: (readInt16(p, 58) ?? 0) / 1000,
      yaccMps2: (readInt16(p, 60) ?? 0) / 1000,
      zaccMps2: (readInt16(p, 62) ?? 0) / 1000
    });
  },
  147: (message) => {
    const p = message.payload;
    if (p.length < 36) return null;
    return decodeWithFields(message, 'BATTERY_STATUS', {
      id: readUInt8(p, 0),
      batteryFunction: readUInt8(p, 1),
      type: readUInt8(p, 2),
      temperatureC: nullableScaled(readInt16(p, 3), 32767, 100),
      currentBatteryA: nullableScaled(readInt16(p, 25), -1, 100),
      currentConsumedMah: readInt32(p, 27) === -1 ? null : readInt32(p, 27),
      energyConsumedHj: readInt32(p, 31) === -1 ? null : readInt32(p, 31),
      batteryRemainingPct: readInt8(p, 35) === -1 ? null : readInt8(p, 35),
      voltageBatteryV: firstBatteryVoltage(p)
    });
  },
  253: (message) => {
    const p = message.payload;
    if (p.length < 51) return null;
    return decodeWithFields(message, 'STATUSTEXT', {
      severity: readUInt8(p, 0),
      text: readCString(p, 1, 50)
    });
  }
};

export function decodeMavlinkMessage(message: MavlinkMessage): DecodedMavlinkMessage {
  return DECODERS[message.msgId]?.(message) ?? decodeWithFields(message, `MSG_${message.msgId}`, {});
}

function nullableScaled(value: number | null, sentinel: number, divisor: number): number | null {
  return value === null || value === sentinel ? null : value / divisor;
}

function firstBatteryVoltage(payload: Buffer): number | null {
  for (let offset = 5; offset < 25; offset += 2) {
    const mv = readUInt16(payload, offset);
    if (mv !== null && mv !== 65535) {
      return mv / 1000;
    }
  }
  return null;
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
  syntheticLocalNorthM: number | null = null;
  syntheticLocalEastM: number | null = null;
  syntheticLocalUpM: number | null = null;
  vehicleType: string | null = null;
  armed: boolean | null = null;
  mode: string | null = null;
  baseMode: number | null = null;
  customMode: number | null = null;
  gpsFix: string | null = null;
  satellitesVisible: number | null = null;
  batteryRemainingPct: number | null = null;
  batteryVoltageV: number | null = null;
  onboardControlSensorsHealth: number | null = null;
  missionSeq: number | null = null;
  lastStatusText: string | null = null;
  readonly trail: { eastM: number; northM: number; upM: number; timestampS: number }[] = [];

  apply(message: MavlinkMessage, nowS = performanceNowS()): void {
    this.connected = true;
    this.lastPacketTimeS = nowS;
    this.systemId = message.systemId;
    this.componentId = message.componentId;
    const decoded = decodeMavlinkMessage(message);
    this.applyDecoded(decoded);
    if (message.msgId === 0) {
      this.lastHeartbeatTimeS = nowS;
      this.applyHeartbeat(message.payload);
    } else if (message.msgId === 30) {
      this.applyAttitude(message.payload);
    } else if (message.msgId === 32) {
      this.applyLocalPosition(message.payload);
    } else if (message.msgId === 33) {
      this.applyGlobalPosition(message.payload);
    } else if (message.msgId === 74) {
      this.applyVfrHud(message.payload);
    }
    const [northM, eastM, upM] = this.localPosition();
    if (northM !== null && eastM !== null && upM !== null) {
      const last = this.trail[this.trail.length - 1];
      if (!last || Math.hypot(last.eastM - eastM, last.northM - northM, last.upM - upM) > 0.05) {
        this.trail.push({ eastM, northM, upM, timestampS: nowS });
        if (this.trail.length > 2400) {
          this.trail.splice(0, this.trail.length - 2400);
        }
      }
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
      },
      status: {
        armed: this.armed,
        mode: this.mode,
        baseMode: this.baseMode,
        customMode: this.customMode,
        gpsFix: this.gpsFix,
        satellitesVisible: this.satellitesVisible,
        batteryRemainingPct: this.batteryRemainingPct,
        batteryVoltageV: this.batteryVoltageV,
        onboardControlSensorsHealth: this.onboardControlSensorsHealth,
        missionSeq: this.missionSeq,
        lastStatusText: this.lastStatusText
      },
      commandCapabilities: defaultCommandCapabilities(true, false),
      diagnostics: {
        linkId: `${this.systemId ?? 'unknown'}:${this.componentId ?? 'unknown'}`,
        transport: 'udp',
        status: this.connected && (packetAgeS === null || packetAgeS < 2) ? 'connected' : 'degraded',
        packetsRx: this.trail.length,
        packetsTx: 0,
        decodedRx: this.trail.length,
        drops: 0,
        lastError: null
      },
      cameraStreams: [{
        id: `${this.systemId ?? 'unknown'}-mock-camera`,
        label: 'MAVLink camera metadata',
        kind: 'mock',
        uri: null,
        status: 'offline',
        captureSupported: false,
        recordingSupported: false,
        telemetrySubtitleSupported: true
      }],
      trail: this.trail.slice(-800)
    };
  }

  private applyDecoded(message: DecodedMavlinkMessage): void {
    const f = message.fields;
    if (message.msgId === 0) {
      this.baseMode = num(f.baseMode);
      this.customMode = num(f.customMode);
      this.armed = boolish(f.armed);
      this.mode = str(f.mode);
    } else if (message.msgId === 1) {
      this.onboardControlSensorsHealth = num(f.onboardControlSensorsHealth);
      this.batteryRemainingPct = num(f.batteryRemainingPct);
      this.batteryVoltageV = num(f.voltageBatteryV);
    } else if (message.msgId === 24) {
      this.gpsFix = str(f.fix);
      this.satellitesVisible = num(f.satellitesVisible);
      if (typeof f.latDeg === 'number') this.latDeg = f.latDeg;
      if (typeof f.lonDeg === 'number') this.lonDeg = f.lonDeg;
      if (typeof f.altM === 'number') this.altitudeM = f.altM;
    } else if (message.msgId === 42) {
      this.missionSeq = num(f.seq);
    } else if (message.msgId === 147) {
      this.batteryRemainingPct = num(f.batteryRemainingPct);
      this.batteryVoltageV = num(f.voltageBatteryV);
    } else if (message.msgId === 253) {
      this.lastStatusText = str(f.text);
    }
  }

  private applyHeartbeat(payload: Buffer): void {
    if (payload.length < 9) {
      return;
    }
    this.vehicleType = mavTypeLabel(payload.readUInt8(4));
  }

  localPosition(): [number | null, number | null, number | null] {
    if (this.syntheticLocalNorthM !== null && this.syntheticLocalEastM !== null && this.syntheticLocalUpM !== null) {
      return [this.syntheticLocalNorthM, this.syntheticLocalEastM, this.syntheticLocalUpM];
    }
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

  private applyLocalPosition(payload: Buffer): void {
    if (payload.length < 28) {
      return;
    }
    const north = readFloat(payload, 4);
    const east = readFloat(payload, 8);
    const down = readFloat(payload, 12);
    const vx = readFloat(payload, 16);
    const vy = readFloat(payload, 20);
    const vz = readFloat(payload, 24);
    if (north === null || east === null || down === null || vx === null || vy === null || vz === null) {
      return;
    }
    this.syntheticLocalNorthM = north;
    this.syntheticLocalEastM = east;
    this.syntheticLocalUpM = -down;
    this.velNMps = vx;
    this.velEMps = vy;
    this.velDMps = vz;
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

function flightModeLabel(customMode: number, baseMode: number): string {
  if ((baseMode & 0x80) === 0 && customMode === 0) {
    return 'Standby';
  }
  return customMode === 0 ? `base ${baseMode}` : `mode ${customMode}`;
}

function gpsFixLabel(fixType: number | null): string | null {
  switch (fixType) {
    case 0:
      return 'No GPS';
    case 1:
      return 'No fix';
    case 2:
      return '2D fix';
    case 3:
      return '3D fix';
    case 4:
      return 'DGPS';
    case 5:
      return 'RTK float';
    case 6:
      return 'RTK fixed';
    default:
      return fixType === null ? null : `fix ${fixType}`;
  }
}

function num(value: number | string | null | undefined): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function str(value: number | string | null | undefined): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null;
}

function boolish(value: number | string | null | undefined): boolean | null {
  if (typeof value !== 'number') {
    return null;
  }
  return value !== 0;
}

class MessageStatsStore {
  private readonly stats = new Map<string, InspectorMessageStats & { sampleTimes: number[]; lastTimeS: number }>();

  observe(message: DecodedMavlinkMessage, nowS: number): void {
    const key = `${message.systemId}:${message.componentId}:${message.msgId}`;
    const current = this.stats.get(key) ?? {
      key,
      msgId: message.msgId,
      name: message.name,
      systemId: message.systemId,
      componentId: message.componentId,
      lastAgeS: 0,
      rateHz: 0,
      count: 0,
      fields: {},
      sampleTimes: [],
      lastTimeS: nowS
    };
    current.count += 1;
    current.name = message.name;
    current.fields = message.fields;
    current.lastTimeS = nowS;
    current.sampleTimes.push(nowS);
    while (current.sampleTimes.length > 0 && nowS - current.sampleTimes[0] > 5) {
      current.sampleTimes.shift();
    }
    const windowS = current.sampleTimes.length > 1 ? current.sampleTimes[current.sampleTimes.length - 1] - current.sampleTimes[0] : 0;
    current.rateHz = windowS > 0 ? (current.sampleTimes.length - 1) / windowS : 0;
    this.stats.set(key, current);
  }

  snapshot(nowS: number): InspectorMessageStats[] {
    return [...this.stats.values()]
      .map((entry) => ({
        key: entry.key,
        msgId: entry.msgId,
        name: entry.name,
        systemId: entry.systemId,
        componentId: entry.componentId,
        lastAgeS: nowS - entry.lastTimeS,
        rateHz: entry.rateHz,
        count: entry.count,
        fields: { ...entry.fields }
      }))
      .sort((a, b) => a.systemId - b.systemId || a.componentId - b.componentId || a.msgId - b.msgId);
  }
}

export class VehicleRegistry {
  private readonly vehicles = new Map<string, LiveVehicleState>();
  private readonly inspector = new MessageStatsStore();
  private readonly events: SessionEvent[] = [];
  private previous = new Map<string, { armed: boolean | null; mode: string | null; gpsFix: string | null; missionSeq: number | null; connected: boolean }>();
  selectedVehicleId: string | null = null;
  packetCount = 0;
  decodedCount = 0;

  apply(message: MavlinkMessage, nowS = performanceNowS()): void {
    const decoded = decodeMavlinkMessage(message);
    const id = vehicleId(message.systemId, message.componentId);
    const vehicle = this.vehicles.get(id) ?? new LiveVehicleState();
    this.packetCount += 1;
    this.decodedCount += decoded.name.startsWith('MSG_') ? 0 : 1;
    this.vehicles.set(id, vehicle);
    if (this.selectedVehicleId === null) {
      this.selectedVehicleId = id;
    }
    const before = this.stateTransitions(id, vehicle, nowS);
    vehicle.apply(message, nowS);
    this.inspector.observe(decoded, nowS);
    this.detectEvents(id, before, vehicle, decoded, nowS);
  }

  select(id: string): void {
    if (this.vehicles.has(id)) {
      this.selectedVehicleId = id;
    }
  }

  addMarker(label: string, nowS = performanceNowS(), vehicleIdOverride = this.selectedVehicleId): SessionEvent | null {
    const vehicle = vehicleIdOverride ? this.vehicles.get(vehicleIdOverride) : null;
    const event = this.makeEvent(nowS, vehicleIdOverride, 'info', 'marker', label, vehicle ?? null);
    this.pushEvent(event);
    return event;
  }

  snapshot(nowS = performanceNowS()): SessionSnapshotPayload {
    const vehicles = [...this.vehicles.entries()].map(([id, vehicle]) => ({ ...vehicle.toJsonable(nowS), id }));
    const snapshot: SessionSnapshotPayload = {
      type: 'session_snapshot',
      vehicles,
      selectedVehicleId: this.selectedVehicleId,
      messages: this.inspector.snapshot(nowS),
      events: this.events.slice(-240).reverse(),
      packetCount: this.packetCount,
      decodedCount: this.decodedCount,
      analysis: buildMultiVehicleAnalysis({
        type: 'session_snapshot',
        vehicles,
        selectedVehicleId: this.selectedVehicleId,
        messages: [],
        events: [],
        packetCount: this.packetCount,
        decodedCount: this.decodedCount
      })
    };
    return snapshot;
  }

  selectedVehicle(nowS = performanceNowS()): VehicleStatePayload {
    const selected = this.selectedVehicleId ? this.vehicles.get(this.selectedVehicleId) : null;
    return selected?.toJsonable(nowS) ?? new LiveVehicleState().toJsonable(nowS);
  }

  private stateTransitions(id: string, vehicle: LiveVehicleState, nowS: number): { armed: boolean | null; mode: string | null; gpsFix: string | null; missionSeq: number | null; connected: boolean } {
    const packetAgeS = vehicle.lastPacketTimeS === null ? null : nowS - vehicle.lastPacketTimeS;
    return {
      armed: vehicle.armed,
      mode: vehicle.mode,
      gpsFix: vehicle.gpsFix,
      missionSeq: vehicle.missionSeq,
      connected: vehicle.connected && (packetAgeS === null || packetAgeS < 2)
    };
  }

  private detectEvents(
    id: string,
    before: { armed: boolean | null; mode: string | null; gpsFix: string | null; missionSeq: number | null; connected: boolean },
    vehicle: LiveVehicleState,
    decoded: DecodedMavlinkMessage,
    nowS: number
  ): void {
    const after = this.stateTransitions(id, vehicle, nowS);
    if (!before.connected && after.connected) {
      this.pushEvent(this.makeEvent(nowS, id, 'info', 'connect', `Vehicle ${id} connected`, vehicle));
    }
    if (before.armed !== null && after.armed !== null && before.armed !== after.armed) {
      this.pushEvent(this.makeEvent(nowS, id, after.armed ? 'warning' : 'info', 'arming', after.armed ? 'Armed' : 'Disarmed', vehicle));
    }
    if (before.mode && after.mode && before.mode !== after.mode) {
      this.pushEvent(this.makeEvent(nowS, id, 'info', 'mode', `Mode ${after.mode}`, vehicle));
    }
    if (before.gpsFix && after.gpsFix && before.gpsFix !== after.gpsFix) {
      const good = /3D|DGPS|RTK/.test(after.gpsFix);
      this.pushEvent(this.makeEvent(nowS, id, good ? 'info' : 'warning', 'gps', `GPS ${after.gpsFix}`, vehicle));
    }
    if (before.missionSeq !== null && after.missionSeq !== null && before.missionSeq !== after.missionSeq) {
      this.pushEvent(this.makeEvent(nowS, id, 'info', 'mission', `Mission item ${after.missionSeq}`, vehicle));
    }
    if (decoded.msgId === 253 && typeof decoded.fields.text === 'string' && decoded.fields.text.length > 0) {
      this.pushEvent(this.makeEvent(nowS, id, 'info', 'statustext', decoded.fields.text, vehicle));
    }
    this.previous.set(id, after);
  }

  private makeEvent(nowS: number, id: string | null, level: SessionEvent['level'], kind: string, label: string, vehicle: LiveVehicleState | null): SessionEvent {
    const [northM, eastM, upM] = vehicle?.localPosition() ?? [null, null, null];
    return {
      id: `${Math.round(nowS * 1000)}-${this.events.length}`,
      timestampS: nowS,
      vehicleId: id,
      level,
      kind,
      label,
      position: northM === null || eastM === null || upM === null ? null : { eastM, northM, upM }
    };
  }

  private pushEvent(event: SessionEvent): void {
    this.events.push(event);
    if (this.events.length > 500) {
      this.events.splice(0, this.events.length - 500);
    }
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
  readonly registry = new VehicleRegistry();
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
      this.registry.apply(message, nowS);
    }
    const payload = this.registry.selectedVehicle(nowS);
    const snapshot = this.registry.snapshot(nowS);
    this.emit('vehicle-state', payload);
    this.emit('session-snapshot', snapshot);
    return payload;
  }

  selectVehicle(id: string): SessionSnapshotPayload {
    this.registry.select(id);
    const snapshot = this.registry.snapshot();
    this.emit('session-snapshot', snapshot);
    this.emit('vehicle-state', this.registry.selectedVehicle());
    return snapshot;
  }

  addMarker(label: string): SessionSnapshotPayload {
    this.registry.addMarker(label);
    const snapshot = this.registry.snapshot();
    this.emit('session-snapshot', snapshot);
    return snapshot;
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
