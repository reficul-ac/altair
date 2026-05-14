#include "altair_vehicle.h"
#include "fsw.h"

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

static void make_valid_input(fsw_input_t *in)
{
    *in = (fsw_input_t){0};
    in->dt_s = 0.01f;
    in->rc.throttle = 0.5f;
    in->rc.roll = 0.0f;
    in->rc.pitch = 0.0f;
    in->rc.yaw = 0.0f;
    in->rc.arm_switch = 1U;
    in->rc.mode_switch = 0U;
    in->imu.gyro_rps.x = 0.0f;
    in->imu.gyro_rps.y = 0.0f;
    in->imu.gyro_rps.z = 0.0f;
    in->imu.accel_mps2.x = 0.0f;
    in->imu.accel_mps2.y = 0.0f;
    in->imu.accel_mps2.z = -9.80665f;
    in->gps.vel_mps.x = 15.0f;
    in->gps.vel_mps.y = 0.0f;
    in->gps.vel_mps.z = 0.0f;
    in->gps.fix_valid = 1U;
    in->baro.altitude_m = 100.0f;
    in->airspeed.true_airspeed_mps = 15.0f;
}

static int check_safe_actuators(const actuator_cmd_t *actuators)
{
    CHECK(actuators->motor == 0.0f);
    CHECK(actuators->aileron == 0.0f);
    CHECK(actuators->elevator == 0.0f);
    CHECK(actuators->rudder == 0.0f);
    return 0;
}

static int check_altair_limits(const actuator_cmd_t *actuators)
{
    CHECK(actuators->motor >= 0.0f && actuators->motor <= 1.0f);
    CHECK(actuators->aileron >= -1.0f && actuators->aileron <= 1.0f);
    CHECK(actuators->elevator >= -1.0f && actuators->elevator <= 1.0f);
    CHECK(actuators->rudder >= -1.0f && actuators->rudder <= 1.0f);
    return 0;
}

static int check_reset_estimate(const state_estimate_t *estimate)
{
    CHECK(estimate->attitude.w == 1.0f);
    CHECK(estimate->attitude.x == 0.0f);
    CHECK(estimate->attitude.y == 0.0f);
    CHECK(estimate->attitude.z == 0.0f);
    CHECK(estimate->euler.roll == 0.0f);
    CHECK(estimate->euler.pitch == 0.0f);
    CHECK(estimate->euler.yaw == 0.0f);
    CHECK(estimate->angular_rate_rps.x == 0.0f);
    CHECK(estimate->angular_rate_rps.y == 0.0f);
    CHECK(estimate->angular_rate_rps.z == 0.0f);
    CHECK(estimate->position_m.x == 0.0f);
    CHECK(estimate->position_m.y == 0.0f);
    CHECK(estimate->position_m.z == 0.0f);
    CHECK(estimate->velocity_mps.x == 0.0f);
    CHECK(estimate->velocity_mps.y == 0.0f);
    CHECK(estimate->velocity_mps.z == 0.0f);
    CHECK(estimate->airspeed_mps == 0.0f);
    return 0;
}

static int step_and_check_mode(fsw_input_t *in, fsw_mode_t expected_mode)
{
    fsw_output_t out;

    bayek_fsw_reset();
    bayek_fsw_step(in, &out);
    CHECK(out.mode == expected_mode);
    CHECK(check_altair_limits(&out.actuators) == 0);
    if (expected_mode == FSW_MODE_DISARMED || expected_mode == FSW_MODE_FAILSAFE)
    {
        CHECK(check_safe_actuators(&out.actuators) == 0);
    }
    return 0;
}

static int step_invalid_and_check_reset_estimate(fsw_input_t *in, fsw_mode_t expected_mode)
{
    fsw_output_t out;

    bayek_fsw_reset();
    bayek_fsw_step(in, &out);
    CHECK(out.mode == expected_mode);
    CHECK(check_safe_actuators(&out.actuators) == 0);
    CHECK(check_reset_estimate(&out.estimate) == 0);
    return 0;
}

static int test_disarmed_uses_safe_actuators(void)
{
    fsw_input_t in;
    fsw_output_t out;

    make_valid_input(&in);

    in.rc.arm_switch = 0U;
    in.rc.throttle = 2.0f;
    in.rc.roll = 2.0f;
    in.rc.pitch = -2.0f;
    in.rc.yaw = 2.0f;
    bayek_fsw_step(&in, &out);
    CHECK(out.mode == FSW_MODE_DISARMED);
    CHECK(check_safe_actuators(&out.actuators) == 0);
    return 0;
}

static int test_null_input_fails_safe(void)
{
    fsw_output_t out;

    bayek_fsw_reset();
    bayek_fsw_step(NULL, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_safe_actuators(&out.actuators) == 0);
    CHECK(check_reset_estimate(&out.estimate) == 0);
    return 0;
}

static int test_null_output_returns(void)
{
    fsw_input_t in;

    make_valid_input(&in);
    bayek_fsw_reset();
    bayek_fsw_step(&in, NULL);
    return 0;
}

