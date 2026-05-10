#ifndef ALTAIR_MATH_UTILS_H
#define ALTAIR_MATH_UTILS_H

#include "common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALTAIR_PI ((real_t)3.14159265358979323846f)
#define ALTAIR_TWO_PI ((real_t)6.28318530717958647692f)

vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_scale(vec3_t v, real_t s);
real_t vec3_dot(vec3_t a, vec3_t b);
vec3_t vec3_cross(vec3_t a, vec3_t b);
real_t vec3_norm(vec3_t v);
vec3_t vec3_normalize(vec3_t v);

quat_t quat_identity(void);
quat_t quat_normalize(quat_t q);
quat_t quat_multiply(quat_t a, quat_t b);
vec3_t quat_rotate_vec3(quat_t q, vec3_t v);
quat_t quat_from_euler(euler_t e);
euler_t euler_from_quat(quat_t q);

real_t wrap_pi(real_t angle_rad);
real_t clamp_real(real_t value, real_t min_value, real_t max_value);
real_t lerp_real(real_t a, real_t b, real_t t);
int real_is_finite(real_t value);

#ifdef __cplusplus
}
#endif

#endif
