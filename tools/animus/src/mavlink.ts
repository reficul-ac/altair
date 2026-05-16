import dgram, { type RemoteInfo, type Socket } from 'node:dgram';
import { EventEmitter } from 'node:events';
import { buildMultiVehicleAnalysis, buildVehicleReadiness, defaultCommandCapabilities, evaluateGuardedCommand, firmwareIdentity, normalizeArmingState, normalizeFailsafeState, normalizeFlightMode, normalizeMissionState } from './parity.js';
import { validateMission } from './state.js';
import type {
  CameraStream,
  CommandAuditEntry,
  CommandCapabilityState,
  CommandDispatchResult,
  CommandName,
  CommandTransaction,
  CommandAuthorityMode,
  GuardedCommandRequest,
  LinkDiagnostics,
  LogListEntry,
  MavlinkProtocolDiagnostics,
  MissionPlan,
  MissionState,
  MissionTransferState,
  MockLinkState,
  MultiVehicleAnalysis,
  NormalizedArmingState,
  NormalizedFailsafeState,
  NormalizedFlightMode,
  FirmwareIdentity,
  VehicleReadiness,
  WritableLinkState,
  OperationAuditEntry,
  ParameterEditRequest,
  ParameterEditResult,
  ParameterValue,
  ProtocolOperation,
  ProtocolOperationDomain,
  ProtocolOperationState
} from './state.js';

export const MAVLINK_V1_STX = 0xfe;
export const MAVLINK_V2_STX = 0xfd;
export const DEFAULT_LISTEN_HOST = '127.0.0.1';
export const DEFAULT_LISTEN_PORT = 14551;
export const DEFAULT_QGC_HOST = '127.0.0.1';
export const DEFAULT_QGC_PORT = 14550;

