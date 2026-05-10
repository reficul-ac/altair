#include "altair_vehicle.h"
#include "fsw.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

static void make_valid_input(fsw_input_t *in) {
  in->dt_s = 0.01f;
  in->rc.throttle = 0.5f;
  in->rc.roll = 0.0f;
  in->rc.pitch = 0.0f;
  in->rc.yaw = 0.0f;
  in->rc.arm_switch = 1U;
  in->rc.mode_switch = 0U;
  in->imu.gyro_rps.x = 0.0f;
  in->imu.gyro_rps.y = 0.0f;
  in->imu.gyro_rps.z = 0.0f;
  in->imu.accel_mps2.x = 0.0f;
  in->imu.accel_mps2.y = 0.0f;
  in->imu.accel_mps2.z = -9.80665f;
  in->gps.vel_mps.x = 15.0f;
  in->gps.vel_mps.y = 0.0f;
  in->gps.vel_mps.z = 0.0f;
  in->gps.fix_valid = 1U;
  in->baro.altitude_m = 100.0f;
  in->airspeed.true_airspeed_mps = 15.0f;
}

static int check_altair_limits(const actuator_cmd_t *actuators) {
  CHECK(actuators->motor >= 0.0f && actuators->motor <= 1.0f);
  CHECK(actuators->aileron >= -1.0f && actuators->aileron <= 1.0f);
  CHECK(actuators->elevator >= -1.0f && actuators->elevator <= 1.0f);
  CHECK(actuators->rudder >= -1.0f && actuators->rudder <= 1.0f);
  return 0;
}

int main(void) {
  fsw_input_t in;
  fsw_output_t out;

  bayek_fsw_init(altair_vehicle_interface());
  make_valid_input(&in);

  in.rc.arm_switch = 0U;
  in.rc.throttle = 2.0f;
  in.rc.roll = 2.0f;
  in.rc.pitch = -2.0f;
  in.rc.yaw = 2.0f;
  bayek_fsw_step(&in, &out);
  CHECK(out.mode == FSW_MODE_DISARMED);
  CHECK(out.actuators.motor == 0.0f);
  CHECK(out.actuators.aileron == 0.0f);
  CHECK(out.actuators.elevator == 0.0f);
  CHECK(out.actuators.rudder == 0.0f);

  in.rc.arm_switch = 1U;
  in.rc.mode_switch = 0U;
  bayek_fsw_step(&in, &out);
  CHECK(out.mode == FSW_MODE_MANUAL);
  CHECK(check_altair_limits(&out.actuators) == 0);
  CHECK(out.actuators.motor == 1.0f);
  CHECK(out.actuators.aileron == 1.0f);
  CHECK(out.actuators.elevator == -1.0f);
  CHECK(out.actuators.rudder == 1.0f);

  in.rc.mode_switch = 1U;
  bayek_fsw_step(&in, &out);
  CHECK(out.mode == FSW_MODE_STABILIZE);
  CHECK(check_altair_limits(&out.actuators) == 0);

  return 0;
}
