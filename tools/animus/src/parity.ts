import {
  LIVE_VIEWER_TARGET_ANALYSIS_VEHICLES,
  type CommandCapabilityState,
  type CommandAuthorityMode,
  type FirmwareIdentity,
  type CommandName,
  type GuardedCommandRequest,
  type GuardedCommandResult,
  type LinkDiagnostics,
  type MockLinkState,
  type MissionState,
  type MultiVehicleAnalysis,
  type NormalizedArmingState,
  type NormalizedFailsafeState,
  type NormalizedFlightMode,
  type SessionSnapshotMessage,
  type VehicleReadiness,
  type VehicleStateMessage
} from './state.js';

const LIVE_COMMANDS: CommandName[] = [
  'arm',
  'disarm',
  'emergency-stop',
  'takeoff',
  'land',
  'return-to-launch',
  'pause',
  'change-altitude',
  'go-to',
  'orbit',
  'mission-start',
  'mission-continue',
  'mission-resume'
];

const PREFLIGHT_GATED_COMMANDS: CommandName[] = [
  'arm',
  'takeoff',
  'go-to',
  'orbit',
  'change-altitude',
  'mission-start',
  'mission-continue',
  'mission-resume'
];

const SAFETY_RESPONSE_COMMANDS: CommandName[] = [
  'disarm',
  'emergency-stop',
  'pause',
  'land',
  'return-to-launch'
];

export function defaultCommandCapabilities(liveLink: boolean, writableLink = false, options: { packetAgeS?: number | null; readiness?: VehicleReadiness; authority?: CommandAuthorityMode } = {}): CommandCapabilityState {
  const stale = options.packetAgeS === null || options.packetAgeS === undefined ? false : options.packetAgeS >= 2;
  const authority = options.authority ?? (writableLink ? 'sitl-writable' : 'read-only');
  const baseReady = liveLink && writableLink && !stale && authority === 'sitl-writable';
  const preflightReady = !options.readiness || options.readiness.overall === 'ready' || options.readiness.overall === 'warning';
  const supported = baseReady
    ? [
        ...SAFETY_RESPONSE_COMMANDS,
        ...(preflightReady ? PREFLIGHT_GATED_COMMANDS : [])
      ]
    : [];
  const blockedCommands = commandBlockedReasons(liveLink, writableLink, stale, options.readiness, authority);
  const blockedReason = supported.length === LIVE_COMMANDS.length
    ? null
    : commandBlockReason(liveLink, writableLink, stale, options.readiness, authority);
  return {
    liveLink,
    writableLink,
    authority,
    stale,
    supported,
    blockedReason,
    blockedCommands,
    readiness: options.readiness
  };
}

export function evaluateGuardedCommand(request: GuardedCommandRequest, capability: CommandCapabilityState | null | undefined): GuardedCommandResult {
  if (!request.confirmed) {
    return rejected(request, 'operator confirmation is required');
  }
  if (!capability?.liveLink) {
    return rejected(request, 'no live vehicle link advertises command support');
  }
  if (capability.stale) {
    return rejected(request, capability.blockedReason ?? 'link is stale');
  }
  if (!capability.writableLink) {
    return rejected(request, capability.blockedReason ?? 'link is read-only');
  }
  if (!capability.supported.includes(request.command)) {
    return rejected(request, capability.blockedCommands?.[request.command] ?? `${request.command} is not advertised by the selected vehicle`);
  }
  return {
    accepted: true,
    command: request.command,
    vehicleId: request.vehicleId,
    reason: 'command accepted by guarded dispatcher',
    mock: false
  };
}

