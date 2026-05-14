#include "altair_mixer.h"
#include "altair_params.h"

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

static int check_actuators(
    const actuator_cmd_t *cmd, real_t motor, real_t aileron, real_t elevator, real_t rudder)
{
    CHECK(near_real(cmd->motor, motor, 1.0e-6f));
    CHECK(near_real(cmd->aileron, aileron, 1.0e-6f));
    CHECK(near_real(cmd->elevator, elevator, 1.0e-6f));
    CHECK(near_real(cmd->rudder, rudder, 1.0e-6f));
    return 0;
}

static int test_manual_passthrough(void)
{
    rc_input_t rc = {0.6f, -0.25f, 0.5f, -0.75f, 1U, 0U};
    actuator_cmd_t cmd = altair_mix_manual(&rc);

    CHECK(check_actuators(&cmd, 0.6f, -0.25f, 0.5f, -0.75f) == 0);
    return 0;
}

static int test_manual_saturation(void)
{
    rc_input_t rc = {2.0f, -2.0f, 0.5f, 3.0f, 1U, 0U};
    actuator_cmd_t cmd = altair_mix_manual(&rc);

    CHECK(check_actuators(&cmd, 1.0f, -1.0f, 0.5f, 1.0f) == 0);
    return 0;
}

static int test_control_saturation(void)
{
    actuator_cmd_t zero = altair_mix_control(0.0f, 0.0f, 0.0f, 0.0f);
    actuator_cmd_t min = altair_mix_control(-1.0f, -1.0f, -1.0f, -1.0f);
    actuator_cmd_t max = altair_mix_control(1.0f, 1.0f, 1.0f, 1.0f);
    actuator_cmd_t high = altair_mix_control(5.0f, 5.0f, 5.0f, 5.0f);
    actuator_cmd_t low = altair_mix_control(-5.0f, -5.0f, -5.0f, -5.0f);

    CHECK(check_actuators(&zero, 0.0f, 0.0f, 0.0f, 0.0f) == 0);
    CHECK(check_actuators(&min, 0.0f, -1.0f, -1.0f, -1.0f) == 0);
    CHECK(check_actuators(&max, 1.0f, 1.0f, 1.0f, 1.0f) == 0);
    CHECK(check_actuators(&high, 1.0f, 1.0f, 1.0f, 1.0f) == 0);
    CHECK(check_actuators(&low, 0.0f, -1.0f, -1.0f, -1.0f) == 0);
    return 0;
}

static int test_safe_actuators(void)
{
    actuator_cmd_t safe = altair_safe_actuators(altair_default_params());
    actuator_cmd_t fallback = altair_safe_actuators(0);

    CHECK(check_actuators(&safe, 0.0f, 0.0f, 0.0f, 0.0f) == 0);
    CHECK(check_actuators(&fallback, 0.0f, 0.0f, 0.0f, 0.0f) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_manual_passthrough() == 0);
    CHECK(test_manual_saturation() == 0);
    CHECK(test_control_saturation() == 0);
    CHECK(test_safe_actuators() == 0);
    return 0;
}
