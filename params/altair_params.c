#include "altair_params.h"

#include "altair_limits.h"

static const vehicle_params_t k_altair_params = {
  .max_airspeed_mps = 35.0f,
  .min_airspeed_mps = 8.0f,
  .max_roll_rad = 0.7853982f,
  .max_pitch_rad = 0.3490658f,
  .max_yaw_rate_rps = 1.5707963f,
  .max_actuator = ALTAIR_SURFACE_MAX,
  .min_actuator = ALTAIR_SURFACE_MIN,
  .safe_motor = 0.0f,
  .safe_surface = 0.0f
};

const vehicle_params_t *altair_default_params(void) {
  return &k_altair_params;
}
