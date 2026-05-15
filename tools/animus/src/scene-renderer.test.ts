import { describe, expect, it } from 'vitest';
import { CAMERA_MODES, classifyVehicleModel, nextCameraMode } from './scene-renderer';

describe('scene renderer helpers', () => {
  it('cycles through the Section 9 camera modes', () => {
    expect(CAMERA_MODES).toEqual(['chase', 'orbit', 'top', 'side', 'free']);
    expect(nextCameraMode('chase')).toBe('orbit');
    expect(nextCameraMode('free')).toBe('chase');
  });

  it('classifies heartbeat vehicle labels into renderable model families', () => {
    expect(classifyVehicleModel('Fixed-wing')).toBe('fixed-wing');
    expect(classifyVehicleModel('Quadrotor')).toBe('multirotor');
    expect(classifyVehicleModel('Helicopter')).toBe('multirotor');
    expect(classifyVehicleModel('VTOL tailsitter')).toBe('vtol');
    expect(classifyVehicleModel('Ground rover')).toBe('generic');
    expect(classifyVehicleModel(null)).toBe('generic');
  });
});
