#include "math_utils.h"

#include <math.h>

vec3_t vec3_add(vec3_t a, vec3_t b) {
  vec3_t r = {a.x + b.x, a.y + b.y, a.z + b.z};
  return r;
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
  vec3_t r = {a.x - b.x, a.y - b.y, a.z - b.z};
  return r;
}

vec3_t vec3_scale(vec3_t v, real_t s) {
  vec3_t r = {v.x * s, v.y * s, v.z * s};
  return r;
}

real_t vec3_dot(vec3_t a, vec3_t b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
  vec3_t r = {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x
  };
  return r;
}

real_t vec3_norm(vec3_t v) {
  return (real_t)sqrtf(vec3_dot(v, v));
}

vec3_t vec3_normalize(vec3_t v) {
  real_t n = vec3_norm(v);
  if (n <= (real_t)1.0e-6f) {
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    return zero;
  }
  return vec3_scale(v, (real_t)1.0f / n);
}

quat_t quat_identity(void) {
  quat_t q = {1.0f, 0.0f, 0.0f, 0.0f};
  return q;
}

quat_t quat_normalize(quat_t q) {
  real_t n = (real_t)sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (n <= (real_t)1.0e-6f) {
    return quat_identity();
  }
  q.w /= n;
  q.x /= n;
  q.y /= n;
  q.z /= n;
  return q;
}

quat_t quat_multiply(quat_t a, quat_t b) {
  quat_t q = {
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
  };
  return q;
}

vec3_t quat_rotate_vec3(quat_t q, vec3_t v) {
  q = quat_normalize(q);
  quat_t p = {0.0f, v.x, v.y, v.z};
  quat_t qc = {q.w, -q.x, -q.y, -q.z};
  quat_t r = quat_multiply(quat_multiply(q, p), qc);
  vec3_t out = {r.x, r.y, r.z};
  return out;
}

quat_t quat_from_euler(euler_t e) {
  real_t cr = (real_t)cosf(e.roll * 0.5f);
  real_t sr = (real_t)sinf(e.roll * 0.5f);
  real_t cp = (real_t)cosf(e.pitch * 0.5f);
  real_t sp = (real_t)sinf(e.pitch * 0.5f);
  real_t cy = (real_t)cosf(e.yaw * 0.5f);
  real_t sy = (real_t)sinf(e.yaw * 0.5f);
  quat_t q = {
    cr * cp * cy + sr * sp * sy,
    sr * cp * cy - cr * sp * sy,
    cr * sp * cy + sr * cp * sy,
    cr * cp * sy - sr * sp * cy
  };
  return quat_normalize(q);
}

euler_t euler_from_quat(quat_t q) {
  q = quat_normalize(q);
  euler_t e;
  real_t sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
  real_t cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
  e.roll = (real_t)atan2f(sinr_cosp, cosr_cosp);

  real_t sinp = 2.0f * (q.w * q.y - q.z * q.x);
  if (sinp >= 1.0f) {
    e.pitch = ALTAIR_PI * 0.5f;
  } else if (sinp <= -1.0f) {
    e.pitch = -ALTAIR_PI * 0.5f;
  } else {
    e.pitch = (real_t)asinf(sinp);
  }

  real_t siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
  real_t cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
  e.yaw = (real_t)atan2f(siny_cosp, cosy_cosp);
  return e;
}

real_t wrap_pi(real_t angle_rad) {
  while (angle_rad > ALTAIR_PI) {
    angle_rad -= ALTAIR_TWO_PI;
  }
  while (angle_rad < -ALTAIR_PI) {
    angle_rad += ALTAIR_TWO_PI;
  }
  return angle_rad;
}

real_t clamp_real(real_t value, real_t min_value, real_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

real_t lerp_real(real_t a, real_t b, real_t t) {
  return a + (b - a) * t;
}

int real_is_finite(real_t value) {
  return isfinite(value) ? 1 : 0;
}
