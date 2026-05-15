import {
  LIVE_VIEWER_TARGET_ANALYSIS_VEHICLES,
  type CommandCapabilityState,
  type CommandName,
  type GuardedCommandRequest,
  type GuardedCommandResult,
  type LinkDiagnostics,
  type MockLinkState,
  type MultiVehicleAnalysis,
  type SessionSnapshotMessage,
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

export function defaultCommandCapabilities(liveLink: boolean, writableLink = false): CommandCapabilityState {
  return {
    liveLink,
    writableLink,
    supported: liveLink && writableLink ? LIVE_COMMANDS : [],
    blockedReason: liveLink && writableLink ? null : liveLink ? 'Live command actions require an explicitly writable link.' : 'No live link is active.'
  };
}

export function evaluateGuardedCommand(request: GuardedCommandRequest, capability: CommandCapabilityState | null | undefined): GuardedCommandResult {
  if (!request.confirmed) {
    return rejected(request, 'operator confirmation is required');
  }
  if (!capability?.liveLink) {
    return rejected(request, 'no live vehicle link advertises command support');
  }
  if (!capability.writableLink) {
    return rejected(request, capability.blockedReason ?? 'link is read-only');
  }
  if (!capability.supported.includes(request.command)) {
    return rejected(request, `${request.command} is not advertised by the selected vehicle`);
  }
  return {
    accepted: true,
    command: request.command,
    vehicleId: request.vehicleId,
    reason: 'command accepted by guarded dispatcher',
    mock: false
  };
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