export const MAVLINK_CRC_EXTRA: Record<number, number> = {
  0: 50,
  1: 124,
  21: 159,
  22: 220,
  23: 168,
  24: 24,
  30: 39,
  32: 185,
  33: 104,
  36: 222,
  39: 254,
  40: 230,
  42: 28,
  43: 132,
  44: 221,
  45: 232,
  46: 11,
  47: 153,
  51: 196,
  65: 118,
  73: 38,
  74: 20,
  76: 152,
  77: 143,
  115: 4,
  117: 128,
  118: 56,
  119: 116,
  120: 134,
  121: 237,
  122: 203,
  133: 6,
  134: 229,
  135: 203,
  136: 1,
  147: 154,
  242: 104,
  253: 83,
  259: 92,
  260: 146,
  262: 12,
  266: 38,
  267: 35,
  268: 14
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

export type MavlinkProtocolFrameInfo = {
  mavlinkVersion: 1 | 2;
  msgId: number;
  incompatFlags: number;
  signed: boolean;
  supportedDialect: boolean;
};

export type MavlinkServiceConfig = {
  listenHost: string;
  listenPort: number;
  qgcForwarding: boolean;
  qgcEndpoints: Endpoint[];
  writableAnimus: boolean;
  authorityMode: CommandAuthorityMode;
};

export type AnimusSessionContext = {
  sessionId: string;
  operatorId: string;
  appSource: string;
  processSource: string;
};

export type VehicleStatePayload = {
  type: 'vehicle_state';
  id?: string;
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
    modeState?: NormalizedFlightMode;
    firmware?: FirmwareIdentity;
    armingState?: NormalizedArmingState;
    failsafeState?: NormalizedFailsafeState;
    missionState?: MissionState;
    readiness?: VehicleReadiness;
    baseMode: number | null;
    customMode: number | null;
    systemStatus?: number | null;
    gpsFix: string | null;
    satellitesVisible: number | null;
    batteryRemainingPct: number | null;
    batteryVoltageV: number | null;
    onboardControlSensorsHealth: number | null;
    missionSeq: number | null;
    lastStatusText: string | null;
  };
  mission?: {
    activeSeq: number | null;
    state?: MissionState;
    waypoints?: {
      seq: number;
      latDeg: number;
      lonDeg: number;
      altitudeM: number | null;
      command?: string | null;
      frame?: string | null;
    }[];
  };
  home?: {
    latDeg: number;
    lonDeg: number;
    altitudeM: number;
  };
  parameters?: ParameterValue[];
  logs?: LogListEntry[];
  terrain?: {
    latDeg: number | null;
    lonDeg: number | null;
    spacingM: number | null;
    terrainHeightM: number | null;
    currentHeightM: number | null;
    pending: number | null;
    loaded: number | null;
  };
  geofences?: [];
  rallyPoints?: [];
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
  commandTransactions?: CommandTransaction[];
  commandAudit?: CommandAuditEntry[];
  protocolOperations?: ProtocolOperation[];
  operationAudit?: OperationAuditEntry[];
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

export const MAV_CMD = {
  NAV_WAYPOINT: 16,
  NAV_RETURN_TO_LAUNCH: 20,
  NAV_LAND: 21,
  NAV_TAKEOFF: 22,
  IMAGE_START_CAPTURE: 2000,
  VIDEO_START_CAPTURE: 2500,
  VIDEO_STOP_CAPTURE: 2501,
  SET_CAMERA_ZOOM: 531,
  SET_CAMERA_FOCUS: 532,
  COMPONENT_ARM_DISARM: 400,
  DO_GO_AROUND: 191,
  DO_PAUSE_CONTINUE: 193,
  MISSION_START: 300,
  DO_CHANGE_ALTITUDE: 186
} as const;

const COMMAND_TRANSACTION_TIMEOUT_S = 2;
const PROTOCOL_OPERATION_TIMEOUT_S = 5;
const MAX_COMMAND_TRANSACTIONS = 80;
const MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES = 20;
const MAX_PROTOCOL_OPERATIONS = 120;

export const MAV_RESULT_LABELS: Record<number, string> = {
  0: 'accepted',
  1: 'temporarily rejected',
  2: 'denied',
  3: 'unsupported',
  4: 'failed',
  5: 'in progress'
};

export function encodeCommandLong(command: number, targetSystem: number, targetComponent: number, params: number[] = [], confirmation = 0, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(33);
  for (let index = 0; index < 7; index += 1) {
    payload.writeFloatLE(Number.isFinite(params[index]) ? params[index] : 0, index * 4);
  }
  payload.writeUInt16LE(command, 28);
  payload.writeUInt8(targetSystem, 30);
  payload.writeUInt8(targetComponent, 31);
  payload.writeUInt8(confirmation, 32);
  return mavlinkV1Frame(76, payload, seq, sourceSystem, sourceComponent);
}

export function encodeMissionCount(count: number, targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(4);
  payload.writeUInt16LE(count, 0);
  payload.writeUInt8(targetSystem, 2);
  payload.writeUInt8(targetComponent, 3);
  return mavlinkV1Frame(44, payload, seq, sourceSystem, sourceComponent);
}

export function encodeMissionClearAll(targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  return mavlinkV1Frame(45, Buffer.from([targetSystem, targetComponent]), seq, sourceSystem, sourceComponent);
}

export function encodeMissionRequestList(targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  return mavlinkV1Frame(43, Buffer.from([targetSystem, targetComponent]), seq, sourceSystem, sourceComponent);
}

export function encodeMissionRequestInt(itemSeq: number, targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(4);
  payload.writeUInt16LE(itemSeq, 0);
  payload.writeUInt8(targetSystem, 2);
  payload.writeUInt8(targetComponent, 3);
  return mavlinkV1Frame(51, payload, seq, sourceSystem, sourceComponent);
}

export function encodeMissionItemInt(waypoint: MissionPlan['waypoints'][number], targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(37);
  payload.writeFloatLE(waypoint.acceptance_radius_m, 0);
  payload.writeFloatLE(waypoint.throttle, 4);
  payload.writeFloatLE(0, 8);
  payload.writeFloatLE(Number.NaN, 12);
  payload.writeInt32LE(Math.round(waypoint.lat_deg * 1e7), 16);
  payload.writeInt32LE(Math.round(waypoint.lon_deg * 1e7), 20);
  payload.writeFloatLE(waypoint.alt_m, 24);
  payload.writeUInt16LE(waypoint.seq, 28);
  payload.writeUInt16LE(MAV_CMD.NAV_WAYPOINT, 30);
  payload.writeUInt8(targetSystem, 32);
  payload.writeUInt8(targetComponent, 33);
  payload.writeUInt8(6, 34);
  payload.writeUInt8(waypoint.seq === 0 ? 1 : 0, 35);
  payload.writeUInt8(1, 36);
  return mavlinkV1Frame(73, payload, seq, sourceSystem, sourceComponent);
}

export function encodeParamRequestList(targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  return mavlinkV1Frame(21, Buffer.from([targetSystem, targetComponent]), seq, sourceSystem, sourceComponent);
}

export function encodeParamSet(name: string, value: number, paramType: number, targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(23);
  payload.writeFloatLE(value, 0);
  payload.writeUInt8(targetSystem, 4);
  payload.writeUInt8(targetComponent, 5);
  payload.write(name.slice(0, 16), 6, 'ascii');
  payload.writeUInt8(paramType, 22);
  return mavlinkV1Frame(23, payload, seq, sourceSystem, sourceComponent);
}

export function encodeLogRequestList(targetSystem: number, targetComponent: number, start = 0, end = 0xffff, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(6);
  payload.writeUInt16LE(start, 0);
  payload.writeUInt16LE(end, 2);
  payload.writeUInt8(targetSystem, 4);
  payload.writeUInt8(targetComponent, 5);
  return mavlinkV1Frame(117, payload, seq, sourceSystem, sourceComponent);
}

export function encodeLogRequestData(logId: number, offset: number, count: number, targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(12);
  payload.writeUInt32LE(offset, 0);
  payload.writeUInt32LE(count, 4);
  payload.writeUInt16LE(logId, 8);
  payload.writeUInt8(targetSystem, 10);
  payload.writeUInt8(targetComponent, 11);
  return mavlinkV1Frame(119, payload, seq, sourceSystem, sourceComponent);
}

export function encodeLogErase(targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  return mavlinkV1Frame(121, Buffer.from([targetSystem, targetComponent]), seq, sourceSystem, sourceComponent);
}

export function encodeLogRequestEnd(targetSystem: number, targetComponent: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  return mavlinkV1Frame(122, Buffer.from([targetSystem, targetComponent]), seq, sourceSystem, sourceComponent);
}

export function encodeTerrainCheck(latDeg: number, lonDeg: number, seq = 1, sourceSystem = 255, sourceComponent = 190): Buffer {
  const payload = Buffer.alloc(8);
  payload.writeInt32LE(Math.round(latDeg * 1e7), 0);
  payload.writeInt32LE(Math.round(lonDeg * 1e7), 4);
  return mavlinkV1Frame(135, payload, seq, sourceSystem, sourceComponent);
}

export function decodeCommandAck(message: MavlinkMessage): { command: number; result: number; label: string } | null {
  if (message.msgId !== 77 || message.payload.length < 3) {
    return null;
  }
  const result = message.payload.readUInt8(2);
  return {
    command: message.payload.readUInt16LE(0),
    result,
    label: MAV_RESULT_LABELS[result] ?? `result ${result}`
  };
}

export function decodeMissionRequestInt(message: MavlinkMessage): { seq: number; targetSystem: number; targetComponent: number } | null {
  if (message.msgId !== 51 || message.payload.length < 4) {
    return null;
  }
  return {
    seq: message.payload.readUInt16LE(0),
    targetSystem: message.payload.readUInt8(2),
    targetComponent: message.payload.readUInt8(3)
  };
}

export function decodeMissionAck(message: MavlinkMessage): { targetSystem: number; targetComponent: number; type: number } | null {
  if (message.msgId !== 47 || message.payload.length < 3) {
    return null;
  }
  return {
    targetSystem: message.payload.readUInt8(0),
    targetComponent: message.payload.readUInt8(1),
    type: message.payload.readUInt8(2)
  };
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

export function inspectMavlinkProtocolFrames(data: Uint8Array): MavlinkProtocolFrameInfo[] {
  const frames: MavlinkProtocolFrameInfo[] = [];
  for (let index = 0; index < data.length;) {
    const stx = data[index];
    if (stx === MAVLINK_V1_STX) {
      if (index + 8 > data.length) break;
      const payloadLen = data[index + 1];
      const frameLen = 6 + payloadLen + 2;
      if (index + frameLen > data.length) break;
      const msgId = data[index + 5];
      frames.push({ mavlinkVersion: 1, msgId, incompatFlags: 0, signed: false, supportedDialect: MAVLINK_CRC_EXTRA[msgId] !== undefined });
      index += frameLen;
      continue;
    }
    if (stx === MAVLINK_V2_STX) {
      if (index + 12 > data.length) break;
      const payloadLen = data[index + 1];
      const incompatFlags = data[index + 2];
      const signed = (incompatFlags & 0x01) !== 0;
      const frameLen = 10 + payloadLen + 2 + (signed ? 13 : 0);
      if (index + frameLen > data.length) break;
      const msgId = data[index + 7] | (data[index + 8] << 8) | (data[index + 9] << 16);
      frames.push({ mavlinkVersion: 2, msgId, incompatFlags, signed, supportedDialect: MAVLINK_CRC_EXTRA[msgId] !== undefined });
      index += frameLen;
      continue;
    }
    index += 1;
  }
  return frames;
}

class MavlinkProtocolTracker {
  private v1Frames = 0;
  private v2Frames = 0;
  private signedFrames = 0;
  private incompatFlags = 0;
  private unsupportedDialectMessages = 0;
  private readonly unsupportedMessageIds = new Set<number>();

  observe(packet: Uint8Array): void {
    for (const frame of inspectMavlinkProtocolFrames(packet)) {
      if (frame.mavlinkVersion === 1) this.v1Frames += 1;
      else this.v2Frames += 1;
      if (frame.signed) this.signedFrames += 1;
      this.incompatFlags |= frame.incompatFlags;
      if (!frame.supportedDialect) {
        this.unsupportedDialectMessages += 1;
        this.unsupportedMessageIds.add(frame.msgId);
      }
    }
  }

  snapshot(): MavlinkProtocolDiagnostics {
    const total = this.v1Frames + this.v2Frames;
    return {
      mavlinkVersion: total === 0 ? 'none' : this.v1Frames > 0 && this.v2Frames > 0 ? 'mixed' : this.v2Frames > 0 ? 'v2' : 'v1',
      signed: this.signedFrames > 0,
      signingRequired: (this.incompatFlags & 0x01) !== 0,
      dialectCoverage: total === 0 ? 'unknown' : this.unsupportedDialectMessages === 0 ? 'supported' : this.unsupportedDialectMessages >= total ? 'unsupported' : 'partial',
      incompatFlags: this.incompatFlags,
      v1Frames: this.v1Frames,
      v2Frames: this.v2Frames,
      unsupportedDialectMessages: this.unsupportedDialectMessages,
      unsupportedMessageIds: [...this.unsupportedMessageIds].sort((a, b) => a - b).slice(0, 12)
    };
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
      firmware: firmwareIdentity(p.readUInt8(5)).label,
      mode: normalizeFlightMode(p.readUInt8(5), customMode, baseMode).label
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
  21: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'PARAM_REQUEST_LIST', { targetSystem: readUInt8(p, 0), targetComponent: readUInt8(p, 1) });
  },
  22: (message) => {
    const p = message.payload;
    if (p.length < 25) return null;
    return decodeWithFields(message, 'PARAM_VALUE', {
      paramValue: readFloat(p, 0),
      paramCount: readUInt16(p, 4),
      paramIndex: readUInt16(p, 6),
      paramId: readCString(p, 8, 16),
      paramType: readUInt8(p, 24)
    });
  },
  23: (message) => {
    const p = message.payload;
    if (p.length < 23) return null;
    return decodeWithFields(message, 'PARAM_SET', {
      paramValue: readFloat(p, 0),
      targetSystem: readUInt8(p, 4),
      targetComponent: readUInt8(p, 5),
      paramId: readCString(p, 6, 16),
      paramType: readUInt8(p, 22)
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
  39: (message) => {
    const p = message.payload;
    if (p.length < 37) return null;
    return decodeWithFields(message, 'MISSION_ITEM', {
      param1: readFloat(p, 0),
      param2: readFloat(p, 4),
      param3: readFloat(p, 8),
      param4: readFloat(p, 12),
      x: readFloat(p, 16),
      y: readFloat(p, 20),
      z: readFloat(p, 24),
      seq: readUInt16(p, 28),
      command: readUInt16(p, 30),
      targetSystem: readUInt8(p, 32),
      targetComponent: readUInt8(p, 33),
      frame: readUInt8(p, 34),
      current: readUInt8(p, 35),
      autocontinue: readUInt8(p, 36)
    });
  },
  40: (message) => {
    const p = message.payload;
    if (p.length < 4) return null;
    return decodeWithFields(message, 'MISSION_REQUEST', { seq: readUInt16(p, 0), targetSystem: readUInt8(p, 2), targetComponent: readUInt8(p, 3) });
  },
  42: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'MISSION_CURRENT', { seq: readUInt16(p, 0) });
  },
  43: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'MISSION_REQUEST_LIST', { targetSystem: readUInt8(p, 0), targetComponent: readUInt8(p, 1) });
  },
  44: (message) => {
    const p = message.payload;
    if (p.length < 4) return null;
    return decodeWithFields(message, 'MISSION_COUNT', {
      count: readUInt16(p, 0),
      targetSystem: readUInt8(p, 2),
      targetComponent: readUInt8(p, 3)
    });
  },
  45: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'MISSION_CLEAR_ALL', { targetSystem: readUInt8(p, 0), targetComponent: readUInt8(p, 1) });
  },
  46: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'MISSION_ITEM_REACHED', { seq: readUInt16(p, 0) });
  },
  47: (message) => {
    const ack = decodeMissionAck(message);
    return ack ? decodeWithFields(message, 'MISSION_ACK', ack) : null;
  },
  51: (message) => {
    const request = decodeMissionRequestInt(message);
    return request ? decodeWithFields(message, 'MISSION_REQUEST_INT', request) : null;
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
  73: (message) => {
    const p = message.payload;
    if (p.length < 37) return null;
    return decodeWithFields(message, 'MISSION_ITEM_INT', {
      acceptanceRadiusM: readFloat(p, 0),
      throttle: readFloat(p, 4),
      xLatE7: readInt32(p, 16),
      yLonE7: readInt32(p, 20),
      zAltM: readFloat(p, 24),
      seq: readUInt16(p, 28),
      command: readUInt16(p, 30),
      targetSystem: readUInt8(p, 32),
      targetComponent: readUInt8(p, 33),
      frame: readUInt8(p, 34),
      current: readUInt8(p, 35),
      autocontinue: readUInt8(p, 36)
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
  76: (message) => {
    const p = message.payload;
    if (p.length < 33) return null;
    return decodeWithFields(message, 'COMMAND_LONG', {
      param1: readFloat(p, 0),
      param2: readFloat(p, 4),
      param3: readFloat(p, 8),
      param4: readFloat(p, 12),
      param5: readFloat(p, 16),
      param6: readFloat(p, 20),
      param7: readFloat(p, 24),
      command: readUInt16(p, 28),
      targetSystem: readUInt8(p, 30),
      targetComponent: readUInt8(p, 31),
      confirmation: readUInt8(p, 32)
    });
  },
  77: (message) => {
    const ack = decodeCommandAck(message);
    return ack ? decodeWithFields(message, 'COMMAND_ACK', ack) : null;
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
  117: (message) => {
    const p = message.payload;
    if (p.length < 6) return null;
    return decodeWithFields(message, 'LOG_REQUEST_LIST', { start: readUInt16(p, 0), end: readUInt16(p, 2), targetSystem: readUInt8(p, 4), targetComponent: readUInt8(p, 5) });
  },
  118: (message) => {
    const p = message.payload;
    if (p.length < 14) return null;
    return decodeWithFields(message, 'LOG_ENTRY', { id: readUInt16(p, 0), numLogs: readUInt16(p, 2), lastLogNum: readUInt16(p, 4), timeUtc: readUInt32(p, 6), sizeBytes: readUInt32(p, 10) });
  },
  119: (message) => {
    const p = message.payload;
    if (p.length < 12) return null;
    return decodeWithFields(message, 'LOG_REQUEST_DATA', { ofs: readUInt32(p, 0), count: readUInt32(p, 4), id: readUInt16(p, 8), targetSystem: readUInt8(p, 10), targetComponent: readUInt8(p, 11) });
  },
  120: (message) => {
    const p = message.payload;
    if (p.length < 7) return null;
    return decodeWithFields(message, 'LOG_DATA', { ofs: readUInt32(p, 0), id: readUInt16(p, 4), count: readUInt8(p, 6) });
  },
  121: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'LOG_ERASE', { targetSystem: readUInt8(p, 0), targetComponent: readUInt8(p, 1) });
  },
  122: (message) => {
    const p = message.payload;
    if (p.length < 2) return null;
    return decodeWithFields(message, 'LOG_REQUEST_END', { targetSystem: readUInt8(p, 0), targetComponent: readUInt8(p, 1) });
  },
  133: (message) => {
    const p = message.payload;
    if (p.length < 18) return null;
    return decodeWithFields(message, 'TERRAIN_REQUEST', { latDeg: (readInt32(p, 0) ?? 0) / 1e7, lonDeg: (readInt32(p, 4) ?? 0) / 1e7, gridSpacingM: readUInt16(p, 8), mask: Number(p.readBigUInt64LE(10)) });
  },
  134: (message) => {
    const p = message.payload;
    if (p.length < 43) return null;
    return decodeWithFields(message, 'TERRAIN_DATA', { latDeg: (readInt32(p, 0) ?? 0) / 1e7, lonDeg: (readInt32(p, 4) ?? 0) / 1e7, gridSpacingM: readUInt16(p, 8), gridbit: readUInt8(p, 10) });
  },
  135: (message) => {
    const p = message.payload;
    if (p.length < 8) return null;
    return decodeWithFields(message, 'TERRAIN_CHECK', { latDeg: (readInt32(p, 0) ?? 0) / 1e7, lonDeg: (readInt32(p, 4) ?? 0) / 1e7 });
  },
  136: (message) => {
    const p = message.payload;
    if (p.length < 22) return null;
    return decodeWithFields(message, 'TERRAIN_REPORT', { latDeg: (readInt32(p, 0) ?? 0) / 1e7, lonDeg: (readInt32(p, 4) ?? 0) / 1e7, spacingM: readUInt16(p, 8), terrainHeightM: readFloat(p, 10), currentHeightM: readFloat(p, 14), pending: readUInt16(p, 18), loaded: readUInt16(p, 20) });
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
  242: (message) => {
    const p = message.payload;
    if (p.length < 52) return null;
    return decodeWithFields(message, 'HOME_POSITION', {
      latDeg: (readInt32(p, 0) ?? 0) / 1e7,
      lonDeg: (readInt32(p, 4) ?? 0) / 1e7,
      altitudeM: (readInt32(p, 8) ?? 0) / 1000,
      xM: readFloat(p, 12),
      yM: readFloat(p, 16),
      zM: readFloat(p, 20),
      approachXM: readFloat(p, 40),
      approachYM: readFloat(p, 44),
      approachZM: readFloat(p, 48)
    });
  },
  253: (message) => {
    const p = message.payload;
    if (p.length < 51) return null;
    return decodeWithFields(message, 'STATUSTEXT', {
      severity: readUInt8(p, 0),
      text: readCString(p, 1, 50)
    });
  },
  259: (message) => {
    const p = message.payload;
    if (p.length < 235) return null;
    return decodeWithFields(message, 'CAMERA_INFORMATION', {
      timeBootMs: readUInt32(p, 0),
      vendorName: readCString(p, 4, 32),
      modelName: readCString(p, 36, 32),
      firmwareVersion: readUInt32(p, 68),
      focalLengthMm: readFloat(p, 72),
      sensorSizeH: readFloat(p, 76),
      sensorSizeV: readFloat(p, 80),
      resolutionH: readUInt16(p, 84),
      resolutionV: readUInt16(p, 86),
      flags: readUInt32(p, 88),
      camDefinitionVersion: readUInt16(p, 233)
    });
  },
  260: (message) => {
    const p = message.payload;
    if (p.length < 13) return null;
    return decodeWithFields(message, 'CAMERA_SETTINGS', { timeBootMs: readUInt32(p, 0), modeId: readUInt8(p, 4), zoomLevel: readFloat(p, 5), focusLevel: readFloat(p, 9) });
  },
  262: (message) => {
    const p = message.payload;
    if (p.length < 18) return null;
    return decodeWithFields(message, 'CAMERA_CAPTURE_STATUS', { timeBootMs: readUInt32(p, 0), imageStatus: readUInt8(p, 4), videoStatus: readUInt8(p, 5), imageIntervalS: readFloat(p, 6), recordingTimeMs: readUInt32(p, 10), availableCapacityMb: readFloat(p, 14) });
  },
  266: (message) => {
    const p = message.payload;
    if (p.length < 5) return null;
    return decodeWithFields(message, 'LOGGING_DATA', { sequence: readUInt16(p, 0), targetSystem: readUInt8(p, 2), targetComponent: readUInt8(p, 3), length: readUInt8(p, 4) });
  },
  267: (message) => {
    const p = message.payload;
    if (p.length < 7) return null;
    return decodeWithFields(message, 'LOGGING_DATA_ACKED', { sequence: readUInt16(p, 0), targetSystem: readUInt8(p, 2), targetComponent: readUInt8(p, 3), length: readUInt8(p, 4), firstMessageOffset: readUInt16(p, 5) });
  },
  268: (message) => {
    const p = message.payload;
    if (p.length < 4) return null;
    return decodeWithFields(message, 'LOGGING_ACK', { sequence: readUInt16(p, 0), targetSystem: readUInt8(p, 2), targetComponent: readUInt8(p, 3) });
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

function isRetryableCommandState(state: CommandTransaction['state']): boolean {
  return state === 'timeout' || state === 'failed';
}

function summarizeProtocol(protocol: MavlinkProtocolDiagnostics): string {
  const signing = protocol.signed ? 'signed' : protocol.signingRequired ? 'signing-required' : 'unsigned';
  return `${protocol.mavlinkVersion}/${signing}/${protocol.dialectCoverage}`;
}

function typedConfirmationBlocked(request: GuardedCommandRequest, encodedParams: number[]): string | null {
  if (request.confirmationType !== 'typed-altitude') {
    return null;
  }
  if (request.command !== 'takeoff' && request.command !== 'change-altitude') {
    return 'typed altitude confirmation is only valid for altitude commands';
  }
  const typedAltitude = typeof request.params?.altitudeM === 'number' ? request.params.altitudeM : Number.NaN;
  if (!Number.isFinite(typedAltitude)) {
    return 'typed altitude confirmation is required';
  }
  const encodedAltitude = request.command === 'takeoff' ? encodedParams[6] : encodedParams[0];
  if (!Number.isFinite(encodedAltitude) || Math.abs(encodedAltitude - typedAltitude) > 1e-6) {
    return 'typed altitude does not match the encoded command payload';
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
  systemStatus: number | null = null;
  autopilot: number | null = null;
  gpsFix: string | null = null;
  satellitesVisible: number | null = null;
  batteryRemainingPct: number | null = null;
  batteryVoltageV: number | null = null;
  onboardControlSensorsHealth: number | null = null;
  missionSeq: number | null = null;
  missionCount: number | null = null;
  missionAckType: number | null = null;
  lastStatusText: string | null = null;
  home: { latDeg: number; lonDeg: number; altitudeM: number } | null = null;
  terrain: VehicleStatePayload['terrain'] | null = null;
  readonly parameters = new Map<string, ParameterValue & { index?: number | null; count?: number | null; updatedAtS?: number }>();
  readonly missionItems = new Map<number, {
    seq: number;
    latDeg: number;
    lonDeg: number;
    altitudeM: number | null;
    command?: string | null;
    frame?: string | null;
  }>();
  readonly logs = new Map<number, LogListEntry>();
  camera: CameraStream | null = null;
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
    const connected = this.connected && (packetAgeS === null || packetAgeS < 2);
    const firmware = firmwareIdentity(this.autopilot);
    const modeState = normalizeFlightMode(this.autopilot, this.customMode, this.baseMode);
    const failsafeState = normalizeFailsafeState(this.autopilot, this.systemStatus);
    const missionState = normalizeMissionState({
      activeSeq: this.missionSeq,
      totalItems: this.missionCount,
      modeState,
      lastAckType: this.missionAckType
    });
    const readiness = buildVehicleReadiness({
      connected,
      packetAgeS,
      status: {
        armed: this.armed,
        mode: this.mode,
        firmware,
        modeState,
        failsafeState,
        missionState,
        baseMode: this.baseMode,
        customMode: this.customMode,
        systemStatus: this.systemStatus,
        gpsFix: this.gpsFix,
        satellitesVisible: this.satellitesVisible,
        batteryRemainingPct: this.batteryRemainingPct,
        batteryVoltageV: this.batteryVoltageV,
        onboardControlSensorsHealth: this.onboardControlSensorsHealth,
        missionSeq: this.missionSeq,
        lastStatusText: this.lastStatusText
      }
    });
    return {
      type: 'vehicle_state',
      connected,
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
        modeState,
        firmware,
        armingState: normalizeArmingState(this.armed, readiness),
        failsafeState,
        missionState,
        readiness,
        baseMode: this.baseMode,
        customMode: this.customMode,
        systemStatus: this.systemStatus,
        gpsFix: this.gpsFix,
        satellitesVisible: this.satellitesVisible,
        batteryRemainingPct: this.batteryRemainingPct,
        batteryVoltageV: this.batteryVoltageV,
        onboardControlSensorsHealth: this.onboardControlSensorsHealth,
        missionSeq: this.missionSeq,
        lastStatusText: this.lastStatusText
      },
      mission: {
        activeSeq: this.missionSeq,
        state: missionState,
        waypoints: [...this.missionItems.values()].sort((a, b) => a.seq - b.seq)
      },
      home: this.home ? { ...this.home } : undefined,
      parameters: [...this.parameters.values()]
        .sort((a, b) => a.name.localeCompare(b.name))
        .map((param) => ({ ...param })),
      logs: [...this.logs.values()].sort((a, b) => a.id - b.id).map((entry) => ({ ...entry })),
      terrain: this.terrain ? { ...this.terrain } : undefined,
      geofences: [],
      rallyPoints: [],
      commandCapabilities: defaultCommandCapabilities(connected, false, { packetAgeS, readiness }),
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
      cameraStreams: [this.camera ?? {
        id: `${this.systemId ?? 'unknown'}-mavlink-camera`,
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
      this.systemStatus = num(f.systemStatus);
      this.autopilot = num(f.autopilot);
      this.armed = boolish(f.armed);
      this.mode = normalizeFlightMode(this.autopilot, this.customMode, this.baseMode).label;
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
    } else if (message.msgId === 44) {
      this.missionCount = num(f.count);
    } else if (message.msgId === 22) {
      const name = str(f.paramId);
      const value = num(f.paramValue);
      const type = num(f.paramType);
      if (name && value !== null) {
        this.parameters.set(name, {
          name,
          value,
          type: mavParamTypeLabel(type),
          readonly: false,
          index: num(f.paramIndex),
          count: num(f.paramCount),
          updatedAtS: performanceNowS()
        });
      }
    } else if (message.msgId === 39 || message.msgId === 73) {
      const seq = num(f.seq);
      const command = num(f.command);
      if (seq !== null && command !== null) {
        const latDeg = message.msgId === 73 ? (num(f.xLatE7) ?? 0) / 1e7 : num(f.x);
        const lonDeg = message.msgId === 73 ? (num(f.yLonE7) ?? 0) / 1e7 : num(f.y);
        const altitudeM = message.msgId === 73 ? num(f.zAltM) : num(f.z);
        if (latDeg !== null && lonDeg !== null) {
          this.missionItems.set(seq, {
            seq,
            latDeg,
            lonDeg,
            altitudeM,
            command: command === MAV_CMD.NAV_WAYPOINT ? 'WAYPOINT' : `MAV_CMD ${command}`,
            frame: f.frame === null || f.frame === undefined ? null : `MAV_FRAME ${f.frame}`
          });
        }
      }
    } else if (message.msgId === 47) {
      this.missionAckType = num(f.type);
    } else if (message.msgId === 118) {
      const id = num(f.id);
      const sizeBytes = num(f.sizeBytes);
      if (id !== null && sizeBytes !== null) {
        this.logs.set(id, {
          id,
          numLogs: num(f.numLogs),
          lastLogNum: num(f.lastLogNum),
          timeUtc: num(f.timeUtc),
          sizeBytes
        });
      }
    } else if (message.msgId === 136) {
      this.terrain = {
        latDeg: num(f.latDeg),
        lonDeg: num(f.lonDeg),
        spacingM: num(f.spacingM),
        terrainHeightM: num(f.terrainHeightM),
        currentHeightM: num(f.currentHeightM),
        pending: num(f.pending),
        loaded: num(f.loaded)
      };
    } else if (message.msgId === 147) {
      this.batteryRemainingPct = num(f.batteryRemainingPct);
      this.batteryVoltageV = num(f.voltageBatteryV);
    } else if (message.msgId === 242) {
      const latDeg = num(f.latDeg);
      const lonDeg = num(f.lonDeg);
      const altitudeM = num(f.altitudeM);
      if (latDeg !== null && lonDeg !== null && altitudeM !== null) {
        this.home = { latDeg, lonDeg, altitudeM };
      }
    } else if (message.msgId === 253) {
      this.lastStatusText = str(f.text);
    } else if (message.msgId === 259) {
      const vendorName = str(f.vendorName);
      const modelName = str(f.modelName);
      const resolutionH = num(f.resolutionH);
      const resolutionV = num(f.resolutionV);
      this.camera = {
        id: `${message.systemId}-${message.componentId}-camera`,
        label: [vendorName, modelName].filter(Boolean).join(' ') || 'MAVLink camera',
        kind: 'mock',
        uri: null,
        status: 'available',
        captureSupported: true,
        recordingSupported: true,
        telemetrySubtitleSupported: true,
        vendorName,
        modelName,
        resolution: resolutionH && resolutionV ? `${resolutionH}x${resolutionV}` : null,
        zoomLevel: this.camera?.zoomLevel ?? null,
        focusLevel: this.camera?.focusLevel ?? null,
        storageFreeMb: this.camera?.storageFreeMb ?? null
      };
    } else if (message.msgId === 260 || message.msgId === 262) {
      const base = this.camera ?? {
        id: `${message.systemId}-${message.componentId}-camera`,
        label: 'MAVLink camera',
        kind: 'mock' as const,
        uri: null,
        status: 'available' as const,
        captureSupported: true,
        recordingSupported: true,
        telemetrySubtitleSupported: true
      };
      this.camera = {
        ...base,
        status: num(f.videoStatus) === 1 ? 'recording' : base.status,
        zoomLevel: num(f.zoomLevel) ?? base.zoomLevel ?? null,
        focusLevel: num(f.focusLevel) ?? base.focusLevel ?? null,
        storageFreeMb: num(f.availableCapacityMb) ?? base.storageFreeMb ?? null
      };
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

function mavParamTypeLabel(paramType: number | null): ParameterValue['type'] {
  switch (paramType) {
    case 1:
    case 2:
      return 'int';
    case 3:
    case 4:
      return 'int';
    case 5:
    case 6:
      return 'int';
    case 7:
      return 'int';
    case 8:
    case 9:
      return 'float';
    default:
      return 'float';
  }
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
  private lastRemote: Endpoint | null = null;
  private txSeq = 1;
  private commandTransactionSeq = 1;
  private operationSeq = 1;
  private readonly commandTransactions: CommandTransaction[] = [];
  private readonly commandAudit: CommandAuditEntry[] = [];
  private readonly protocolOperations: ProtocolOperation[] = [];
  private readonly operationAudit: OperationAuditEntry[] = [];
  private readonly logDownloadBuffers = new Map<string, Buffer[]>();
  private readonly protocolTracker = new MavlinkProtocolTracker();
  private readonly sessionContext: AnimusSessionContext;

  constructor(config: Partial<MavlinkServiceConfig> = {}, sessionContext: Partial<AnimusSessionContext> = {}) {
    super();
    this.config = normalizeConfig(config);
    this.sessionContext = {
      sessionId: sessionContext.sessionId ?? `animus-${Date.now().toString(36)}`,
      operatorId: sessionContext.operatorId ?? process.env.USER ?? process.env.USERNAME ?? 'unknown',
      appSource: sessionContext.appSource ?? 'animus-electron',
      processSource: sessionContext.processSource ?? `pid:${process.pid}`
    };
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

  linkState(): WritableLinkState {
    const selected = this.selectedVehiclePayload();
    return {
      liveLink: selected.connected,
      writable: this.config.writableAnimus,
      transport: 'udp',
      mode: this.config.authorityMode,
      blockedReason: defaultCommandCapabilities(selected.connected, this.config.writableAnimus, {
        packetAgeS: selected.packetAgeS,
        readiness: selected.status?.readiness,
        authority: this.config.authorityMode
      }).blockedReason
    };
  }

  handlePacket(packet: Buffer, remote?: RemoteInfo): VehicleStatePayload {
    if (remote) {
      this.lastRemote = { host: remote.address, port: remote.port };
    }
    this.forwarder.forward(packet);
    this.protocolTracker.observe(packet);
    const nowS = performanceNowS();
    this.expireCommandTransactionsForSnapshot(nowS);
    for (const message of this.parser.feed(packet)) {
      this.state.apply(message, nowS);
      this.registry.apply(message, nowS);
      this.applyCommandAck(message, nowS);
      this.applyProtocolMessage(message, nowS);
    }
    this.expireCommandTransactionsForSnapshot(nowS);
    const payload = this.registry.selectedVehicle(nowS);
    const snapshot = this.snapshot(nowS);
    this.applyLinkDiagnostics(payload);
    this.applyWritableCapabilities(payload);
    this.emit('vehicle-state', payload);
    this.emit('session-snapshot', snapshot);
    return payload;
  }

  dispatchCommand(request: GuardedCommandRequest): CommandDispatchResult {
    const vehicle = this.selectedVehiclePayload();
    const target = parseVehicleTarget(request.vehicleId, vehicle);
    const command = commandSpec(request.command, request.params);
    const blocked = this.writeBlocked(request.confirmed, target, request.command);
    if (blocked) {
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'dispatch-blocked',
        request,
        transaction: null,
        commandId: command?.command ?? null,
        params: command?.params ?? [],
        accepted: false,
        state: 'blocked',
        reason: blocked
      }));
      return { accepted: false, command: request.command, vehicleId: request.vehicleId, reason: blocked, mock: false, sentPackets: 0, transactionId: null, state: 'blocked', ack: null };
    }
    if (!target) {
      const reason = 'no live vehicle link advertises command support';
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'dispatch-blocked',
        request,
        transaction: null,
        commandId: command?.command ?? null,
        params: command?.params ?? [],
        accepted: false,
        state: 'blocked',
        reason
      }));
      return { accepted: false, command: request.command, vehicleId: request.vehicleId, reason, mock: false, sentPackets: 0, transactionId: null, state: 'blocked', ack: null };
    }
    if (!command) {
      const reason = `${request.command} is not supported by the SITL dispatcher`;
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'dispatch-blocked',
        request,
        transaction: null,
        commandId: null,
        params: [],
        accepted: false,
        state: 'blocked',
        reason
      }));
      return { accepted: false, command: request.command, vehicleId: request.vehicleId, reason, mock: false, sentPackets: 0, transactionId: null, state: 'blocked', ack: null };
    }
    const typedConfirmationReason = typedConfirmationBlocked(request, command.params);
    if (typedConfirmationReason) {
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'dispatch-blocked',
        request,
        transaction: null,
        commandId: command.command,
        params: command.params,
        accepted: false,
        state: 'blocked',
        reason: typedConfirmationReason
      }));
      return { accepted: false, command: request.command, vehicleId: request.vehicleId, reason: typedConfirmationReason, mock: false, sentPackets: 0, transactionId: null, state: 'blocked', ack: null };
    }
    const nowS = performanceNowS();
    const transaction = this.createCommandTransaction(request, command.command, command.params, nowS);
    const frame = encodeCommandLong(command.command, target.systemId, target.componentId, command.params, 0, this.nextSeq());
    try {
      this.sendFrame(frame);
    } catch (error) {
      const reason = error instanceof Error ? error.message : String(error);
      transaction.state = 'failed';
      transaction.updatedAtS = nowS;
      transaction.failureReason = reason;
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'send-failed',
        request,
        transaction,
        commandId: command.command,
        params: command.params,
        accepted: false,
        state: 'failed',
        reason
      }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: false, command: request.command, vehicleId: request.vehicleId, reason, mock: false, sentPackets: 0, transactionId: transaction.id, state: transaction.state, ack: null };
    }
    this.registry.addMarker(`Command ${request.command} sent`, nowS, request.vehicleId);
    this.pushCommandAudit(this.createCommandAuditEntry({
      eventKind: 'dispatch-sent',
      request,
      transaction,
      commandId: command.command,
      params: command.params,
      accepted: true,
      state: transaction.state,
      reason: 'command sent on writable SITL link'
    }));
    this.emit('session-snapshot', this.snapshot(nowS));
    return { accepted: true, command: request.command, vehicleId: request.vehicleId, reason: 'command sent on writable SITL link', mock: false, sentPackets: 1, transactionId: transaction.id, state: transaction.state, ack: null };
  }

  retryCommandTransaction(transactionId: string): CommandDispatchResult | null {
    const nowS = performanceNowS();
    const transaction = this.commandTransactions.find((candidate) => candidate.id === transactionId);
    if (!transaction) {
      return null;
    }
    const request: GuardedCommandRequest = {
      command: transaction.commandName,
      vehicleId: transaction.vehicleId,
      confirmed: true,
      confirmationType: transaction.confirmationType,
      confirmationResult: 'accepted',
      originSurface: 'setup-command-history'
    };
    if (!isRetryableCommandState(transaction.state)) {
      const reason = `${transaction.state} command transactions cannot be retried`;
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'retry-blocked',
        request,
        transaction,
        commandId: transaction.commandId,
        params: transaction.params,
        accepted: false,
        state: transaction.state,
        reason
      }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: false, command: transaction.commandName, vehicleId: transaction.vehicleId, reason, mock: false, sentPackets: 0, transactionId: transaction.id, state: transaction.state, ack: transaction.ack ? { ...transaction.ack } : null };
    }
    const target = parseVehicleTarget(transaction.vehicleId, this.selectedVehiclePayload());
    const blocked = this.writeBlocked(true, target, transaction.commandName);
    if (blocked) {
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'retry-blocked',
        request,
        transaction,
        commandId: transaction.commandId,
        params: transaction.params,
        accepted: false,
        state: transaction.state,
        reason: blocked
      }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: false, command: transaction.commandName, vehicleId: transaction.vehicleId, reason: blocked, mock: false, sentPackets: 0, transactionId: transaction.id, state: transaction.state, ack: transaction.ack ? { ...transaction.ack } : null };
    }
    if (!target) {
      const reason = 'no live vehicle link advertises command support';
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'retry-blocked',
        request,
        transaction,
        commandId: transaction.commandId,
        params: transaction.params,
        accepted: false,
        state: transaction.state,
        reason
      }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: false, command: transaction.commandName, vehicleId: transaction.vehicleId, reason, mock: false, sentPackets: 0, transactionId: transaction.id, state: transaction.state, ack: transaction.ack ? { ...transaction.ack } : null };
    }
    transaction.retryCount += 1;
    transaction.sentAtS = nowS;
    transaction.updatedAtS = nowS;
    transaction.state = 'sent';
    transaction.ack = null;
    transaction.failureReason = null;
    const frame = encodeCommandLong(transaction.commandId, target.systemId, target.componentId, transaction.params, transaction.retryCount, this.nextSeq());
    try {
      this.sendFrame(frame);
    } catch (error) {
      const reason = error instanceof Error ? error.message : String(error);
      transaction.state = 'failed';
      transaction.updatedAtS = nowS;
      transaction.failureReason = reason;
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'send-failed',
        request,
        transaction,
        commandId: transaction.commandId,
        params: transaction.params,
        accepted: false,
        state: 'failed',
        reason
      }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: false, command: transaction.commandName, vehicleId: transaction.vehicleId, reason, mock: false, sentPackets: 0, transactionId: transaction.id, state: transaction.state, ack: null };
    }
    this.registry.addMarker(`Command ${transaction.commandName} retry ${transaction.retryCount} sent`, nowS, transaction.vehicleId);
    this.pushCommandAudit(this.createCommandAuditEntry({
      eventKind: 'retry-sent',
      request,
      transaction,
      commandId: transaction.commandId,
      params: transaction.params,
      accepted: true,
      state: 'sent',
      reason: 'command retry sent on writable SITL link'
    }));
    this.emit('session-snapshot', this.snapshot(nowS));
    return { accepted: true, command: transaction.commandName, vehicleId: transaction.vehicleId, reason: 'command retry sent on writable SITL link', mock: false, sentPackets: 1, transactionId: transaction.id, state: transaction.state, ack: null };
  }

  expireCommandTransactions(nowS = performanceNowS()): void {
    const changed = this.expireCommandTransactionsForSnapshot(nowS);
    if (changed) {
      this.emit('session-snapshot', this.snapshot(nowS));
    }
  }

  cancelCommandTransaction(transactionId: string, reason = 'operator cancelled pending command'): CommandTransaction | null {
    const nowS = performanceNowS();
    const transaction = this.commandTransactions.find((candidate) => candidate.id === transactionId);
    if (!transaction || transaction.state !== 'sent') {
      return transaction ? { ...transaction, params: [...transaction.params], ack: transaction.ack ? { ...transaction.ack } : null, cancellationEligible: false } : null;
    }
    transaction.state = 'cancelled';
    transaction.updatedAtS = nowS;
    transaction.failureReason = reason;
    this.registry.addMarker(`Command ${transaction.commandName} cancelled`, nowS, transaction.vehicleId);
    this.pushCommandAudit(this.createCommandAuditEntry({
      eventKind: 'cancelled',
      request: {
        command: transaction.commandName,
        vehicleId: transaction.vehicleId,
        confirmed: true,
        confirmationType: transaction.confirmationType
      },
      transaction,
      commandId: transaction.commandId,
      params: transaction.params,
      accepted: false,
      state: 'cancelled',
      reason
    }));
    this.emit('session-snapshot', this.snapshot(nowS));
    return { ...transaction, params: [...transaction.params], ack: transaction.ack ? { ...transaction.ack } : null, cancellationEligible: false };
  }

  refreshParameters(vehicleId = this.registry.selectedVehicleId ?? '', originSurface = 'setup-parameters'): ParameterEditResult {
    return this.sendSimpleOperation({
      domain: 'parameters',
      action: 'refresh',
      vehicleId,
      confirmed: true,
      originSurface,
      frames: (target) => [encodeParamRequestList(target.systemId, target.componentId, this.nextSeq())],
      resultSummary: 'parameter list refresh requested'
    });
  }

  setParameter(request: ParameterEditRequest): ParameterEditResult {
    const numericValue = typeof request.value === 'boolean' ? (request.value ? 1 : 0) : Number(request.value);
    if (!Number.isFinite(numericValue) || request.name.trim().length === 0 || request.name.length > 16) {
      return { accepted: false, vehicleId: request.vehicleId, name: request.name, state: 'rejected', reason: 'parameter edit requires a numeric value and a MAVLink parameter name up to 16 characters', operationId: null };
    }
    const result = this.sendSimpleOperation({
      domain: 'parameters',
      action: 'set',
      vehicleId: request.vehicleId,
      confirmed: request.confirmed,
      originSurface: request.originSurface ?? 'setup-parameters',
      confirmationType: request.confirmationType,
      payload: { name: request.name, value: numericValue, paramType: request.paramType ?? 9 },
      frames: (target) => [encodeParamSet(request.name, numericValue, request.paramType ?? 9, target.systemId, target.componentId, this.nextSeq())],
      resultSummary: `parameter ${request.name} set requested`
    });
    return { ...result, name: request.name };
  }

  uploadMission(plan: MissionPlan, vehicleId = this.registry.selectedVehicleId ?? '', confirmed = true, originSurface = 'plan-mission'): MissionTransferState {
    const validation = validateMission(plan);
    if (!validation.valid) {
      return { accepted: false, direction: 'upload', vehicleId, waypointCount: plan.waypoints.length, state: 'rejected', reason: validation.issues.map((issue) => issue.message).join('; '), sentPackets: 0, validation, operationId: null, plan: null };
    }
    const result = this.sendSimpleOperation({
      domain: 'mission',
      action: 'upload',
      vehicleId,
      confirmed,
      originSurface,
      payload: { waypointCount: plan.waypoints.length },
      frames: (target) => [
        encodeMissionClearAll(target.systemId, target.componentId, this.nextSeq()),
        encodeMissionCount(plan.waypoints.length, target.systemId, target.componentId, this.nextSeq()),
        ...plan.waypoints.map((waypoint) => encodeMissionItemInt(waypoint, target.systemId, target.componentId, this.nextSeq()))
      ],
      resultSummary: `mission upload started (${plan.waypoints.length} waypoints)`
    });
    return { accepted: result.accepted, direction: 'upload', vehicleId, waypointCount: plan.waypoints.length, state: result.state, reason: result.reason, sentPackets: this.protocolOperations.find((op) => op.id === result.operationId)?.sentPackets ?? 0, validation, operationId: result.operationId, plan: null };
  }

  downloadMission(vehicleId = this.registry.selectedVehicleId ?? '', confirmed = true, originSurface = 'plan-mission'): MissionTransferState {
    const validation = validateMission({ schemaVersion: 1, source: 'bayek-v1', waypoints: [] });
    const result = this.sendSimpleOperation({
      domain: 'mission',
      action: 'download',
      vehicleId,
      confirmed,
      originSurface,
      frames: (target) => [encodeMissionRequestList(target.systemId, target.componentId, this.nextSeq())],
      resultSummary: 'mission download requested'
    });
    const plan = this.missionPlanFromVehicle(vehicleId);
    return { accepted: result.accepted, direction: 'download', vehicleId, waypointCount: plan?.waypoints.length ?? 0, state: result.state, reason: result.reason, sentPackets: this.protocolOperations.find((op) => op.id === result.operationId)?.sentPackets ?? 0, validation, operationId: result.operationId, plan };
  }

  clearMission(vehicleId = this.registry.selectedVehicleId ?? '', confirmed = true, originSurface = 'plan-mission'): MissionTransferState {
    const validation = validateMission({ schemaVersion: 1, source: 'bayek-v1', waypoints: [] });
    const result = this.sendSimpleOperation({
      domain: 'mission',
      action: 'clear',
      vehicleId,
      confirmed,
      originSurface,
      confirmationType: 'typed-operation',
      frames: (target) => [encodeMissionClearAll(target.systemId, target.componentId, this.nextSeq())],
      resultSummary: 'mission clear requested'
    });
    return { accepted: result.accepted, direction: 'clear', vehicleId, waypointCount: 0, state: result.state, reason: result.reason, sentPackets: this.protocolOperations.find((op) => op.id === result.operationId)?.sentPackets ?? 0, validation, operationId: result.operationId, plan: null };
  }

  listLogs(vehicleId = this.registry.selectedVehicleId ?? '', originSurface = 'logs'): ParameterEditResult {
    return this.sendSimpleOperation({
      domain: 'logs',
      action: 'list',
      vehicleId,
      confirmed: true,
      originSurface,
      frames: (target) => [encodeLogRequestList(target.systemId, target.componentId, 0, 0xffff, this.nextSeq())],
      resultSummary: 'onboard log list requested'
    });
  }

  downloadLog(logId: number, vehicleId = this.registry.selectedVehicleId ?? '', originSurface = 'logs'): ParameterEditResult {
    const result = this.sendSimpleOperation({
      domain: 'logs',
      action: 'download',
      vehicleId,
      confirmed: true,
      originSurface,
      payload: { logId },
      frames: (target) => [encodeLogRequestData(logId, 0, 900, target.systemId, target.componentId, this.nextSeq())],
      resultSummary: `log ${logId} download requested`
    });
    if (result.operationId) this.logDownloadBuffers.set(result.operationId, []);
    return result;
  }

  eraseLogs(vehicleId = this.registry.selectedVehicleId ?? '', confirmed = true, originSurface = 'logs'): ParameterEditResult {
    return this.sendSimpleOperation({
      domain: 'logs',
      action: 'erase',
      vehicleId,
      confirmed,
      originSurface,
      confirmationType: 'typed-operation',
      frames: (target) => [encodeLogErase(target.systemId, target.componentId, this.nextSeq())],
      resultSummary: 'onboard log erase requested'
    });
  }

  requestTerrain(vehicleId = this.registry.selectedVehicleId ?? '', originSurface = 'map-terrain'): ParameterEditResult {
    const selected = this.selectedVehiclePayload();
    const latDeg = selected.globalPosition.latDeg ?? selected.home?.latDeg ?? 0;
    const lonDeg = selected.globalPosition.lonDeg ?? selected.home?.lonDeg ?? 0;
    return this.sendSimpleOperation({
      domain: 'terrain',
      action: 'check',
      vehicleId,
      confirmed: true,
      originSurface,
      payload: { latDeg, lonDeg },
      frames: () => [encodeTerrainCheck(latDeg, lonDeg, this.nextSeq())],
      resultSummary: 'terrain check requested'
    });
  }

  cameraAction(action: 'capture' | 'record-start' | 'record-stop' | 'zoom' | 'focus', vehicleId = this.registry.selectedVehicleId ?? '', value?: number, originSurface = 'video-camera'): ParameterEditResult {
    const command = action === 'capture'
      ? MAV_CMD.IMAGE_START_CAPTURE
      : action === 'record-start'
        ? MAV_CMD.VIDEO_START_CAPTURE
        : action === 'record-stop'
          ? MAV_CMD.VIDEO_STOP_CAPTURE
          : action === 'zoom'
            ? MAV_CMD.SET_CAMERA_ZOOM
            : MAV_CMD.SET_CAMERA_FOCUS;
    const params = action === 'zoom' || action === 'focus' ? [0, value ?? 0] : action === 'capture' ? [0, 0, 1] : [];
    return this.sendSimpleOperation({
      domain: 'camera',
      action,
      vehicleId,
      confirmed: true,
      originSurface,
      payload: { value },
      frames: (target) => [encodeCommandLong(command, target.systemId, target.componentId, params, 0, this.nextSeq())],
      resultSummary: `camera ${action} command sent`
    });
  }

  uploadMissionToSitl(plan: MissionPlan, vehicleId = this.registry.selectedVehicleId ?? ''): MissionTransferState {
    return this.uploadMission(plan, vehicleId, true, 'plan-mission-legacy');
  }

  downloadMissionFromSitl(vehicleId = this.registry.selectedVehicleId ?? ''): MissionTransferState {
    return this.downloadMission(vehicleId, true, 'plan-mission-legacy');
  }

  selectVehicle(id: string): SessionSnapshotPayload {
    this.registry.select(id);
    const snapshot = this.snapshot();
    this.emit('session-snapshot', snapshot);
    const selected = this.selectedVehiclePayload();
    this.emit('vehicle-state', selected);
    return snapshot;
  }

  addMarker(label: string): SessionSnapshotPayload {
    this.registry.addMarker(label);
    const snapshot = this.snapshot();
    this.emit('session-snapshot', snapshot);
    return snapshot;
  }

  cancelOperation(operationId: string, reason = 'operator cancelled protocol operation'): ProtocolOperation | null {
    const nowS = performanceNowS();
    const operation = this.protocolOperations.find((candidate) => candidate.id === operationId);
    if (!operation || (operation.state !== 'waiting' && operation.state !== 'waiting-ack' && operation.state !== 'receiving' && operation.state !== 'sending')) {
      return operation ? { ...operation } : null;
    }
    operation.state = 'cancelled';
    operation.updatedAtS = nowS;
    operation.failureReason = reason;
    operation.resultSummary = reason;
    this.pushOperationAudit(this.createOperationAuditEntry({ eventKind: 'operation-cancelled', operation, accepted: false, reason }));
    this.emit('session-snapshot', this.snapshot(nowS));
    return { ...operation };
  }

  expireProtocolOperations(nowS = performanceNowS()): void {
    const changed = this.expireProtocolOperationsForSnapshot(nowS);
    if (changed) this.emit('session-snapshot', this.snapshot(nowS));
  }

  private sendSimpleOperation(options: {
    domain: ProtocolOperationDomain;
    action: string;
    vehicleId: string;
    confirmed: boolean;
    originSurface?: string;
    confirmationType?: OperationAuditEntry['confirmationType'];
    payload?: Record<string, unknown>;
    frames: (target: { systemId: number; componentId: number }) => Buffer[];
    resultSummary: string;
  }): ParameterEditResult {
    const vehicle = this.selectedVehiclePayload();
    const target = parseVehicleTarget(options.vehicleId, vehicle);
    const blocked = this.writeBlocked(options.confirmed, target);
    if (blocked || !target) {
      const reason = blocked ?? 'no live vehicle link advertises protocol operation support';
      this.pushOperationAudit(this.createOperationAuditEntry({
        eventKind: 'operation-blocked',
        operation: null,
        accepted: false,
        reason,
        domain: options.domain,
        action: options.action,
        vehicleId: options.vehicleId,
        payload: options.payload ?? {},
        confirmationType: options.confirmationType,
        originSurface: options.originSurface,
        confirmed: options.confirmed
      }));
      return { accepted: false, vehicleId: options.vehicleId, state: 'blocked', reason, operationId: null };
    }
    const nowS = performanceNowS();
    const operation = this.createProtocolOperation(options.domain, options.action, options.vehicleId, nowS, options.payload);
    try {
      const frames = options.frames(target);
      for (const frame of frames) this.sendFrame(frame);
      operation.sentPackets += frames.length;
      operation.state = operation.domain === 'logs' && operation.action === 'download' ? 'receiving' : 'waiting-ack';
      operation.resultSummary = options.resultSummary;
      operation.updatedAtS = nowS;
      this.registry.addMarker(options.resultSummary, nowS, options.vehicleId);
      this.pushOperationAudit(this.createOperationAuditEntry({
        eventKind: 'operation-sent',
        operation,
        accepted: true,
        reason: options.resultSummary,
        confirmationType: options.confirmationType,
        originSurface: options.originSurface
      }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: true, vehicleId: options.vehicleId, state: operation.state, reason: options.resultSummary, operationId: operation.id };
    } catch (error) {
      const reason = error instanceof Error ? error.message : String(error);
      operation.state = 'failed';
      operation.failureReason = reason;
      operation.resultSummary = reason;
      operation.updatedAtS = nowS;
      this.pushOperationAudit(this.createOperationAuditEntry({ eventKind: 'operation-failed', operation, accepted: false, reason }));
      this.emit('session-snapshot', this.snapshot(nowS));
      return { accepted: false, vehicleId: options.vehicleId, state: 'failed', reason, operationId: operation.id };
    }
  }

  private createProtocolOperation(domain: ProtocolOperationDomain, action: string, vehicleId: string, nowS: number, payload: Record<string, unknown> = {}): ProtocolOperation {
    const operation: ProtocolOperation = {
      id: `op-${Math.round(nowS * 1000)}-${this.operationSeq++}`,
      domain,
      action,
      vehicleId,
      state: 'sending',
      createdAtS: nowS,
      updatedAtS: nowS,
      deadlineS: nowS + PROTOCOL_OPERATION_TIMEOUT_S,
      retryCount: 0,
      timeoutS: PROTOCOL_OPERATION_TIMEOUT_S,
      sentPackets: 0,
      receivedPackets: 0,
      progressPct: null,
      resultSummary: null,
      failureReason: null,
      payload
    };
    this.protocolOperations.push(operation);
    if (this.protocolOperations.length > MAX_PROTOCOL_OPERATIONS) {
      this.protocolOperations.splice(0, this.protocolOperations.length - MAX_PROTOCOL_OPERATIONS);
    }
    return operation;
  }

  private missionPlanFromVehicle(vehicleId: string): MissionPlan | null {
    const snapshot = this.registry.snapshot();
    const vehicle = snapshot.vehicles.find((candidate) => candidate.id === vehicleId) ?? snapshot.vehicles[0];
    const waypoints = vehicle?.mission?.waypoints;
    if (!waypoints || waypoints.length === 0) return null;
    return {
      schemaVersion: 1,
      source: 'bayek-v1',
      waypoints: waypoints.map((waypoint, index) => ({
        seq: index,
        lat_deg: waypoint.latDeg,
        lon_deg: waypoint.lonDeg,
        alt_m: waypoint.altitudeM ?? 0,
        throttle: 0.5,
        acceptance_radius_m: 25
      }))
    };
  }

  private activeForwardTargets(): Endpoint[] {
    return this.config.qgcForwarding ? this.config.qgcEndpoints : [];
  }

  private nextSeq(): number {
    const seq = this.txSeq;
    this.txSeq = (this.txSeq + 1) & 0xff;
    return seq;
  }

  private sendFrame(frame: Buffer): void {
    if (!this.socket || !this.lastRemote) {
      throw new Error('no live MAVLink remote endpoint is available');
    }
    this.socket.send(frame, this.lastRemote.port, this.lastRemote.host);
  }

  private applyWritableCapabilities(payload: VehicleStatePayload): void {
    payload.commandCapabilities = defaultCommandCapabilities(payload.connected, this.config.writableAnimus, {
      packetAgeS: payload.packetAgeS,
      readiness: payload.status?.readiness,
      authority: this.config.authorityMode,
      qgcForwarding: this.config.qgcForwarding,
      selectedWritableEndpoint: this.lastRemote ? `${this.lastRemote.host}:${this.lastRemote.port}` : null
    });
  }

  private selectedVehiclePayload(nowS = performanceNowS()): VehicleStatePayload {
    const selected = this.registry.selectedVehicle(nowS);
    this.applyLinkDiagnostics(selected);
    this.applyWritableCapabilities(selected);
    return selected;
  }

  private applyLinkDiagnostics(payload: VehicleStatePayload): void {
    const selectedWritableEndpoint = this.lastRemote ? `${this.lastRemote.host}:${this.lastRemote.port}` : null;
    const protocol = this.protocolTracker.snapshot();
    payload.diagnostics = {
      ...(payload.diagnostics ?? {
        linkId: `${payload.systemId ?? 'unknown'}:${payload.componentId ?? 'unknown'}`,
        transport: 'udp',
        status: payload.connected ? 'connected' : 'degraded',
        packetsRx: 0,
        packetsTx: 0,
        decodedRx: 0,
        drops: 0,
        lastError: null
      }),
      protocol,
      qgcForwarding: this.config.qgcForwarding,
      selectedWritableEndpoint,
      duplicateGcsForwardingRisk: Boolean(this.config.writableAnimus && this.config.qgcForwarding && selectedWritableEndpoint)
    };
    if (payload.status) {
      const readiness = buildVehicleReadiness({
        connected: payload.connected,
        packetAgeS: payload.packetAgeS,
        status: payload.status,
        diagnostics: payload.diagnostics
      });
      payload.status.readiness = readiness;
      payload.status.armingState = normalizeArmingState(payload.status.armed, readiness);
    }
  }

  private snapshot(nowS = performanceNowS()): SessionSnapshotPayload {
    this.expireCommandTransactionsForSnapshot(nowS);
    this.expireProtocolOperationsForSnapshot(nowS);
    const snapshot = this.registry.snapshot(nowS);
    for (const vehicle of snapshot.vehicles) {
      this.applyLinkDiagnostics(vehicle);
      this.applyWritableCapabilities(vehicle);
    }
    snapshot.commandTransactions = this.commandTransactions.slice(-MAX_COMMAND_TRANSACTIONS).reverse().map((transaction) => ({
      ...transaction,
      params: [...transaction.params],
      ack: transaction.ack ? { ...transaction.ack } : null,
      cancellationEligible: transaction.state === 'sent',
      retryEligible: isRetryableCommandState(transaction.state)
    }));
    snapshot.commandAudit = this.commandAudit.slice(-MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES).reverse().map((entry) => ({
      ...entry,
      params: [...entry.params],
      ack: entry.ack ? { ...entry.ack } : null
    }));
    snapshot.protocolOperations = this.protocolOperations.slice(-MAX_PROTOCOL_OPERATIONS).reverse().map((operation) => ({
      ...operation,
      payload: { ...(operation.payload ?? {}) },
      cancellationEligible: operation.state === 'waiting' || operation.state === 'waiting-ack' || operation.state === 'receiving',
      retryEligible: operation.state === 'timeout' || operation.state === 'failed'
    }));
    snapshot.operationAudit = this.operationAudit.slice(-MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES).reverse().map((entry) => ({
      ...entry,
      payload: { ...entry.payload }
    }));
    return snapshot;
  }

  private createCommandTransaction(request: GuardedCommandRequest, commandId: number, params: number[], nowS: number): CommandTransaction {
    const transaction: CommandTransaction = {
      id: `cmd-${Math.round(nowS * 1000)}-${this.commandTransactionSeq++}`,
      vehicleId: request.vehicleId,
      commandName: request.command,
      commandId,
      params: [...params],
      confirmationType: request.confirmationType ?? 'browser-confirm',
      createdAtS: nowS,
      sentAtS: nowS,
      updatedAtS: nowS,
      state: 'sent',
      ack: null,
      retryCount: 0,
      failureReason: null
    };
    this.commandTransactions.push(transaction);
    if (this.commandTransactions.length > MAX_COMMAND_TRANSACTIONS) {
      this.commandTransactions.splice(0, this.commandTransactions.length - MAX_COMMAND_TRANSACTIONS);
    }
    return transaction;
  }

  private applyCommandAck(message: MavlinkMessage, nowS: number): void {
    const ack = decodeCommandAck(message);
    if (!ack) {
      return;
    }
    const id = vehicleId(message.systemId, message.componentId);
    const transaction = [...this.commandTransactions].reverse().find((candidate) => candidate.vehicleId === id && candidate.commandId === ack.command && candidate.state === 'sent');
    if (!transaction) {
      return;
    }
    transaction.ack = ack;
    transaction.updatedAtS = nowS;
    transaction.state = ack.result === 0 ? 'acknowledged' : 'failed';
    transaction.failureReason = ack.result === 0 ? null : ack.label;
    this.registry.addMarker(`Command ${transaction.commandName} ${transaction.state}: ${ack.label}`, nowS, transaction.vehicleId);
    this.pushCommandAudit(this.createCommandAuditEntry({
      eventKind: 'ack',
      request: {
        command: transaction.commandName,
        vehicleId: transaction.vehicleId,
        confirmed: true,
        confirmationType: transaction.confirmationType
      },
      transaction,
      commandId: transaction.commandId,
      params: transaction.params,
      accepted: ack.result === 0,
      state: transaction.state,
      reason: ack.result === 0 ? 'COMMAND_ACK accepted' : ack.label,
      ack
    }));
  }

  private applyProtocolMessage(message: MavlinkMessage, nowS: number): void {
    const id = vehicleId(message.systemId, message.componentId);
    const decoded = decodeMavlinkMessage(message);
    const activeFor = (domain: ProtocolOperationDomain, actions?: string[]): ProtocolOperation | undefined =>
      [...this.protocolOperations].reverse().find((operation) =>
        operation.vehicleId === id &&
        operation.domain === domain &&
        (actions ? actions.includes(operation.action) : true) &&
        (operation.state === 'waiting' || operation.state === 'waiting-ack' || operation.state === 'receiving' || operation.state === 'sending'));
    const complete = (operation: ProtocolOperation, summary: string): void => {
      operation.receivedPackets += 1;
      operation.updatedAtS = nowS;
      operation.state = 'complete';
      operation.progressPct = 100;
      operation.resultSummary = summary;
      this.pushOperationAudit(this.createOperationAuditEntry({ eventKind: 'operation-complete', operation, accepted: true, reason: summary }));
    };
    if (message.msgId === 22) {
      const operation = activeFor('parameters', ['refresh', 'set']);
      if (operation) {
        operation.receivedPackets += 1;
        operation.updatedAtS = nowS;
        const count = num(decoded.fields.paramCount);
        const index = num(decoded.fields.paramIndex);
        operation.progressPct = count && index !== null ? Math.min(100, ((index + 1) / count) * 100) : operation.progressPct;
        const targetName = typeof operation.payload?.name === 'string' ? operation.payload.name : null;
        if (operation.action === 'set' && decoded.fields.paramId === targetName) {
          complete(operation, `parameter ${targetName} confirmed`);
        } else if (operation.action === 'refresh' && count !== null && index !== null && index + 1 >= count) {
          complete(operation, `parameter cache refreshed (${count} values)`);
        }
      }
    } else if (message.msgId === 44) {
      const operation = activeFor('mission', ['download']);
      const count = num(decoded.fields.count);
      if (operation && count !== null) {
        operation.receivedPackets += 1;
        operation.updatedAtS = nowS;
        operation.payload = { ...(operation.payload ?? {}), expectedCount: count };
        operation.progressPct = count === 0 ? 100 : 0;
        if (count === 0) complete(operation, 'mission download complete: vehicle mission is empty');
        else {
          try {
            this.sendFrame(encodeMissionRequestInt(0, message.systemId, message.componentId, this.nextSeq()));
            operation.sentPackets += 1;
            operation.state = 'receiving';
            operation.resultSummary = `mission download receiving ${count} item${count === 1 ? '' : 's'}`;
          } catch (error) {
            operation.state = 'failed';
            operation.failureReason = error instanceof Error ? error.message : String(error);
          }
        }
      }
    } else if (message.msgId === 39 || message.msgId === 73) {
      const operation = activeFor('mission', ['download']);
      const expectedCount = typeof operation?.payload?.expectedCount === 'number' ? operation.payload.expectedCount : null;
      const seq = num(decoded.fields.seq);
      if (operation && expectedCount !== null && seq !== null) {
        operation.receivedPackets += 1;
        operation.updatedAtS = nowS;
        operation.progressPct = Math.min(100, ((seq + 1) / expectedCount) * 100);
        if (seq + 1 >= expectedCount) {
          complete(operation, `mission download complete (${expectedCount} items)`);
        } else {
          try {
            this.sendFrame(encodeMissionRequestInt(seq + 1, message.systemId, message.componentId, this.nextSeq()));
            operation.sentPackets += 1;
          } catch (error) {
            operation.state = 'failed';
            operation.failureReason = error instanceof Error ? error.message : String(error);
          }
        }
      }
    } else if (message.msgId === 47) {
      const operation = activeFor('mission', ['upload', 'clear']);
      const ack = decodeMissionAck(message);
      if (operation && ack) {
        if (ack.type === 0) complete(operation, operation.action === 'clear' ? 'mission cleared' : 'mission upload acknowledged');
        else {
          operation.state = 'failed';
          operation.updatedAtS = nowS;
          operation.failureReason = `MISSION_ACK type ${ack.type}`;
          this.pushOperationAudit(this.createOperationAuditEntry({ eventKind: 'operation-failed', operation, accepted: false, reason: operation.failureReason }));
        }
      }
    } else if (message.msgId === 118) {
      const operation = activeFor('logs', ['list']);
      if (operation) {
        operation.receivedPackets += 1;
        operation.updatedAtS = nowS;
        const numLogs = num(decoded.fields.numLogs);
        const lastLogNum = num(decoded.fields.lastLogNum);
        operation.progressPct = numLogs && lastLogNum !== null ? Math.min(100, ((num(decoded.fields.id) ?? 0) + 1) / Math.max(1, lastLogNum + 1) * 100) : operation.progressPct;
        if (numLogs !== null && this.registry.selectedVehicle().logs && this.registry.selectedVehicle().logs!.length >= numLogs) {
          complete(operation, `log list refreshed (${numLogs} logs)`);
        }
      }
    } else if (message.msgId === 120) {
      const operation = activeFor('logs', ['download']);
      if (operation) {
        const count = num(decoded.fields.count) ?? 0;
        operation.receivedPackets += 1;
        operation.updatedAtS = nowS;
        operation.payload = { ...(operation.payload ?? {}), receivedBytes: (Number(operation.payload?.receivedBytes ?? 0) + count) };
        operation.resultSummary = `received ${operation.payload.receivedBytes} log bytes`;
      }
    } else if (message.msgId === 136) {
      const operation = activeFor('terrain', ['check', 'request']);
      if (operation) complete(operation, `terrain report ${decoded.fields.loaded ?? '--'} loaded / ${decoded.fields.pending ?? '--'} pending`);
    } else if (message.msgId === 77) {
      const ack = decodeCommandAck(message);
      const operation = activeFor('camera');
      if (operation && ack && [MAV_CMD.IMAGE_START_CAPTURE, MAV_CMD.VIDEO_START_CAPTURE, MAV_CMD.VIDEO_STOP_CAPTURE, MAV_CMD.SET_CAMERA_ZOOM, MAV_CMD.SET_CAMERA_FOCUS].some((command) => command === ack.command)) {
        if (ack.result === 0) complete(operation, `camera ${operation.action} acknowledged`);
        else {
          operation.state = 'failed';
          operation.updatedAtS = nowS;
          operation.failureReason = ack.label;
          this.pushOperationAudit(this.createOperationAuditEntry({ eventKind: 'operation-failed', operation, accepted: false, reason: ack.label }));
        }
      }
    } else if (message.msgId === 259 || message.msgId === 260 || message.msgId === 262) {
      const operation = activeFor('camera');
      if (operation) complete(operation, `camera status updated from ${decoded.name}`);
    }
  }

  private expireCommandTransactionsForSnapshot(nowS: number): boolean {
    let changed = false;
    for (const transaction of this.commandTransactions) {
      if (transaction.state !== 'sent' || transaction.sentAtS === null || nowS - transaction.sentAtS < COMMAND_TRANSACTION_TIMEOUT_S) {
        continue;
      }
      transaction.state = 'timeout';
      transaction.updatedAtS = nowS;
      transaction.failureReason = `no COMMAND_ACK received within ${COMMAND_TRANSACTION_TIMEOUT_S}s`;
      this.registry.addMarker(`Command ${transaction.commandName} timed out`, nowS, transaction.vehicleId);
      this.pushCommandAudit(this.createCommandAuditEntry({
        eventKind: 'timeout',
        request: {
          command: transaction.commandName,
          vehicleId: transaction.vehicleId,
          confirmed: true,
          confirmationType: transaction.confirmationType
        },
        transaction,
        commandId: transaction.commandId,
        params: transaction.params,
        accepted: false,
        state: 'timeout',
        reason: transaction.failureReason
      }));
      changed = true;
    }
    return changed;
  }

  private expireProtocolOperationsForSnapshot(nowS: number): boolean {
    let changed = false;
    for (const operation of this.protocolOperations) {
      if ((operation.state !== 'waiting' && operation.state !== 'waiting-ack' && operation.state !== 'receiving' && operation.state !== 'sending') || operation.deadlineS === null || nowS < operation.deadlineS) {
        continue;
      }
      operation.state = 'timeout';
      operation.updatedAtS = nowS;
      operation.failureReason = `no ${operation.domain} ${operation.action} response within ${operation.timeoutS}s`;
      operation.resultSummary = operation.failureReason;
      this.registry.addMarker(`${operation.domain} ${operation.action} timed out`, nowS, operation.vehicleId);
      this.pushOperationAudit(this.createOperationAuditEntry({ eventKind: 'operation-timeout', operation, accepted: false, reason: operation.failureReason }));
      changed = true;
    }
    return changed;
  }

  private createCommandAuditEntry(options: {
    eventKind: CommandAuditEntry['eventKind'];
    request: GuardedCommandRequest;
    transaction: CommandTransaction | null;
    commandId: number | null;
    params: number[];
    accepted: boolean;
    state: CommandAuditEntry['state'];
    reason: string;
    ack?: CommandAuditEntry['ack'];
  }): CommandAuditEntry {
    const config = this.getConfig();
    return {
      schemaVersion: 1,
      eventKind: options.eventKind,
      transactionId: options.transaction?.id ?? null,
      sessionId: this.sessionContext.sessionId,
      operatorId: this.sessionContext.operatorId,
      timestamp: new Date().toISOString(),
      vehicleId: options.request.vehicleId,
      commandName: options.request.command,
      commandId: options.commandId,
      params: [...options.params],
      payload: { params: { ...(options.request.params ?? {}) }, encodedParams: [...options.params] },
      confirmationType: options.request.confirmationType ?? 'browser-confirm',
      accepted: options.accepted,
      state: options.state,
      reason: options.reason,
      failureReason: options.accepted ? null : options.reason,
      ack: options.ack ? { ...options.ack } : null,
      authority: config.authorityMode,
      writable: config.writableAnimus,
      retryCount: options.transaction?.retryCount ?? 0,
      appSource: this.sessionContext.appSource,
      processSource: this.sessionContext.processSource,
      commandOrigin: options.request.originSurface ?? 'unknown',
      vehicleTarget: options.request.vehicleId,
      authorityMode: config.authorityMode,
      writableEndpoint: this.lastRemote ? `${this.lastRemote.host}:${this.lastRemote.port}` : null,
      qgcForwarding: config.qgcForwarding,
      protocolSummary: summarizeProtocol(this.protocolTracker.snapshot()),
      confirmationResult: options.request.confirmationResult ?? (options.request.confirmed ? 'accepted' : 'rejected')
    };
  }

  private pushCommandAudit(entry: CommandAuditEntry): void {
    this.commandAudit.push(entry);
    if (this.commandAudit.length > MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES) {
      this.commandAudit.splice(0, this.commandAudit.length - MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES);
    }
    this.emit('command-audit', entry);
  }

  private createOperationAuditEntry(options: {
    eventKind: OperationAuditEntry['eventKind'];
    operation: ProtocolOperation | null;
    accepted: boolean;
    reason: string;
    domain?: ProtocolOperationDomain;
    action?: string;
    vehicleId?: string;
    payload?: Record<string, unknown>;
    confirmationType?: OperationAuditEntry['confirmationType'];
    originSurface?: string;
    confirmed?: boolean;
  }): OperationAuditEntry {
    const config = this.getConfig();
    return {
      schemaVersion: 1,
      eventKind: options.eventKind,
      operationId: options.operation?.id ?? null,
      sessionId: this.sessionContext.sessionId,
      operatorId: this.sessionContext.operatorId,
      timestamp: new Date().toISOString(),
      vehicleId: options.operation?.vehicleId ?? options.vehicleId ?? '',
      domain: options.operation?.domain ?? options.domain ?? 'parameters',
      action: options.operation?.action ?? options.action ?? 'unknown',
      payload: { ...(options.operation?.payload ?? options.payload ?? {}) },
      accepted: options.accepted,
      state: options.operation?.state ?? (options.accepted ? 'complete' : 'blocked'),
      reason: options.reason,
      failureReason: options.accepted ? null : options.reason,
      authority: config.authorityMode,
      writable: config.writableAnimus,
      retryCount: options.operation?.retryCount ?? 0,
      appSource: this.sessionContext.appSource,
      processSource: this.sessionContext.processSource,
      originSurface: options.originSurface,
      authorityMode: config.authorityMode,
      writableEndpoint: this.lastRemote ? `${this.lastRemote.host}:${this.lastRemote.port}` : null,
      qgcForwarding: config.qgcForwarding,
      protocolSummary: summarizeProtocol(this.protocolTracker.snapshot()),
      confirmationType: options.confirmationType,
      confirmationResult: options.confirmed === false ? 'rejected' : 'accepted'
    };
  }

  private pushOperationAudit(entry: OperationAuditEntry): void {
    this.operationAudit.push(entry);
    if (this.operationAudit.length > MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES) {
      this.operationAudit.splice(0, this.operationAudit.length - MAX_COMMAND_AUDIT_SNAPSHOT_ENTRIES);
    }
    this.emit('operation-audit', entry);
  }

  private writeBlocked(confirmed: boolean, target: { systemId: number; componentId: number } | null, command?: CommandName): string | null {
    if (!confirmed) {
      return 'operator confirmation is required';
    }
    const selected = this.selectedVehiclePayload();
    const authorityMode = this.config.authorityMode;
    const capability = defaultCommandCapabilities(selected.connected, this.config.writableAnimus, {
      packetAgeS: selected.packetAgeS,
      readiness: selected.status?.readiness,
      authority: authorityMode
    });
    if (!this.config.writableAnimus) {
      return 'Animus writes require --writable-animus or --trusted-live-writable at launch.';
    }
    if (authorityMode === 'trusted-live-writable' && !this.protocolTracker.snapshot().signed) {
      return 'trusted live writes require a signed MAVLink v2 link before Animus will send protocol operations.';
    }
    if (command) {
      const vehicleId = target ? `${target.systemId}:${target.componentId}` : `${selected.systemId ?? '--'}:${selected.componentId ?? '--'}`;
      const result = evaluateGuardedCommand({ command, vehicleId, confirmed }, capability);
      if (!result.accepted) {
        return result.reason;
      }
    } else {
      if (!target || !selected.connected) {
        return 'no live vehicle link advertises command support';
      }
      if (selected.packetAgeS === null || selected.packetAgeS >= 2) {
        return 'Live command actions are blocked because the selected link is stale.';
      }
      const blockedReadiness = selected.status?.readiness?.checks.find((check) => check.state === 'blocked');
      if (blockedReadiness) {
        return `${blockedReadiness.label} blocks commands: ${blockedReadiness.detail}`;
      }
    }
    if (!this.socket || !this.lastRemote) {
      return 'no writable MAVLink remote endpoint is available';
    }
    return null;
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
    qgcEndpoints: config.qgcEndpoints ?? [{ host: DEFAULT_QGC_HOST, port: DEFAULT_QGC_PORT }],
    authorityMode: config.authorityMode ?? (config.writableAnimus ? 'sitl-writable' : 'read-only'),
    writableAnimus: config.writableAnimus ?? Boolean(config.authorityMode && config.authorityMode !== 'read-only')
  };
}

