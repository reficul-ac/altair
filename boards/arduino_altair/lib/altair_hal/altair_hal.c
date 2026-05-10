#include "altair_hal.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

void altair_hal_init(void) {
}

uint32_t altair_hal_time_us(void) {
#ifdef ARDUINO
  return micros();
#else
  return 0U;
#endif
}

void altair_hal_read_inputs(fsw_input_t *input) {
  input->dt_s = 0.01f;
  input->imu.accel_mps2.x = 0.0f;
  input->imu.accel_mps2.y = 0.0f;
  input->imu.accel_mps2.z = -9.80665f;
  input->imu.gyro_rps.x = 0.0f;
  input->imu.gyro_rps.y = 0.0f;
  input->imu.gyro_rps.z = 0.0f;
  input->imu.timestamp_us = altair_hal_time_us();
  input->gps.lat_deg = 0.0f;
  input->gps.lon_deg = 0.0f;
  input->gps.alt_m = 0.0f;
  input->gps.vel_mps.x = 0.0f;
  input->gps.vel_mps.y = 0.0f;
  input->gps.vel_mps.z = 0.0f;
  input->gps.fix_valid = 0U;
  input->gps.timestamp_us = input->imu.timestamp_us;
  input->baro.pressure_pa = 101325.0f;
  input->baro.altitude_m = 0.0f;
  input->baro.timestamp_us = input->imu.timestamp_us;
  input->airspeed.true_airspeed_mps = 0.0f;
  input->airspeed.timestamp_us = input->imu.timestamp_us;
  input->rc.throttle = 0.0f;
  input->rc.roll = 0.0f;
  input->rc.pitch = 0.0f;
  input->rc.yaw = 0.0f;
  input->rc.arm_switch = 0U;
  input->rc.mode_switch = 0U;
}

void altair_hal_write_actuators(const actuator_cmd_t *cmd) {
  (void)cmd;
}

void altair_hal_send_telemetry(const fsw_output_t *output) {
  (void)output;
}