export function firmwareIdentity(autopilot: number | null | undefined): FirmwareIdentity {
  switch (autopilot) {
    case 3:
      return { family: 'ardupilot', label: 'ArduPilot', autopilot, source: 'heartbeat', unsupportedReason: null };
    case 12:
      return { family: 'px4', label: 'PX4', autopilot, source: 'heartbeat', unsupportedReason: null };
    case 0:
      return { family: 'generic', label: 'Generic MAVLink', autopilot, source: 'heartbeat', unsupportedReason: 'firmware-specific capabilities are unknown for generic MAVLink' };
    case null:
    case undefined:
      return { family: 'unknown', label: 'Unknown firmware', autopilot: null, source: 'unknown', unsupportedReason: 'no heartbeat autopilot field has been decoded yet' };
    default:
      return { family: 'unsupported', label: `MAV_AUTOPILOT ${autopilot}`, autopilot, source: 'heartbeat', unsupportedReason: `MAV_AUTOPILOT ${autopilot} has no Animus compatibility mapping yet` };
  }
}

export function normalizeFlightMode(autopilot: number | null | undefined, customMode: number | null | undefined, baseMode: number | null | undefined): NormalizedFlightMode {
  const firmware = firmwareIdentity(autopilot);
  const armed = typeof baseMode === 'number' ? (baseMode & 0x80) !== 0 : null;
  if (baseMode === null || baseMode === undefined || customMode === null || customMode === undefined) {
    return { label: 'Unknown', family: firmware.family, category: 'unknown', baseMode: baseMode ?? null, customMode: customMode ?? null, armed, known: false, unsupportedReason: 'heartbeat mode fields are incomplete' };
  }
  if ((baseMode & 0x80) === 0 && customMode === 0) {
    return { label: 'Standby', family: firmware.family, category: 'standby', baseMode, customMode, armed, known: true, unsupportedReason: null };
  }
  const baseLabel = baseModeLabel(baseMode);
  if (firmware.family === 'px4') {
    const label = px4ModeLabel(customMode);
    return { label: label ?? `PX4 custom ${customMode}`, family: firmware.family, category: categoryFor(label, baseLabel), baseMode, customMode, armed, known: label !== null, unsupportedReason: label === null ? 'PX4 custom mode is not mapped yet' : null };
  }
  if (firmware.family === 'ardupilot') {
    const label = ardupilotModeLabel(customMode);
    return { label: label ?? `ArduPilot custom ${customMode}`, family: firmware.family, category: categoryFor(label, baseLabel), baseMode, customMode, armed, known: label !== null, unsupportedReason: label === null ? 'ArduPilot custom mode is not mapped yet' : null };
  }
  const label = customMode === 0 ? baseLabel : `${firmware.label} custom ${customMode}`;
  return {
    label,
    family: firmware.family,
    category: customMode === 0 ? categoryFor(baseLabel, baseLabel) : 'unknown',
    baseMode,
    customMode,
    armed,
    known: customMode === 0,
    unsupportedReason: customMode === 0 ? firmware.unsupportedReason : `custom mode ${customMode} needs a firmware-specific mapping`
  };
}

export function normalizeArmingState(armed: boolean | null | undefined, readiness: VehicleReadiness): NormalizedArmingState {
  if (armed === true) {
    return { armed: true, label: 'armed', readyForArm: 'ready', reason: null };
  }
  if (armed === false) {
    const blocked = readiness.checks.find((check) => check.state === 'blocked');
    return { armed: false, label: 'disarmed', readyForArm: blocked ? 'blocked' : readiness.overall === 'unknown' ? 'unknown' : 'ready', reason: blocked?.detail ?? null };
  }
  return { armed: null, label: 'unknown', readyForArm: 'unknown', reason: 'arming state has not been decoded from heartbeat' };
}

export function normalizeFailsafeState(autopilot: number | null | undefined, systemStatus: number | null | undefined): NormalizedFailsafeState {
  const firmware = firmwareIdentity(autopilot);
  const raw = typeof systemStatus === 'number' && Number.isFinite(systemStatus) ? systemStatus : null;
  const base = { systemStatus: raw, family: firmware.family };
  switch (raw) {
    case 3:
      return { ...base, status: 'standby', label: 'Standby', known: true, commandBlocking: false };
    case 4:
      return { ...base, status: 'active', label: 'Active', known: true, commandBlocking: false };
    case 5:
      return { ...base, status: 'critical', label: 'Critical failsafe', known: true, commandBlocking: true };
    case 6:
      return { ...base, status: 'emergency', label: 'Emergency failsafe', known: true, commandBlocking: true };
    case 7:
      return { ...base, status: 'poweroff', label: 'Poweroff', known: true, commandBlocking: true };
    case 8:
      return { ...base, status: 'emergency', label: 'Flight termination', known: true, commandBlocking: true };
    default:
      return {
        ...base,
        status: 'unknown',
        label: raw === null ? 'Unknown' : `MAV_STATE ${raw}`,
        known: false,
        commandBlocking: false
      };
  }
}

