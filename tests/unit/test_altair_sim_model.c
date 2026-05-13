#include "altair_sim_model.h"
#include "altair_vehicle.h"
#include "math_utils.h"

#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)
#define CHECK_MSG(c, msg) do { if (!(c)) { printf("check failed: %s:%d: %s\n", __FILE__, __LINE__, msg); return 1; } } while (0)

static int close_real(real_t a, real_t b) {
  return fabsf(a - b) <= 1.0e-6f;
}

static int close_vec3(vec3_t a, vec3_t b) {
  return close_real(a.x, b.x) && close_real(a.y, b.y) && close_real(a.z, b.z);
}

static int close_quat(quat_t a, quat_t b) {
  return close_real(a.w, b.w) && close_real(a.x, b.x) &&
         close_real(a.y, b.y) && close_real(a.z, b.z);
}

static int close_actuator(actuator_cmd_t a, actuator_cmd_t b) {
  return close_real(a.motor, b.motor) &&
         close_real(a.aileron, b.aileron) &&
         close_real(a.elevator, b.elevator) &&
         close_real(a.rudder, b.rudder);
}

static int close_state(sim_fixedwing_state_t a, sim_fixedwing_state_t b) {
  return close_vec3(a.body.position_ned_m, b.body.position_ned_m) &&
         close_vec3(a.body.velocity_ned_mps, b.body.velocity_ned_mps) &&
         close_quat(a.body.attitude_body_to_ned, b.body.attitude_body_to_ned) &&
         close_vec3(a.body.position_ecef_m, b.body.position_ecef_m) &&
         close_vec3(a.body.velocity_ecef_mps, b.body.velocity_ecef_mps) &&
         close_quat(a.body.attitude_body_to_ecef, b.body.attitude_body_to_ecef) &&
         close_vec3(a.body.omega_body_rps, b.body.omega_body_rps) &&
         close_actuator(a.body.actuator_state, b.body.actuator_state) &&
         close_vec3(a.body.specific_force_body_mps2, b.body.specific_force_body_mps2) &&
         close_real(a.body.time_s, b.body.time_s) &&
         close_vec3(a.last_force_body_n, b.last_force_body_n) &&
         close_vec3(a.last_moment_body_nm, b.last_moment_body_nm) &&
         close_real(a.last_airspeed_mps, b.last_airspeed_mps);
}

static sim_fixedwing_state_t run_sequence(const sim_fixedwing_params_t *params, int frame_mode) {
  sim_fixedwing_state_t state;
  sim_fixedwing_params_t local_params = *params;
  actuator_cmd_t cmd;
  int i;

  local_params.core.frame_mode = frame_mode;
  sim_fixedwing_init_default(&state);
  for (i = 0; i < 250; ++i) {
    cmd.motor = i < 80 ? 0.62f : 0.54f;
    cmd.aileron = i < 60 ? 0.0f : (i < 170 ? 0.18f : -0.08f);
    cmd.elevator = i < 120 ? 0.04f : -0.03f;
    cmd.rudder = i < 150 ? 0.0f : 0.05f;
    if (!sim_fixedwing_step(&state, &local_params, &cmd, 0.01f)) {
      state.last_airspeed_mps = NAN;
      return state;
    }
  }
  return state;
}

int main(void) {
  sim_fixedwing_params_t params;
  sim_fixedwing_params_t invalid;
  sim_fixedwing_state_t first;
  sim_fixedwing_state_t second;
  const vehicle_params_t *vehicle = altair_default_params();
  char error[160];

  altair_fixedwing_sim_params(&params);
  CHECK_MSG(altair_fixedwing_sim_params_are_valid(&params, vehicle, error, sizeof(error)), error);
  CHECK(close_real(params.core.mass_kg, 2.5f));
  CHECK(close_real(params.wing_area_m2, 0.45f));

  invalid = params;
  invalid.core.mass_kg = -1.0f;
  CHECK(!altair_fixedwing_sim_params_are_valid(&invalid, vehicle, error, sizeof(error)));
  CHECK(error[0] != '\0');

  invalid = params;
  invalid.wing_area_m2 = -1.0f;
  CHECK(!altair_fixedwing_sim_params_are_valid(&invalid, vehicle, error, sizeof(error)));
  CHECK(error[0] != '\0');

  invalid = params;
  invalid.max_thrust_n = -1.0f;
  CHECK(!altair_fixedwing_sim_params_are_valid(&invalid, vehicle, error, sizeof(error)));
  CHECK(error[0] != '\0');

  first = run_sequence(&params, SIM6DOF_FRAME_ECEF);
  second = run_sequence(&params, SIM6DOF_FRAME_ECEF);
  CHECK(sim_fixedwing_state_is_valid(&first));
  CHECK(sim_fixedwing_state_is_valid(&second));
  CHECK(close_state(first, second));
  first = run_sequence(&params, SIM6DOF_FRAME_NED);
  second = run_sequence(&params, SIM6DOF_FRAME_NED);
  CHECK(sim_fixedwing_state_is_valid(&first));
  CHECK(sim_fixedwing_state_is_valid(&second));
  CHECK(close_state(first, second));
  return 0;
}