function parseVehicleTarget(vehicleId: string, fallback: VehicleStatePayload): { systemId: number; componentId: number } | null {
  const [systemText, componentText] = vehicleId.split(':');
  const systemId = Number(systemText);
  const componentId = Number(componentText);
  if (Number.isInteger(systemId) && Number.isInteger(componentId) && systemId > 0 && systemId <= 255 && componentId >= 0 && componentId <= 255) {
    return { systemId, componentId };
  }
  if (fallback.systemId !== null && fallback.componentId !== null) {
    return { systemId: fallback.systemId, componentId: fallback.componentId };
  }
  return null;
}

export function commandSpec(command: CommandName, params: GuardedCommandRequest['params']): { command: number; params: number[] } | null {
  const altitude = typeof params?.altitudeM === 'number' ? params.altitudeM : 0;
  switch (command) {
    case 'arm':
      return { command: MAV_CMD.COMPONENT_ARM_DISARM, params: [1] };
    case 'disarm':
      return { command: MAV_CMD.COMPONENT_ARM_DISARM, params: [0] };
    case 'emergency-stop':
      return { command: MAV_CMD.COMPONENT_ARM_DISARM, params: [0, 21196] };
    case 'takeoff':
      return { command: MAV_CMD.NAV_TAKEOFF, params: [0, 0, 0, 0, 0, 0, altitude || 50] };
    case 'land':
      return { command: MAV_CMD.NAV_LAND, params: [] };
    case 'return-to-launch':
      return { command: MAV_CMD.NAV_RETURN_TO_LAUNCH, params: [] };
    case 'pause':
      return { command: MAV_CMD.DO_PAUSE_CONTINUE, params: [0] };
    case 'mission-start':
    case 'mission-continue':
    case 'mission-resume':
      return { command: MAV_CMD.MISSION_START, params: [] };
    case 'change-altitude':
      return { command: MAV_CMD.DO_CHANGE_ALTITUDE, params: [altitude] };
    default:
      return null;
  }
}

function performanceNowS(): number {
  return Number(process.hrtime.bigint()) / 1000000000;
}
