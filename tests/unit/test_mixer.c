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

static int test_manual_boundaries(void)
{
    rc_input_t min = {0.0f, -1.0f, -1.0f, -1.0f, 1U, 0U};
    rc_input_t max = {1.0f, 1.0f, 1.0f, 1.0f, 1U, 0U};
    actuator_cmd_t min_cmd = altair_mix_manual(&min);
    actuator_cmd_t max_cmd = altair_mix_manual(&max);

    CHECK(check_actuators(&min_cmd, 0.0f, -1.0f, -1.0f, -1.0f) == 0);
    CHECK(check_actuators(&max_cmd, 1.0f, 1.0f, 1.0f, 1.0f) == 0);
    return 0;
}

static int test_manual_saturation(void)
{
    rc_input_t rc = {2.0f, -2.0f, 0.5f, 3.0f, 1U, 0U};
    actuator_cmd_t cmd = altair_mix_manual(&rc);

    CHECK(check_actuators(&cmd, 1.0f, -1.0f, 0.5f, 1.0f) == 0);
    return 0;
}

static int test_manual_independent_saturation(void)
{
    rc_input_t throttle_high = {2.0f, 0.2f, -0.3f, 0.4f, 1U, 0U};
    rc_input_t throttle_low = {-2.0f, 0.2f, -0.3f, 0.4f, 1U, 0U};
    rc_input_t roll_high = {0.5f, 2.0f, -0.3f, 0.4f, 1U, 0U};
    rc_input_t roll_low = {0.5f, -2.0f, -0.3f, 0.4f, 1U, 0U};
    rc_input_t pitch_high = {0.5f, 0.2f, 2.0f, 0.4f, 1U, 0U};
    rc_input_t pitch_low = {0.5f, 0.2f, -2.0f, 0.4f, 1U, 0U};
    rc_input_t yaw_high = {0.5f, 0.2f, -0.3f, 2.0f, 1U, 0U};
    rc_input_t yaw_low = {0.5f, 0.2f, -0.3f, -2.0f, 1U, 0U};
    actuator_cmd_t throttle_high_cmd = altair_mix_manual(&throttle_high);
    actuator_cmd_t throttle_low_cmd = altair_mix_manual(&throttle_low);
    actuator_cmd_t roll_high_cmd = altair_mix_manual(&roll_high);
    actuator_cmd_t roll_low_cmd = altair_mix_manual(&roll_low);
    actuator_cmd_t pitch_high_cmd = altair_mix_manual(&pitch_high);
    actuator_cmd_t pitch_low_cmd = altair_mix_manual(&pitch_low);
    actuator_cmd_t yaw_high_cmd = altair_mix_manual(&yaw_high);
    actuator_cmd_t yaw_low_cmd = altair_mix_manual(&yaw_low);

    CHECK(check_actuators(&throttle_high_cmd, 1.0f, 0.2f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&throttle_low_cmd, 0.0f, 0.2f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&roll_high_cmd, 0.5f, 1.0f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&roll_low_cmd, 0.5f, -1.0f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&pitch_high_cmd, 0.5f, 0.2f, 1.0f, 0.4f) == 0);
    CHECK(check_actuators(&pitch_low_cmd, 0.5f, 0.2f, -1.0f, 0.4f) == 0);
    CHECK(check_actuators(&yaw_high_cmd, 0.5f, 0.2f, -0.3f, 1.0f) == 0);
    CHECK(check_actuators(&yaw_low_cmd, 0.5f, 0.2f, -0.3f, -1.0f) == 0);
    return 0;
}

static int test_control_sign_passthrough(void)
{
    actuator_cmd_t positive = altair_mix_control(0.5f, 0.25f, 0.5f, 0.75f);
    actuator_cmd_t negative = altair_mix_control(0.5f, -0.25f, -0.5f, -0.75f);

    CHECK(check_actuators(&positive, 0.5f, 0.25f, 0.5f, 0.75f) == 0);
    CHECK(check_actuators(&negative, 0.5f, -0.25f, -0.5f, -0.75f) == 0);
    return 0;
}

static int test_control_boundaries(void)
{
    actuator_cmd_t min = altair_mix_control(0.0f, -1.0f, -1.0f, -1.0f);
    actuator_cmd_t max = altair_mix_control(1.0f, 1.0f, 1.0f, 1.0f);

    CHECK(check_actuators(&min, 0.0f, -1.0f, -1.0f, -1.0f) == 0);
    CHECK(check_actuators(&max, 1.0f, 1.0f, 1.0f, 1.0f) == 0);
    return 0;
}

static int test_control_independent_saturation(void)
{
    actuator_cmd_t throttle_high = altair_mix_control(2.0f, 0.2f, -0.3f, 0.4f);
    actuator_cmd_t throttle_low = altair_mix_control(-2.0f, 0.2f, -0.3f, 0.4f);
    actuator_cmd_t roll_high = altair_mix_control(0.5f, 2.0f, -0.3f, 0.4f);
    actuator_cmd_t roll_low = altair_mix_control(0.5f, -2.0f, -0.3f, 0.4f);
    actuator_cmd_t pitch_high = altair_mix_control(0.5f, 0.2f, 2.0f, 0.4f);
    actuator_cmd_t pitch_low = altair_mix_control(0.5f, 0.2f, -2.0f, 0.4f);
    actuator_cmd_t yaw_high = altair_mix_control(0.5f, 0.2f, -0.3f, 2.0f);
    actuator_cmd_t yaw_low = altair_mix_control(0.5f, 0.2f, -0.3f, -2.0f);

    CHECK(check_actuators(&throttle_high, 1.0f, 0.2f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&throttle_low, 0.0f, 0.2f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&roll_high, 0.5f, 1.0f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&roll_low, 0.5f, -1.0f, -0.3f, 0.4f) == 0);
    CHECK(check_actuators(&pitch_high, 0.5f, 0.2f, 1.0f, 0.4f) == 0);
    CHECK(check_actuators(&pitch_low, 0.5f, 0.2f, -1.0f, 0.4f) == 0);
    CHECK(check_actuators(&yaw_high, 0.5f, 0.2f, -0.3f, 1.0f) == 0);
    CHECK(check_actuators(&yaw_low, 0.5f, 0.2f, -0.3f, -1.0f) == 0);
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

static int test_safe_actuators_clamp_out_of_range_params(void)
{
    vehicle_params_t high;
    vehicle_params_t low;

    altair_vehicle_params_default(&high);
    altair_vehicle_params_default(&low);
    high.safe_motor = 2.0f;
    high.safe_surface = 2.0f;
    low.safe_motor = -2.0f;
    low.safe_surface = -2.0f;
    actuator_cmd_t high_cmd = altair_safe_actuators(&high);
    actuator_cmd_t low_cmd = altair_safe_actuators(&low);

    CHECK(check_actuators(&high_cmd, 1.0f, 1.0f, 1.0f, 1.0f) == 0);
    CHECK(check_actuators(&low_cmd, 0.0f, -1.0f, -1.0f, -1.0f) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_manual_passthrough() == 0);
    CHECK(test_manual_boundaries() == 0);
    CHECK(test_manual_saturation() == 0);
    CHECK(test_manual_independent_saturation() == 0);
    CHECK(test_control_sign_passthrough() == 0);
    CHECK(test_control_boundaries() == 0);
    CHECK(test_control_independent_saturation() == 0);
    CHECK(test_control_saturation() == 0);
    CHECK(test_safe_actuators() == 0);
    CHECK(test_safe_actuators_clamp_out_of_range_params() == 0);
    return 0;
}