export function normalizeMissionState(input: {
  activeSeq?: number | null;
  totalItems?: number | null;
  modeState?: NormalizedFlightMode | null;
  lastAckType?: number | null;
}): MissionState {
  const activeSeq = integerOrNull(input.activeSeq);
  const totalItems = integerOrNull(input.totalItems);
  const lastAckType = integerOrNull(input.lastAckType);
  const invalidAck = lastAckType !== null && lastAckType !== 0;
  if (totalItems !== null && totalItems < 0) {
    return { activeSeq, totalItems, state: 'unknown', progressPct: null, valid: false, detail: 'mission count is invalid', lastAckType };
  }
  if (activeSeq !== null && activeSeq < 0) {
    return { activeSeq, totalItems, state: 'unknown', progressPct: null, valid: false, detail: 'active mission item is invalid', lastAckType };
  }
  if (invalidAck) {
    return { activeSeq, totalItems, state: 'unknown', progressPct: null, valid: false, detail: `mission ACK rejected with type ${lastAckType}`, lastAckType };
  }
  if (totalItems === null && activeSeq === null) {
    return { activeSeq: null, totalItems: null, state: 'unknown', progressPct: null, valid: false, detail: 'mission state has not been decoded', lastAckType };
  }
  if (totalItems === 0) {
    return { activeSeq, totalItems, state: 'not-started', progressPct: 0, valid: true, detail: 'no mission items loaded', lastAckType };
  }
  if (totalItems !== null && activeSeq !== null && activeSeq >= totalItems) {
    return { activeSeq, totalItems, state: 'complete', progressPct: 100, valid: true, detail: `mission complete (${totalItems}/${totalItems})`, lastAckType };
  }
  if (totalItems !== null && activeSeq === null) {
    return { activeSeq, totalItems, state: 'not-started', progressPct: 0, valid: true, detail: `${totalItems} mission item${totalItems === 1 ? '' : 's'} loaded`, lastAckType };
  }
  const progressPct = totalItems !== null && activeSeq !== null ? Math.max(0, Math.min(100, ((activeSeq + 1) / totalItems) * 100)) : null;
  const modeCategory = input.modeState?.category ?? 'unknown';
  const state = activeSeq === 0 && modeCategory !== 'auto'
    ? 'not-started'
    : modeCategory === 'auto'
      ? 'active'
      : 'paused';
  return {
    activeSeq,
    totalItems,
    state,
    progressPct,
    valid: true,
    detail: missionDetail(state, activeSeq, totalItems, progressPct),
    lastAckType
  };
}

