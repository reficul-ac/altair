#include "altair_params.h"
#include "fsw.h"

#include <stdio.h>
#include <time.h>

int main(void) {
  enum { iterations = 100000 };
  fsw_input_t in;
  fsw_output_t out;
  int i;
  clock_t start;
  clock_t end;
  double elapsed_s;
  double us_per_step;

  fsw_init(altair_default_params());
  in.dt_s = 0.01f;
  in.rc.throttle = 0.5f;
  in.rc.roll = 0.1f;
  in.rc.pitch = 0.0f;
  in.rc.yaw = 0.0f;
  in.rc.arm_switch = 1U;
  in.rc.mode_switch = 1U;
  in.imu.gyro_rps.x = 0.0f;
  in.imu.gyro_rps.y = 0.0f;
  in.imu.gyro_rps.z = 0.0f;
  in.imu.accel_mps2.x = 0.0f;
  in.imu.accel_mps2.y = 0.0f;
  in.imu.accel_mps2.z = -9.80665f;
  in.gps.vel_mps.x = 15.0f;
  in.gps.vel_mps.y = 0.0f;
  in.gps.vel_mps.z = 0.0f;
  in.gps.fix_valid = 1U;
  in.baro.altitude_m = 100.0f;
  in.airspeed.true_airspeed_mps = 15.0f;

  start = clock();
  for (i = 0; i < iterations; ++i) {
    fsw_step(&in, &out);
  }
  end = clock();
  elapsed_s = (double)(end - start) / (double)CLOCKS_PER_SEC;
  us_per_step = elapsed_s * 1000000.0 / (double)iterations;
  printf("fsw_step_iterations=%d elapsed_s=%.6f us_per_step=%.3f\n", iterations, elapsed_s, us_per_step);
  return us_per_step < 1000.0 ? 0 : 1;
}
