#include "control_utils.h"

#include "math_utils.h"

void pid_init(pid_t *pid, real_t kp, real_t ki, real_t kd, real_t out_min, real_t out_max) {
  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->integrator = 0.0f;
  pid->prev_error = 0.0f;
  pid->out_min = out_min;
  pid->out_max = out_max;
  pid->integrator_min = out_min;
  pid->integrator_max = out_max;
  pid->initialized = 0U;
}

void pid_reset(pid_t *pid) {
  pid->integrator = 0.0f;
  pid->prev_error = 0.0f;
  pid->initialized = 0U;
}

real_t pid_step(pid_t *pid, real_t setpoint, real_t measurement, real_t dt_s) {
  real_t error = setpoint - measurement;
  real_t derivative = 0.0f;
  if (pid->initialized && dt_s > 0.0f) {
    derivative = (error - pid->prev_error) / dt_s;
  }
  if (dt_s > 0.0f) {
    pid->integrator += error * pid->ki * dt_s;
    pid->integrator = clamp_real(pid->integrator, pid->integrator_min, pid->integrator_max);
  }
  pid->prev_error = error;
  pid->initialized = 1U;
  return clamp_real(pid->kp * error + pid->integrator + pid->kd * derivative, pid->out_min, pid->out_max);
}

void integrator_init(integrator_t *i, real_t min_value, real_t max_value) {
  i->value = 0.0f;
  i->min_value = min_value;
  i->max_value = max_value;
}

void integrator_reset(integrator_t *i, real_t value) {
  i->value = clamp_real(value, i->min_value, i->max_value);
}

real_t integrator_step(integrator_t *i, real_t input, real_t dt_s) {
  if (dt_s > 0.0f) {
    i->value = clamp_real(i->value + input * dt_s, i->min_value, i->max_value);
  }
  return i->value;
}

void lowpass_init(lowpass_filter_t *f, real_t alpha) {
  f->alpha = clamp_real(alpha, 0.0f, 1.0f);
  f->state = 0.0f;
  f->initialized = 0U;
}

void lowpass_reset(lowpass_filter_t *f) {
  f->state = 0.0f;
  f->initialized = 0U;
}

real_t lowpass_step(lowpass_filter_t *f, real_t input) {
  if (!f->initialized) {
    f->state = input;
    f->initialized = 1U;
  } else {
    f->state = f->alpha * input + (1.0f - f->alpha) * f->state;
  }
  return f->state;
}

void rate_limiter_init(rate_limiter_t *r, real_t rate_limit_per_s) {
  r->prev = 0.0f;
  r->rate_limit_per_s = rate_limit_per_s;
  r->initialized = 0U;
}

void rate_limiter_reset(rate_limiter_t *r, real_t value) {
  r->prev = value;
  r->initialized = 1U;
}

real_t rate_limiter_step(rate_limiter_t *r, real_t target, real_t dt_s) {
  if (!r->initialized) {
    r->prev = target;
    r->initialized = 1U;
    return target;
  }
  r->prev = slew_limit(r->prev, target, r->rate_limit_per_s * dt_s);
  return r->prev;
}

real_t slew_limit(real_t current, real_t target, real_t max_delta) {
  real_t delta = target - current;
  max_delta = max_delta < 0.0f ? -max_delta : max_delta;
  delta = clamp_real(delta, -max_delta, max_delta);
  return current + delta;
}

real_t apply_deadband(real_t value, real_t deadband) {
  real_t abs_value = value < 0.0f ? -value : value;
  if (abs_value <= deadband) {
    return 0.0f;
  }
  return value > 0.0f ? value - deadband : value + deadband;
}

real_t saturate_actuator(real_t value, real_t min_value, real_t max_value) {
  return clamp_real(value, min_value, max_value);
}