export function buildVehicleReadiness(vehicle: Pick<VehicleStateMessage, 'connected' | 'packetAgeS' | 'status'>): VehicleReadiness {
  const checks: VehicleReadiness['checks'] = [];
  const stale = vehicle.packetAgeS === null || vehicle.packetAgeS >= 2;
  checks.push({
    key: 'link',
    label: 'Live link',
    state: vehicle.connected && !stale ? 'ready' : 'blocked',
    detail: vehicle.connected && !stale ? 'fresh MAVLink packets' : stale ? 'selected link is stale or missing packet age' : 'no selected live link'
  });
  const gps = vehicle.status?.gpsFix;
  checks.push({
    key: 'gps',
    label: 'GPS',
    state: gps === null || gps === undefined ? 'unknown' : /3D|DGPS|RTK/.test(gps) ? 'ready' : 'warning',
    detail: gps ?? 'GPS fix has not been decoded'
  });
  const estimator = vehicle.status?.onboardControlSensorsHealth;
  checks.push({
    key: 'estimator',
    label: 'Estimator',
    state: estimator === null || estimator === undefined ? 'unknown' : estimator === 0 ? 'blocked' : 'ready',
    detail: estimator === null || estimator === undefined ? 'SYS_STATUS sensor health has not been decoded' : estimator === 0 ? 'SYS_STATUS reports no healthy onboard control sensors' : `sensor health mask ${estimator}`
  });
  const battery = vehicle.status?.batteryRemainingPct;
  const voltage = vehicle.status?.batteryVoltageV;
  const invalidVoltage = voltage !== null && voltage !== undefined && (!Number.isFinite(voltage) || voltage <= 0);
  checks.push({
    key: 'battery',
    label: 'Battery / power',
    state: invalidVoltage ? 'blocked' : battery === null || battery === undefined ? 'unknown' : battery < 10 ? 'blocked' : battery < 20 ? 'warning' : 'ready',
    detail: batteryPowerDetail(battery, voltage, invalidVoltage)
  });
  const failsafe = vehicle.status?.failsafeState ?? normalizeFailsafeState(vehicle.status?.firmware?.autopilot, vehicle.status?.systemStatus);
  checks.push({
    key: 'failsafe',
    label: 'Failsafe',
    state: failsafe.known ? (failsafe.commandBlocking ? 'blocked' : 'ready') : 'unknown',
    detail: failsafe.known ? failsafe.label : 'MAVLink system status has not been decoded'
  });
  const mission = vehicle.status?.missionState ?? normalizeMissionState({
    activeSeq: vehicle.status?.missionSeq,
    totalItems: null,
    modeState: vehicle.status?.modeState
  });
  checks.push({
    key: 'mission',
    label: 'Mission',
    state: undecodedMission(mission) ? 'unknown' : !mission.valid ? 'blocked' : mission.state === 'unknown' ? 'unknown' : 'ready',
    detail: mission.detail
  });
  const firmware = vehicle.status?.firmware;
  checks.push({
    key: 'firmware',
    label: 'Firmware',
    state: !firmware || firmware.family === 'unknown' ? 'unknown' : firmware.family === 'unsupported' ? 'warning' : 'ready',
    detail: firmware?.unsupportedReason ?? firmware?.label ?? 'firmware identity has not been decoded'
  });
  return { checks, overall: overallReadiness(checks.map((check) => check.state)) };
}

function undecodedMission(mission: MissionState): boolean {
  return mission.state === 'unknown' && mission.activeSeq === null && mission.totalItems === null && (mission.lastAckType === null || mission.lastAckType === undefined);
}

function integerOrNull(value: number | null | undefined): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? Math.trunc(value) : null;
}

function missionDetail(state: MissionState['state'], activeSeq: number | null, totalItems: number | null, progressPct: number | null): string {
  const item = activeSeq === null ? 'unknown item' : `item ${activeSeq}`;
  const progress = progressPct === null ? '' : ` / ${progressPct.toFixed(0)}%`;
  const total = totalItems === null ? '' : ` of ${totalItems}`;
  switch (state) {
    case 'active':
      return `active ${item}${total}${progress}`;
    case 'paused':
      return `paused at ${item}${total}${progress}`;
    case 'not-started':
      return `not started${totalItems === null ? '' : ` / ${totalItems} item${totalItems === 1 ? '' : 's'} loaded`}`;
    case 'complete':
      return `mission complete${totalItems === null ? '' : ` / ${totalItems} item${totalItems === 1 ? '' : 's'}`}`;
    default:
      return 'mission state has not been decoded';
  }
}

