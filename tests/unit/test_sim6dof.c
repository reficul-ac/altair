#include "sim6dof.h"

#include "math_utils.h"

#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

static real_t quat_norm(quat_t q) {
  return (real_t)sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

int main(void) {
  sim6dof_params_t params;
  sim6dof_state_t state;
  actuator_cmd_t cmd = {0.0f, 0.0f, 0.0f, 0.0f};
  vec3_t zero = {0.0f, 0.0f, 0.0f};
  vec3_t force = {10.0f, 0.0f, 0.0f};
  vec3_t moment = {0.0f, 1.0f, 0.0f};
  vec3_t small_moment = {0.0f, 0.05f, 0.0f};
  int i;

  sim6dof_default_params(&params);
  sim6dof_init_level(&state, 0.0f, 0.0f);
  CHECK(sim6dof_step(&state, &params, &cmd, zero, zero, 0.01f));
  CHECK(state.velocity_ned_mps.x == 0.0f);
  CHECK(state.velocity_ned_mps.z > 0.09f);

  sim6dof_init_level(&state, 0.0f, 0.0f);
  CHECK(sim6dof_step(&state, &params, &cmd, force, zero, 0.10f));
  CHECK(state.velocity_ned_mps.x > 0.3f);

  sim6dof_init_level(&state, 0.0f, 0.0f);
  CHECK(sim6dof_step(&state, &params, &cmd, zero, moment, 0.10f));
  CHECK(state.omega_body_rps.y > 0.1f);

  sim6dof_init_level(&state, 100.0f, 20.0f);
  for (i = 0; i < 1000; ++i) {
    CHECK(sim6dof_step(&state, &params, &cmd, zero, small_moment, 0.01f));
    CHECK(fabsf(quat_norm(state.attitude_body_to_ned) - 1.0f) < 0.001f);
  }

  CHECK(!sim6dof_step(&state, &params, &cmd, zero, zero, 0.0f));
  state.velocity_ned_mps.x = NAN;
  CHECK(!sim6dof_state_is_valid(&state));
  return 0;
}
