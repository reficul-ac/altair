import type { SessionSnapshotMessage } from './state.js';

export type SessionSnapshotCompatibilityResult = {
  snapshot: SessionSnapshotMessage | null;
  warnings: string[];
  errors: string[];
};

type MutableSnapshot = SessionSnapshotMessage & Record<string, unknown>;
type OptionalArrayDomain =
  | 'logSources'
  | 'console'
  | 'mockLinks'
  | 'commandTransactions'
  | 'commandAudit'
  | 'protocolOperations'
  | 'operationAudit';

export function normalizeSessionSnapshot(value: unknown): SessionSnapshotCompatibilityResult {
  const warnings: string[] = [];
  const errors: string[] = [];
  if (!isRecord(value)) {
    return { snapshot: null, warnings, errors: ['session snapshot must be an object'] };
  }
  if (value.type !== 'session_snapshot') {
    return { snapshot: null, warnings, errors: ['session snapshot type must be session_snapshot'] };
  }
  requireArray(value, 'vehicles', errors);
  requireArray(value, 'messages', errors);
  requireArray(value, 'events', errors);
  requireFiniteNumber(value, 'packetCount', errors);
  requireFiniteNumber(value, 'decodedCount', errors);
  if (value.selectedVehicleId !== undefined && value.selectedVehicleId !== null && typeof value.selectedVehicleId !== 'string') {
    errors.push('session snapshot selectedVehicleId must be a string or null');
  }
  if (Array.isArray(value.vehicles)) {
    value.vehicles.forEach((vehicle, index) => validateVehicle(vehicle, `vehicles.${index}`, errors));
  }
  if (errors.length > 0) {
    return { snapshot: null, warnings, errors };
  }

  const snapshot = { ...(value as SessionSnapshotMessage) } as MutableSnapshot;
  snapshot.selectedVehicleId = typeof snapshot.selectedVehicleId === 'string' ? snapshot.selectedVehicleId : null;
  normalizeOptionalArray(snapshot, 'logSources', warnings);
  normalizeOptionalArray(snapshot, 'console', warnings);
  normalizeOptionalArray(snapshot, 'mockLinks', warnings);
  normalizeOptionalArray(snapshot, 'commandTransactions', warnings);
  normalizeOptionalArray(snapshot, 'commandAudit', warnings);
  normalizeOptionalArray(snapshot, 'protocolOperations', warnings);
  normalizeOptionalArray(snapshot, 'operationAudit', warnings);
  return { snapshot, warnings, errors };
}

function validateVehicle(value: unknown, path: string, errors: string[]): void {
  if (!isRecord(value)) {
    errors.push(`${path} must be an object`);
    return;
  }
  if (value.type !== 'vehicle_state') {
    errors.push(`${path}.type must be vehicle_state`);
  }
  if (value.id !== undefined && typeof value.id !== 'string') {
    errors.push(`${path}.id must be a string when present`);
  }
  requireBoolean(value, 'connected', `${path}.connected`, errors);
  requireNullableFiniteNumber(value, 'packetAgeS', `${path}.packetAgeS`, errors);
  requireNullableFiniteNumber(value, 'heartbeatAgeS', `${path}.heartbeatAgeS`, errors);
  requireNullableFiniteNumber(value, 'systemId', `${path}.systemId`, errors);
  requireNullableFiniteNumber(value, 'componentId', `${path}.componentId`, errors);
  if (value.vehicleType !== null && typeof value.vehicleType !== 'string') {
    errors.push(`${path}.vehicleType must be a string or null`);
  }
  validateAttitude(value.attitude, `${path}.attitude`, errors);
  validateGlobalPosition(value.globalPosition, `${path}.globalPosition`, errors);
  validateLocalPosition(value.localPosition, `${path}.localPosition`, errors);
  validateVelocity(value.velocity, `${path}.velocity`, errors);
  validateMetrics(value.metrics, `${path}.metrics`, errors);
}

function validateAttitude(value: unknown, path: string, errors: string[]): void {
  if (!isRecord(value)) {
    errors.push(`${path} must be an object`);
    return;
  }
  for (const field of ['rollRad', 'pitchRad', 'yawRad', 'rollRateRps', 'pitchRateRps', 'yawRateRps']) {
    requireFiniteNumber(value, field, errors, `${path}.${field}`);
  }
}

function validateGlobalPosition(value: unknown, path: string, errors: string[]): void {
  if (!isRecord(value)) {
    errors.push(`${path} must be an object`);
    return;
  }
  for (const field of ['latDeg', 'lonDeg', 'altitudeM', 'relativeAltitudeM', 'originLatDeg', 'originLonDeg', 'originAltitudeM']) {
    requireNullableFiniteNumber(value, field, `${path}.${field}`, errors);
  }
}

function validateLocalPosition(value: unknown, path: string, errors: string[]): void {
  if (!isRecord(value)) {
    errors.push(`${path} must be an object`);
    return;
  }
  for (const field of ['northM', 'eastM', 'upM']) {
    requireNullableFiniteNumber(value, field, `${path}.${field}`, errors);
  }
}

function validateVelocity(value: unknown, path: string, errors: string[]): void {
  if (!isRecord(value)) {
    errors.push(`${path} must be an object`);
    return;
  }
  for (const field of ['northMps', 'eastMps', 'downMps']) {
    requireFiniteNumber(value, field, errors, `${path}.${field}`);
  }
}

function validateMetrics(value: unknown, path: string, errors: string[]): void {
  if (!isRecord(value)) {
    errors.push(`${path} must be an object`);
    return;
  }
  for (const field of ['headingDeg', 'airspeedMps', 'groundspeedMps', 'climbMps', 'throttlePct']) {
    requireNullableFiniteNumber(value, field, `${path}.${field}`, errors);
  }
}

function normalizeOptionalArray(snapshot: MutableSnapshot, key: OptionalArrayDomain, warnings: string[]): void {
  const value = snapshot[key];
  if (value === undefined) {
    snapshot[key] = [] as never;
    warnings.push(`session snapshot missing optional ${String(key)}; normalized to empty array`);
    return;
  }
  if (!Array.isArray(value)) {
    snapshot[key] = [] as never;
    warnings.push(`session snapshot optional ${String(key)} must be an array; normalized to empty array`);
  }
}

function requireArray(value: Record<string, unknown>, key: string, errors: string[]): void {
  if (!Array.isArray(value[key])) {
    errors.push(`session snapshot ${key} must be an array`);
  }
}

function requireBoolean(value: Record<string, unknown>, key: string, path: string, errors: string[]): void {
  if (typeof value[key] !== 'boolean') {
    errors.push(`${path} must be a boolean`);
  }
}

function requireFiniteNumber(value: Record<string, unknown>, key: string, errors: string[], path = `session snapshot ${key}`): void {
  if (!Number.isFinite(value[key])) {
    errors.push(`${path} must be a finite number`);
  }
}

function requireNullableFiniteNumber(value: Record<string, unknown>, key: string, path: string, errors: string[]): void {
  if (value[key] !== null && !Number.isFinite(value[key])) {
    errors.push(`${path} must be a finite number or null`);
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

export function assertCompatibleSessionSnapshot(value: unknown, context: string): { snapshot: SessionSnapshotMessage; warnings: string[] } {
  const result = normalizeSessionSnapshot(value);
  if (result.snapshot === null) {
    throw new Error(`${context}: ${result.errors.join('; ')}`);
  }
  return { snapshot: result.snapshot, warnings: result.warnings };
}
