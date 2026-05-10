#include "altair_mixer.h"
#include "altair_params.h"

#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

static int near_real(real_t a, real_t b, real_t eps) {
  return fabsf(a - b) <= eps;
}

int main(void) {
  rc_input_t rc = {2.0f, -2.0f, 0.5f, 3.0f, 1U, 0U};
  actuator_cmd_t cmd = altair_mix_manual(&rc);
  actuator_cmd_t safe = altair_safe_actuators(altair_default_params());
  CHECK(near_real(cmd.motor, 1.0f, 1.0e-6f));
  CHECK(near_real(cmd.aileron, -1.0f, 1.0e-6f));
  CHECK(near_real(cmd.elevator, 0.5f, 1.0e-6f));
  CHECK(near_real(cmd.rudder, 1.0f, 1.0e-6f));
  CHECK(near_real(safe.motor, 0.0f, 1.0e-6f));
  CHECK(near_real(safe.aileron, 0.0f, 1.0e-6f));
  return 0;
}
