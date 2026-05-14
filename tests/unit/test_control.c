#include "control_utils.h"

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
    pid_t pid;
    integrator_t integ;
    lowpass_filter_t lp;
    rate_limiter_t rl;
    real_t y;

    pid_init(&pid, 10.0f, 5.0f, 0.0f, -0.5f, 0.5f);
    y = pid_step(&pid, 1.0f, 0.0f, 0.1f);
    CHECK(near_real(y, 0.5f, 1.0e-6f));
    CHECK(pid.integrator <= pid.integrator_max);

    integrator_init(&integ, -1.0f, 1.0f);
    CHECK(near_real(integrator_step(&integ, 20.0f, 1.0f), 1.0f, 1.0e-6f));
    integrator_reset(&integ, -2.0f);
    CHECK(near_real(integ.value, -1.0f, 1.0e-6f));

    lowpass_init(&lp, 0.5f);
    CHECK(near_real(lowpass_step(&lp, 10.0f), 10.0f, 1.0e-6f));
    CHECK(near_real(lowpass_step(&lp, 0.0f), 5.0f, 1.0e-6f));

    rate_limiter_init(&rl, 2.0f);
    rate_limiter_reset(&rl, 0.0f);
    CHECK(near_real(rate_limiter_step(&rl, 10.0f, 0.25f), 0.5f, 1.0e-6f));
    CHECK(near_real(slew_limit(0.0f, -2.0f, 0.25f), -0.25f, 1.0e-6f));
    CHECK(near_real(apply_deadband(0.05f, 0.1f), 0.0f, 1.0e-6f));
    CHECK(near_real(apply_deadband(0.2f, 0.1f), 0.1f, 1.0e-6f));
    CHECK(near_real(saturate_actuator(2.0f, -1.0f, 1.0f), 1.0f, 1.0e-6f));
    return 0;
}
