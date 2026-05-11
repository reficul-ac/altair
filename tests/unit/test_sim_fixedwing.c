#include "sim_fixedwing.h"

#include "math_utils.h"

#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

static sim_fixedwing_state_t run_for(real_t throttle, real_t aileron, real_t elevator, real_t seconds) {
  sim_fixedwing_params_t params;
  sim_fixedwing_state_t state;
  actuator_cmd_t cmd;
  int steps = (int)(seconds / 0.01f);
  int i;

  sim_fixedwing_default_params(&params);
  sim_fixedwing_init_default(&state);
  cmd.motor = throttle;
  cmd.aileron = aileron;
  cmd.elevator = elevator;
  cmd.rudder = 0.0f;
  for (i = 0; i < steps; ++i) {
    if (!sim_fixedwing_step(&state, &params, &cmd, 0.01f)) {
      state.last_airspeed_mps = NAN;
      return state;
    }
  }
  return state;
}

int main(void) {
  sim_fixedwing_params_t params;
  sim_fixedwing_state_t cruise;
  sim_fixedwing_state_t low_throttle;
  sim_fixedwing_state_t high_throttle;
  sim_fixedwing_state_t roll;
  sim_fixedwing_state_t pitch;
  actuator_cmd_t cmd = {0.55f, 0.0f, 0.0f, 0.0f};
  int i;

  sim_fixedwing_default_params(&params);
  sim_fixedwing_init_default(&cruise);
  for (i = 0; i < 1000; ++i) {
    CHECK(sim_fixedwing_step(&cruise, &params, &cmd, 0.01f));
  }
  CHECK(cruise.last_airspeed_mps > 8.0f && cruise.last_airspeed_mps < 45.0f);
  CHECK(-cruise.body.position_ned_m.z > 20.0f && -cruise.body.position_ned_m.z < 300.0f);

  low_throttle = run_for(0.25f, 0.0f, 0.0f, 3.0f);
  high_throttle = run_for(0.85f, 0.0f, 0.0f, 3.0f);
  CHECK(high_throttle.last_airspeed_mps > low_throttle.last_airspeed_mps + 1.0f);

  roll = run_for(0.55f, 0.5f, 0.0f, 1.0f);
  CHECK(fabsf(roll.body.omega_body_rps.x) > 0.5f);

  pitch = run_for(0.65f, 0.0f, 0.4f, 1.0f);
  CHECK(pitch.body.omega_body_rps.y > 0.3f || -pitch.body.position_ned_m.z > 121.0f);
  CHECK(sim_fixedwing_state_is_valid(&pitch));
  return 0;
}
