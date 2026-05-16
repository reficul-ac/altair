#include "math_utils.h"

#include <math.h>
#include <stdio.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d\n", __FILE__, __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int near_real(real_t a, real_t b, real_t eps)
{
    return fabsf(a - b) <= eps;
}

int main(void)
{
    vec3_t a = {1.0f, 2.0f, 3.0f};
    vec3_t b = {4.0f, -1.0f, 2.0f};
    vec3_t c = vec3_add(a, b);
    vec3_t x_axis = {1.0f, 0.0f, 0.0f};
    vec3_t y_axis = {0.0f, 1.0f, 0.0f};
    vec3_t z_axis = vec3_cross(x_axis, y_axis);
    euler_t e = {0.1f, -0.2f, 0.3f};
    quat_t q = quat_from_euler(e);
    euler_t out = euler_from_quat(q);
    vec3_t rotated;

    CHECK(near_real(c.x, 5.0f, 1.0e-6f));
    CHECK(near_real(c.y, 1.0f, 1.0e-6f));
    CHECK(near_real(c.z, 5.0f, 1.0e-6f));
    CHECK(near_real(vec3_dot(a, b), 8.0f, 1.0e-6f));
    CHECK(near_real(vec3_norm(a), sqrtf(14.0f), 1.0e-6f));
    CHECK(near_real(z_axis.x, 0.0f, 1.0e-6f));
    CHECK(near_real(z_axis.y, 0.0f, 1.0e-6f));
    CHECK(near_real(z_axis.z, 1.0f, 1.0e-6f));
    CHECK(near_real(out.roll, e.roll, 1.0e-5f));
    CHECK(near_real(out.pitch, e.pitch, 1.0e-5f));
    CHECK(near_real(out.yaw, e.yaw, 1.0e-5f));
    rotated = quat_rotate_vec3(quat_from_euler((euler_t){0.0f, 0.0f, BAYEK_PI * 0.5f}), x_axis);
    CHECK(near_real(rotated.x, 0.0f, 1.0e-5f));
    CHECK(near_real(rotated.y, 1.0f, 1.0e-5f));
    CHECK(near_real(wrap_pi(4.0f), 4.0f - BAYEK_TWO_PI, 1.0e-6f));
    CHECK(near_real(clamp_real(2.0f, -1.0f, 1.0f), 1.0f, 1.0e-6f));
    CHECK(near_real(lerp_real(2.0f, 4.0f, 0.25f), 2.5f, 1.0e-6f));
    return 0;
}