export function buildMultiVehicleAnalysis(snapshot: SessionSnapshotMessage, ghostTimestampS: number | null = null): MultiVehicleAnalysis {
  const positioned = snapshot.vehicles
    .map((vehicle) => ({ id: vehicle.id ?? `${vehicle.systemId ?? '--'}:${vehicle.componentId ?? '--'}`, vehicle, point: pointFromVehicle(vehicle) }))
    .filter((entry): entry is { id: string; vehicle: VehicleStateMessage; point: { eastM: number; northM: number; upM: number } } => entry.point !== null);
  const centroid = positioned.length === 0
    ? null
    : {
        eastM: average(positioned.map((entry) => entry.point.eastM)),
        northM: average(positioned.map((entry) => entry.point.northM)),
        upM: average(positioned.map((entry) => entry.point.upM))
      };
  const conflicts: MultiVehicleAnalysis['deconfliction']['conflicts'] = [];
  for (let i = 0; i < positioned.length; i += 1) {
    for (let j = i + 1; j < positioned.length; j += 1) {
      const distanceM = distance(positioned[i].point, positioned[j].point);
      if (distanceM < 25) {
        conflicts.push({ a: positioned[i].id, b: positioned[j].id, distanceM, timestampS: ghostTimestampS });
      }
    }
  }
  return {
    targetVehicleCount: LIVE_VIEWER_TARGET_ANALYSIS_VEHICLES,
    alignment: 'takeoff',
    referenceVehicleId: snapshot.selectedVehicleId,
    ghostTimestampS,
    deconfliction: {
      minimumSeparationM: conflicts.length === 0 ? null : Math.min(...conflicts.map((conflict) => conflict.distanceM)),
      conflicts
    },
    formation: {
      centroid,
      vehicles: centroid === null
        ? []
        : positioned.map((entry) => ({
            id: entry.id,
            eastOffsetM: entry.point.eastM - centroid.eastM,
            northOffsetM: entry.point.northM - centroid.northM,
            upOffsetM: entry.point.upM - centroid.upM
          }))
    }
  };
}

export function createMockLink(vehicleCount: number, id = 'mock-default'): MockLinkState {
  const count = Math.max(1, Math.min(32, Math.floor(vehicleCount)));
  const diagnostics: LinkDiagnostics = {
    linkId: id,
    transport: 'mock',
    status: 'connected',
    packetsRx: 0,
    packetsTx: 0,
    decodedRx: 0,
    drops: 0,
    lastError: null
  };
  return { id, label: `${count} vehicle mock link`, vehicleCount: count, running: true, diagnostics };
}

function rejected(request: GuardedCommandRequest, reason: string): GuardedCommandResult {
  return { accepted: false, command: request.command, vehicleId: request.vehicleId, reason, mock: false };
}

function commandBlockReason(liveLink: boolean, writableLink: boolean, stale: boolean, readiness: VehicleReadiness | undefined, authority: CommandAuthorityMode): string {
  if (!liveLink) return 'No live link is active.';
  if (stale) return 'Live command actions are blocked because the selected link is stale.';
  if (!writableLink) return 'Live command actions require an explicitly writable link.';
  if (authority !== 'sitl-writable') return `Command authority is ${authority}.`;
  const blocked = readiness?.checks.find((check) => check.state === 'blocked');
  if (blocked) return `${blocked.label} blocks commands: ${blocked.detail}`;
  if (readiness?.overall === 'unknown') return 'Vehicle readiness is unknown.';
  return 'Live command actions are blocked.';
}

function commandBlockedReasons(liveLink: boolean, writableLink: boolean, stale: boolean, readiness: VehicleReadiness | undefined, authority: CommandAuthorityMode): Partial<Record<CommandName, string>> {
  const globalReason = commandBlockReason(liveLink, writableLink, stale, readiness, authority);
  const reasons: Partial<Record<CommandName, string>> = {};
  if (!liveLink || stale || !writableLink || authority !== 'sitl-writable') {
    for (const command of LIVE_COMMANDS) reasons[command] = globalReason;
    return reasons;
  }
  const readinessReason = readinessBlockReason(readiness);
  if (readinessReason) {
    for (const command of PREFLIGHT_GATED_COMMANDS) reasons[command] = readinessReason;
  }
  return reasons;
}

