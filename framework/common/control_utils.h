#ifndef ALTAIR_CONTROL_UTILS_H
#define ALTAIR_CONTROL_UTILS_H

#include "common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  real_t kp;
  real_t ki;
  real_t kd;
  real_t integrator;
  real_t prev_error;
  real_t out_min;
  real_t out_max;
  real_t integrator_min;
  real_t integrator_max;
  uint8_t initialized;
} pid_t;

typedef struct {
  real_t value;
  real_t min_value;
  real_t max_value;
} integrator_t;

typedef struct {
  real_t alpha;
  real_t state;
  uint8_t initialized;
} lowpass_filter_t;

typedef struct {
  real_t prev;
  real_t rate_limit_per_s;
  uint8_t initialized;
} rate_limiter_t;

void pid_init(pid_t *pid, real_t kp, real_t ki, real_t kd, real_t out_min, real_t out_max);
void pid_reset(pid_t *pid);
real_t pid_step(pid_t *pid, real_t setpoint, real_t measurement, real_t dt_s);

void integrator_init(integrator_t *i, real_t min_value, real_t max_value);
void integrator_reset(integrator_t *i, real_t value);
real_t integrator_step(integrator_t *i, real_t input, real_t dt_s);

void lowpass_init(lowpass_filter_t *f, real_t alpha);
void lowpass_reset(lowpass_filter_t *f);
real_t lowpass_step(lowpass_filter_t *f, real_t input);

void rate_limiter_init(rate_limiter_t *r, real_t rate_limit_per_s);
void rate_limiter_reset(rate_limiter_t *r, real_t value);
real_t rate_limiter_step(rate_limiter_t *r, real_t target, real_t dt_s);

real_t slew_limit(real_t current, real_t target, real_t max_delta);
real_t apply_deadband(real_t value, real_t deadband);
real_t saturate_actuator(real_t value, real_t min_value, real_t max_value);

#ifdef __cplusplus
}
#endif

#endif