static int test_invalid_inputs_do_not_update_estimate(void)
{
    fsw_input_t in;

    make_valid_input(&in);
    in.imu.gyro_rps.z = 1.0f;
    in.dt_s = 0.0f;
    CHECK(step_invalid_and_check_reset_estimate(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.imu.gyro_rps.z = 1.0f;
    in.dt_s = 0.1001f;
    CHECK(step_invalid_and_check_reset_estimate(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.imu.gyro_rps.z = 1.0f;
    in.gps.fix_valid = 0U;
    CHECK(step_invalid_and_check_reset_estimate(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.rc.arm_switch = 0U;
    in.gps.fix_valid = 0U;
    CHECK(step_invalid_and_check_reset_estimate(&in, FSW_MODE_DISARMED) == 0);

    return 0;
}

static int test_manual_mode_requires_valid_inputs(void)
{
    fsw_input_t in;
    fsw_output_t out;

    make_valid_input(&in);

    in.rc.arm_switch = 1U;
    in.rc.mode_switch = 0U;
    in.rc.throttle = 2.0f;
    in.rc.roll = 2.0f;
    in.rc.pitch = -2.0f;
    in.rc.yaw = 2.0f;
    bayek_fsw_step(&in, &out);
    CHECK(out.mode == FSW_MODE_MANUAL);
    CHECK(check_altair_limits(&out.actuators) == 0);
    CHECK(out.actuators.motor == 1.0f);
    CHECK(out.actuators.aileron == 1.0f);
    CHECK(out.actuators.elevator == -1.0f);
    CHECK(out.actuators.rudder == 1.0f);
    return 0;
}

static int test_stabilize_mode_requires_valid_inputs(void)
{
    fsw_input_t in;
    fsw_output_t out;

    make_valid_input(&in);

    in.rc.mode_switch = 1U;
    bayek_fsw_step(&in, &out);
    CHECK(out.mode == FSW_MODE_STABILIZE);
    CHECK(check_altair_limits(&out.actuators) == 0);
    return 0;
}

static int test_mission_mode_requires_loaded_mission(void)
{
    fsw_input_t in;
    fsw_output_t out;
    bayek_mission_plan_t mission = {0};

    make_valid_input(&in);
    in.rc.mode_switch = 2U;

    bayek_fsw_clear_mission();
    bayek_fsw_step(&in, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_safe_actuators(&out.actuators) == 0);

    mission.waypoint_count = 1U;
    mission.waypoints[0].lat_deg = in.gps.lat_deg + 0.001f;
    mission.waypoints[0].lon_deg = in.gps.lon_deg;
    mission.waypoints[0].alt_m = in.gps.alt_m + 10.0f;
    mission.waypoints[0].throttle = 0.50f;
    mission.waypoints[0].acceptance_radius_m = 5.0f;
    bayek_fsw_set_mission(&mission);
    bayek_fsw_step(&in, &out);
    CHECK(out.mode == FSW_MODE_MISSION);
    CHECK(check_altair_limits(&out.actuators) == 0);
    bayek_fsw_clear_mission();
    return 0;
}

static int test_mode_boundaries(void)
{
    fsw_input_t in;

    make_valid_input(&in);
    CHECK(step_and_check_mode(&in, FSW_MODE_MANUAL) == 0);

    make_valid_input(&in);
    in.rc.mode_switch = 1U;
    CHECK(step_and_check_mode(&in, FSW_MODE_STABILIZE) == 0);

    make_valid_input(&in);
    in.rc.mode_switch = 2U;
    CHECK(step_and_check_mode(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.rc.arm_switch = 0U;
    CHECK(step_and_check_mode(&in, FSW_MODE_DISARMED) == 0);

    make_valid_input(&in);
    in.gps.fix_valid = 0U;
    CHECK(step_and_check_mode(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.dt_s = 0.0f;
    CHECK(step_and_check_mode(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.dt_s = -0.01f;
    CHECK(step_and_check_mode(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.dt_s = 0.1f;
    CHECK(step_and_check_mode(&in, FSW_MODE_MANUAL) == 0);

    make_valid_input(&in);
    in.rc.mode_switch = 3U;
    CHECK(step_and_check_mode(&in, FSW_MODE_FAILSAFE) == 0);

    make_valid_input(&in);
    in.dt_s = 0.1001f;
    CHECK(step_and_check_mode(&in, FSW_MODE_FAILSAFE) == 0);

    return 0;
}

int main(void)
{
    bayek_fsw_init(altair_vehicle_interface());

    CHECK(test_disarmed_uses_safe_actuators() == 0);
    CHECK(test_null_input_fails_safe() == 0);
    CHECK(test_null_output_returns() == 0);
    CHECK(test_invalid_inputs_do_not_update_estimate() == 0);
    CHECK(test_manual_mode_requires_valid_inputs() == 0);
    CHECK(test_stabilize_mode_requires_valid_inputs() == 0);
    CHECK(test_mission_mode_requires_loaded_mission() == 0);
    CHECK(test_mode_boundaries() == 0);

    return 0;
}
