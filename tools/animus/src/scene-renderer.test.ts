import { describe, expect, it } from 'vitest';
import { Vector3 } from 'three';
import { CAMERA_MODES, aircraftPoseFromTelemetry, aircraftVisualNoseDirection, cameraPresetOffset, classifyVehicleModel, nextCameraMode } from './scene-renderer';

function attitude(rollRad = 0, pitchRad = 0, yawRad = 0) {
  return { rollRad, pitchRad, yawRad };
}

function expectVectorClose(actual: Vector3, expected: Vector3): void {
  expect(actual.x).toBeCloseTo(expected.x, 6);
  expect(actual.y).toBeCloseTo(expected.y, 6);
  expect(actual.z).toBeCloseTo(expected.z, 6);
}

describe('scene renderer helpers', () => {
  it('cycles through the Section 9 camera modes', () => {
    expect(CAMERA_MODES).toEqual(['chase', 'orbit', 'top', 'side', 'fpv', 'free']);
    expect(nextCameraMode('chase')).toBe('orbit');
    expect(nextCameraMode('side')).toBe('fpv');
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

  it('maps cardinal telemetry yaw into east/north/up heading vectors', () => {
    expectVectorClose(aircraftPoseFromTelemetry(attitude(0, 0, 0)).heading, new Vector3(0, 1, 0));
    expectVectorClose(aircraftPoseFromTelemetry(attitude(0, 0, Math.PI / 2)).heading, new Vector3(1, 0, 0));
    expectVectorClose(aircraftPoseFromTelemetry(attitude(0, 0, Math.PI)).heading, new Vector3(0, -1, 0));
    expectVectorClose(aircraftPoseFromTelemetry(attitude(0, 0, Math.PI * 1.5)).heading, new Vector3(-1, 0, 0));
  });

  it('maps the model visual nose axis to cardinal telemetry directions', () => {
    expectVectorClose(aircraftVisualNoseDirection(attitude(0, 0, 0)), new Vector3(0, 1, 0));
    expectVectorClose(aircraftVisualNoseDirection(attitude(0, 0, Math.PI / 2)), new Vector3(1, 0, 0));
    expectVectorClose(aircraftVisualNoseDirection(attitude(0, 0, Math.PI)), new Vector3(0, -1, 0));
    expectVectorClose(aircraftVisualNoseDirection(attitude(0, 0, Math.PI * 1.5)), new Vector3(-1, 0, 0));
  });

  it('raises the nose for positive pitch without changing flat heading', () => {
    const pose = aircraftPoseFromTelemetry(attitude(0, Math.PI / 6, 0));
    expect(pose.heading.z).toBeCloseTo(0.5, 6);
    expectVectorClose(pose.heading.clone().setZ(0).normalize(), new Vector3(0, 1, 0));
  });

  it('banks on isolated roll without changing heading', () => {
    const pose = aircraftPoseFromTelemetry(attitude(Math.PI / 6, 0, 0));
    expectVectorClose(pose.heading, new Vector3(0, 1, 0));
    expect(pose.right.z).toBeLessThan(0);
    expect(pose.up.z).toBeCloseTo(Math.cos(Math.PI / 6), 6);
  });

  it('keeps the pose basis orthonormal for combined roll pitch and yaw', () => {
    const pose = aircraftPoseFromTelemetry(attitude(0.31, -0.22, 1.17));
    expect(pose.heading.length()).toBeCloseTo(1, 6);
    expect(pose.right.length()).toBeCloseTo(1, 6);
    expect(pose.up.length()).toBeCloseTo(1, 6);
    expect(pose.heading.dot(pose.right)).toBeCloseTo(0, 6);
    expect(pose.heading.dot(pose.up)).toBeCloseTo(0, 6);
    expect(pose.right.dot(pose.up)).toBeCloseTo(0, 6);
    expect(pose.euler.order).toBe('ZYX');
  });

  it('provides an FPV camera preset ahead of the vehicle nose', () => {
    expectVectorClose(cameraPresetOffset('fpv', attitude(0, 0, Math.PI / 2)), new Vector3(10, 0, 3.5));
  });
});
