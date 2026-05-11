#ifndef ALTAIR_SITL_INITIAL_CONDITIONS_H
#define ALTAIR_SITL_INITIAL_CONDITIONS_H

#include "common_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  real_t lat_deg;
  real_t lon_deg;
  real_t altitude_m;
  real_t roll_rad;
  real_t pitch_rad;
  real_t yaw_rad;
  real_t vel_n_mps;
  real_t vel_e_mps;
  real_t vel_d_mps;
  uint8_t has_velocity_ned;
  real_t p_rps;
  real_t q_rps;
  real_t r_rps;
  real_t airspeed_mps;
  rc_input_t rc;
} sitl_initial_conditions_t;

void sitl_initial_conditions_default(sitl_initial_conditions_t *initial);
int sitl_initial_conditions_load(const char *path, sitl_initial_conditions_t *initial, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
