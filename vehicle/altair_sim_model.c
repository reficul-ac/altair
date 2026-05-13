#include "altair_sim_model.h"

#include "altair_mixer.h"
#include "math_utils.h"

#include <math.h>
#include <stdio.h>

static int fail_validation(char *error, size_t error_size, const char *message) {
  if (error != NULL && error_size > 0U) {
    (void)snprintf(error, error_size, "%s", message);
  }
  return 0;
}

static int close_enough(real_t a, real_t b) {
  return fabsf(a - b) <= 1.0e-6f;
}

static int vec3_is_positive(vec3_t v) {
  return real_is_finite(v.x) && real_is_finite(v.y) && real_is_finite(v.z) &&
         v.x > 0.0f && v.y > 0.0f && v.z > 0.0f;
}

void altair_fixedwing_sim_params(sim_fixedwing_params_t *params) {
  const vehicle_params_t *vehicle = altair_default_params();
  if (params == NULL) {
    return;
  }

  sim_fixedwing_default_params(params);
  params->core.mass_kg = vehicle->mass_kg;
  params->wing_area_m2 = vehicle->wing_area_m2;
}

int altair_fixedwing_sim_params_are_valid(const sim_fixedwing_params_t *params,
                                          const vehicle_params_t *vehicle,
                                          char *error,
                                          size_t error_size) {
  actuator_cmd_t safe;

  if (params == NULL) {
    return fail_validation(error, error_size, "sim params are null");
  }
  if (vehicle == NULL) {
    return fail_validation(error, error_size, "vehicle params are null");
  }

  if (!real_is_finite(vehicle->mass_kg) || vehicle->mass_kg <= 0.0f) {
    return fail_validation(error, error_size, "vehicle mass must be finite and positive");
  }
  if (!real_is_finite(vehicle->wing_area_m2) || vehicle->wing_area_m2 <= 0.0f) {
    return fail_validation(error, error_size, "vehicle wing area must be finite and positive");
  }
  if (!real_is_finite(vehicle->min_airspeed_mps) ||
      !real_is_finite(vehicle->max_airspeed_mps) ||
      vehicle->min_airspeed_mps <= 0.0f ||
      vehicle->max_airspeed_mps <= vehicle->min_airspeed_mps ||
      vehicle->max_airspeed_mps > 120.0f) {
    return fail_validation(error, error_size, "vehicle airspeed limits are inconsistent");
  }
  if (!real_is_finite(vehicle->min_actuator) ||
      !real_is_finite(vehicle->max_actuator) ||
      vehicle->min_actuator < -1.0f ||
      vehicle->max_actuator > 1.0f ||
      vehicle->min_actuator >= vehicle->max_actuator) {
    return fail_validation(error, error_size, "vehicle actuator range must fit normalized sim outputs");
  }

  if (!close_enough(params->core.mass_kg, vehicle->mass_kg)) {
    return fail_validation(error, error_size, "sim mass does not match Altair vehicle mass");
  }
  if (!close_enough(params->wing_area_m2, vehicle->wing_area_m2)) {
    return fail_validation(error, error_size, "sim wing area does not match Altair vehicle wing area");
  }
  if (!vec3_is_positive(params->core.inertia_kgm2)) {
    return fail_validation(error, error_size, "sim inertia must be finite and positive");
  }
  if (!real_is_finite(params->core.gravity_mps2) || params->core.gravity_mps2 <= 0.0f) {
    return fail_validation(error, error_size, "sim gravity must be finite and positive");
  }
  if (!real_is_finite(params->core.air_density_kgpm3) || params->core.air_density_kgpm3 <= 0.0f) {
    return fail_validation(error, error_size, "sim air density must be finite and positive");
  }
  if (!real_is_finite(params->core.actuator_lag_hz) || params->core.actuator_lag_hz <= 0.0f) {
    return fail_validation(error, error_size, "sim actuator lag must be finite and positive");
  }
  if (params->core.frame_mode != SIM6DOF_FRAME_NED && params->core.frame_mode != SIM6DOF_FRAME_ECEF) {
    return fail_validation(error, error_size, "sim frame mode is invalid");
  }
  if (params->core.earth_model != SIM6DOF_EARTH_SPHERICAL) {
    return fail_validation(error, error_size, "sim earth model is invalid");
  }
  if (!real_is_finite(params->core.earth_radius_m) || params->core.earth_radius_m <= 1000.0f) {
    return fail_validation(error, error_size, "sim earth radius must be finite and positive");
  }
  if (!real_is_finite(params->wing_span_m) || params->wing_span_m <= 0.0f ||
      !real_is_finite(params->mean_chord_m) || params->mean_chord_m <= 0.0f ||
      !real_is_finite(params->max_thrust_n) || params->max_thrust_n <= 0.0f) {
    return fail_validation(error, error_size, "sim geometry and thrust must be finite and positive");
  }
  if (!real_is_finite(params->drag_cd0) || params->drag_cd0 < 0.0f ||
      !real_is_finite(params->drag_cd_alpha) || params->drag_cd_alpha < 0.0f ||
      !real_is_finite(params->lift_cl0) ||
      !real_is_finite(params->lift_cl_alpha) ||
      !real_is_finite(params->lift_cl_elevator) ||
      !real_is_finite(params->stall_alpha_rad) || params->stall_alpha_rad <= 0.0f) {
    return fail_validation(error, error_size, "sim aerodynamic coefficients are invalid");
  }
  if (!real_is_finite(params->roll_aileron_nm) ||
      !real_is_finite(params->pitch_elevator_nm) ||
      !real_is_finite(params->yaw_rudder_nm) ||
      !real_is_finite(params->rate_damping_nms.x) ||
      !real_is_finite(params->rate_damping_nms.y) ||
      !real_is_finite(params->rate_damping_nms.z)) {
    return fail_validation(error, error_size, "sim control moments or damping are non-finite");
  }

  safe = altair_safe_actuators(vehicle);
  if (safe.motor < 0.0f || safe.motor > 1.0f ||
      safe.aileron < vehicle->min_actuator || safe.aileron > vehicle->max_actuator ||
      safe.elevator < vehicle->min_actuator || safe.elevator > vehicle->max_actuator ||
      safe.rudder < vehicle->min_actuator || safe.rudder > vehicle->max_actuator) {
    return fail_validation(error, error_size, "safe actuator outputs violate vehicle/sim ranges");
  }

  if (error != NULL && error_size > 0U) {
    error[0] = '\0';
  }
  return 1;
}
