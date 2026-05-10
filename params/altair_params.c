#include "altair_params.h"

#include "altair_limits.h"

static const vehicle_params_t k_altair_params = {
  2.5f,
  0.45f,
  35.0f,
  8.0f,
  0.7853982f,
  0.3490658f,
  1.5707963f,
  ALTAIR_SURFACE_MAX,
  ALTAIR_SURFACE_MIN,
  0.0f,
  0.0f
};

const vehicle_params_t *altair_default_params(void) {
  return &k_altair_params;
}