function readinessBlockReason(readiness: VehicleReadiness | undefined): string | null {
  const blocked = readiness?.checks.find((check) => check.state === 'blocked');
  if (blocked) return `${blocked.label} blocks commands: ${blocked.detail}`;
  if (readiness?.overall === 'unknown') return 'Vehicle readiness is unknown.';
  return null;
}

function batteryPowerDetail(battery: number | null | undefined, voltage: number | null | undefined, invalidVoltage: boolean): string {
  if (invalidVoltage) return `invalid battery voltage ${voltage} V`;
  const parts = [];
  if (battery === null || battery === undefined) parts.push('battery remaining has not been decoded');
  else parts.push(`${battery}% remaining`);
  if (voltage !== null && voltage !== undefined) parts.push(`${voltage.toFixed(2)} V`);
  return parts.join(' / ');
}

function baseModeLabel(baseMode: number): string {
  if ((baseMode & 0x10) !== 0) return 'Manual';
  if ((baseMode & 0x04) !== 0) return 'Auto';
  if ((baseMode & 0x08) !== 0) return 'Guided';
  if ((baseMode & 0x40) !== 0) return 'Stabilized';
  return `base ${baseMode}`;
}

function px4ModeLabel(customMode: number): string | null {
  const mainMode = (customMode >> 16) & 0xff;
  const subMode = (customMode >> 24) & 0xff;
  const main: Record<number, string> = { 1: 'Manual', 2: 'Altitude', 3: 'Position', 4: 'Auto', 5: 'Acro', 6: 'Offboard', 7: 'Stabilized', 8: 'Rattitude' };
  if (mainMode !== 4) return main[mainMode] ?? null;
  const auto: Record<number, string> = { 1: 'Auto ready', 2: 'Auto takeoff', 3: 'Auto loiter', 4: 'Auto mission', 5: 'Auto RTL', 6: 'Auto land', 8: 'Auto follow target', 9: 'Auto precision land' };
  return auto[subMode] ?? 'Auto';
}

function ardupilotModeLabel(customMode: number): string | null {
  const modes: Record<number, string> = {
    0: 'Manual',
    1: 'Circle',
    2: 'Stabilize',
    3: 'Training',
    4: 'Acro',
    5: 'FBWA',
    6: 'FBWB',
    7: 'Cruise',
    8: 'Autotune',
    10: 'Auto',
    11: 'RTL',
    12: 'Loiter',
    14: 'Avoid ADSB',
    15: 'Guided',
    16: 'Initializing',
    17: 'QStabilize',
    18: 'QHover',
    19: 'QLoiter',
    20: 'QLand',
    21: 'QRTL',
    22: 'QAutotune',
    23: 'QAcro',
    24: 'Thermal'
  };
  return modes[customMode] ?? null;
}

function categoryFor(label: string | null, fallback: string): NormalizedFlightMode['category'] {
  const text = (label ?? fallback).toLowerCase();
  if (text.includes('manual') || text.includes('acro')) return 'manual';
  if (text.includes('auto') || text.includes('mission') || text.includes('rtl') || text.includes('land') || text.includes('takeoff')) return 'auto';
  if (text.includes('guided') || text.includes('offboard')) return 'guided';
  if (text.includes('stabil')) return 'stabilized';
  if (text.includes('standby') || text.includes('initializing')) return 'standby';
  return 'unknown';
}

function overallReadiness(states: VehicleReadiness['checks'][number]['state'][]): VehicleReadiness['overall'] {
  if (states.includes('blocked')) return 'blocked';
  if (states.includes('warning')) return 'warning';
  if (states.includes('unknown')) return 'unknown';
  return 'ready';
}

function pointFromVehicle(vehicle: VehicleStateMessage): { eastM: number; northM: number; upM: number } | null {
  const { eastM, northM, upM } = vehicle.localPosition;
  if (eastM === null || northM === null || upM === null) return null;
  return { eastM, northM, upM };
}

function average(values: number[]): number {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function distance(a: { eastM: number; northM: number; upM: number }, b: { eastM: number; northM: number; upM: number }): number {
  return Math.hypot(a.eastM - b.eastM, a.northM - b.northM, a.upM - b.upM);
}
