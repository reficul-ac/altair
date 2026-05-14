#include "altair_fsw.h"

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

typedef struct
{
    unsigned int manual_calls;
    unsigned int control_calls;
    unsigned int safe_calls;
    const rc_input_t *last_manual_rc;
    const vehicle_params_t *last_safe_params;
    real_t last_throttle;
    real_t last_roll;
    real_t last_pitch;
    real_t last_yaw;
} mock_vehicle_state_t;

static const vehicle_params_t params = {40.0f, 5.0f, 0.50f, 0.30f, 0.80f, 1.0f, -1.0f, 0.0f, 0.0f};
static mock_vehicle_state_t mock;
static altair_fsw_t fsw;

static actuator_cmd_t mix_manual(const rc_input_t *rc)
{
    actuator_cmd_t cmd;

    ++mock.manual_calls;
    mock.last_manual_rc = rc;
    cmd.motor = rc ? rc->throttle : -1.0f;
    cmd.aileron = rc ? rc->roll : -1.0f;
    cmd.elevator = rc ? rc->pitch : -1.0f;
    cmd.rudder = rc ? rc->yaw : -1.0f;
    return cmd;
}

static actuator_cmd_t
mix_control(real_t throttle, real_t roll_cmd, real_t pitch_cmd, real_t yaw_cmd)
{
    actuator_cmd_t cmd;

    ++mock.control_calls;
    mock.last_throttle = throttle;
    mock.last_roll = roll_cmd;
    mock.last_pitch = pitch_cmd;
    mock.last_yaw = yaw_cmd;
    cmd.motor = throttle;
    cmd.aileron = roll_cmd;
    cmd.elevator = pitch_cmd;
    cmd.rudder = yaw_cmd;
    return cmd;
}

static actuator_cmd_t safe_actuators(const vehicle_params_t *safe_params)
{
    actuator_cmd_t cmd;

    ++mock.safe_calls;
    mock.last_safe_params = safe_params;
    cmd.motor = -0.25f;
    cmd.aileron = -0.50f;
    cmd.elevator = -0.75f;
    cmd.rudder = -1.00f;
    return cmd;
}

static const bayek_vehicle_interface_t vehicle = {&params, mix_manual, mix_control, safe_actuators};

static void reset_mock(void)
{
    mock = (mock_vehicle_state_t){0};
}

static void init_mock_fsw(void)
{
    reset_mock();
    altair_fsw_init(&fsw, &vehicle);
    altair_fsw_clear_mission(&fsw);
}

static void make_valid_input(fsw_input_t *in)
{
    *in = (fsw_input_t){0};
    in->dt_s = 0.01f;
    in->rc.throttle = 0.45f;
    in->rc.roll = 0.20f;
    in->rc.pitch = -0.10f;
    in->rc.yaw = 0.05f;
    in->rc.arm_switch = 1U;
    in->rc.mode_switch = 0U;
    in->imu.accel_mps2.z = -9.80665f;
    in->gps.lat_deg = 37.0f;
    in->gps.lon_deg = -122.0f;
    in->gps.alt_m = 100.0f;
    in->gps.vel_mps.x = 15.0f;
    in->gps.fix_valid = 1U;
    in->baro.altitude_m = 100.0f;
    in->airspeed.true_airspeed_mps = 15.0f;
}

static bayek_mission_plan_t make_two_waypoint_mission(const fsw_input_t *in)
{
    bayek_mission_plan_t mission = {0};
    mission.waypoint_count = 2U;
    mission.waypoints[0].lat_deg = in->gps.lat_deg;
    mission.waypoints[0].lon_deg = in->gps.lon_deg;
    mission.waypoints[0].alt_m = in->gps.alt_m;
    mission.waypoints[0].throttle = 0.50f;
    mission.waypoints[0].acceptance_radius_m = 100.0f;
    mission.waypoints[1].lat_deg = in->gps.lat_deg + 0.001f;
    mission.waypoints[1].lon_deg = in->gps.lon_deg;
    mission.waypoints[1].alt_m = in->gps.alt_m + 10.0f;
    mission.waypoints[1].throttle = 0.50f;
    mission.waypoints[1].acceptance_radius_m = 10.0f;
    return mission;
}

static int near_real(real_t a, real_t b)
{
    return fabsf(a - b) <= 1.0e-6f;
}

static int check_zero_actuators(const actuator_cmd_t *actuators)
{
    CHECK(near_real(actuators->motor, 0.0f));
    CHECK(near_real(actuators->aileron, 0.0f));
    CHECK(near_real(actuators->elevator, 0.0f));
    CHECK(near_real(actuators->rudder, 0.0f));
    return 0;
}

static int check_mock_safe_actuators(const actuator_cmd_t *actuators)
{
    CHECK(near_real(actuators->motor, -0.25f));
    CHECK(near_real(actuators->aileron, -0.50f));
    CHECK(near_real(actuators->elevator, -0.75f));
    CHECK(near_real(actuators->rudder, -1.00f));
    return 0;
}

static int check_reset_estimate(const state_estimate_t *estimate)
{
    CHECK(near_real(estimate->attitude.w, 1.0f));
    CHECK(near_real(estimate->attitude.x, 0.0f));
    CHECK(near_real(estimate->attitude.y, 0.0f));
    CHECK(near_real(estimate->attitude.z, 0.0f));
    CHECK(near_real(estimate->euler.roll, 0.0f));
    CHECK(near_real(estimate->euler.pitch, 0.0f));
    CHECK(near_real(estimate->euler.yaw, 0.0f));
    CHECK(near_real(estimate->angular_rate_rps.x, 0.0f));
    CHECK(near_real(estimate->angular_rate_rps.y, 0.0f));
    CHECK(near_real(estimate->angular_rate_rps.z, 0.0f));
    CHECK(near_real(estimate->position_m.x, 0.0f));
    CHECK(near_real(estimate->position_m.y, 0.0f));
    CHECK(near_real(estimate->position_m.z, 0.0f));
    CHECK(near_real(estimate->velocity_mps.x, 0.0f));
    CHECK(near_real(estimate->velocity_mps.y, 0.0f));
    CHECK(near_real(estimate->velocity_mps.z, 0.0f));
    CHECK(near_real(estimate->airspeed_mps, 0.0f));
    return 0;
}

static int check_control_output_is_bounded(const actuator_cmd_t *actuators)
{
    CHECK(actuators->motor >= 0.0f && actuators->motor <= 1.0f);
    CHECK(actuators->aileron >= -1.0f && actuators->aileron <= 1.0f);
    CHECK(actuators->elevator >= -1.0f && actuators->elevator <= 1.0f);
    CHECK(actuators->rudder >= -1.0f && actuators->rudder <= 1.0f);
    return 0;
}

static int test_null_input_failsafe_uses_safe_actuators(void)
{
    fsw_output_t out;

    init_mock_fsw();
    altair_fsw_step(&fsw, NULL, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_mock_safe_actuators(&out.actuators) == 0);
    CHECK(check_reset_estimate(&out.estimate) == 0);
    CHECK(mock.safe_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.control_calls == 0U);
    CHECK(mock.last_safe_params == &params);
    return 0;
}

static int test_null_output_returns_without_callbacks(void)
{
    fsw_input_t in;

    init_mock_fsw();
    make_valid_input(&in);
    altair_fsw_step(&fsw, &in, NULL);
    CHECK(mock.safe_calls == 0U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.control_calls == 0U);
    return 0;
}

static int test_uninitialized_or_missing_vehicle_failsafe_zero_actuators(void)
{
    fsw_input_t in;
    fsw_output_t out;

    make_valid_input(&in);
    altair_fsw_init(&fsw, NULL);
    altair_fsw_step(&fsw, &in, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_zero_actuators(&out.actuators) == 0);
    CHECK(check_reset_estimate(&out.estimate) == 0);
    return 0;
}

static int check_invalid_input_failsafe(fsw_input_t *in)
{
    fsw_output_t out;

    init_mock_fsw();
    in->imu.gyro_rps.z = 1.0f;
    altair_fsw_step(&fsw, in, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_mock_safe_actuators(&out.actuators) == 0);
    CHECK(check_reset_estimate(&out.estimate) == 0);
    CHECK(mock.safe_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.control_calls == 0U);
    return 0;
}

static int test_invalid_dt_boundaries_fail_safe(void)
{
    fsw_input_t in;

    make_valid_input(&in);
    in.dt_s = 0.0f;
    CHECK(check_invalid_input_failsafe(&in) == 0);

    make_valid_input(&in);
    in.dt_s = -0.01f;
    CHECK(check_invalid_input_failsafe(&in) == 0);

    make_valid_input(&in);
    in.dt_s = 0.1001f;
    CHECK(check_invalid_input_failsafe(&in) == 0);
    return 0;
}

static int test_missing_gps_fix_failsafe(void)
{
    fsw_input_t in;

    make_valid_input(&in);
    in.gps.fix_valid = 0U;
    CHECK(check_invalid_input_failsafe(&in) == 0);
    return 0;
}

static int test_disarmed_input_uses_safe_actuators(void)
{
    fsw_input_t in;
    fsw_output_t out;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.arm_switch = 0U;
    altair_fsw_step(&fsw, &in, &out);
    CHECK(out.mode == FSW_MODE_DISARMED);
    CHECK(check_mock_safe_actuators(&out.actuators) == 0);
    CHECK(mock.safe_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.control_calls == 0U);
    return 0;
}

static int test_manual_mode_uses_manual_mixer(void)
{
    fsw_input_t in;
    fsw_output_t out;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.mode_switch = 0U;
    altair_fsw_step(&fsw, &in, &out);
    CHECK(out.mode == FSW_MODE_MANUAL);
    CHECK(near_real(out.actuators.motor, in.rc.throttle));
    CHECK(near_real(out.actuators.aileron, in.rc.roll));
    CHECK(near_real(out.actuators.elevator, in.rc.pitch));
    CHECK(near_real(out.actuators.rudder, in.rc.yaw));
    CHECK(mock.manual_calls == 1U);
    CHECK(mock.control_calls == 0U);
    CHECK(mock.safe_calls == 0U);
    CHECK(mock.last_manual_rc == &in.rc);
    CHECK(fsw.relative_launch.step_count == 1U);
    CHECK(fsw.external_guidance.step_count == 0U);
    CHECK(fsw.performance_management.step_count == 0U);
    return 0;
}

static int test_stabilize_mode_uses_control_mixer(void)
{
    fsw_input_t in;
    fsw_output_t out;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.mode_switch = 1U;
    in.rc.roll = 1.0f;
    in.rc.pitch = -1.0f;
    in.rc.yaw = 1.0f;
    altair_fsw_step(&fsw, &in, &out);
    CHECK(out.mode == FSW_MODE_STABILIZE);
    CHECK(check_control_output_is_bounded(&out.actuators) == 0);
    CHECK(mock.control_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.safe_calls == 0U);
    CHECK(near_real(out.actuators.motor, mock.last_throttle));
    CHECK(near_real(out.actuators.aileron, mock.last_roll));
    CHECK(near_real(out.actuators.elevator, mock.last_pitch));
    CHECK(near_real(out.actuators.rudder, mock.last_yaw));
    CHECK(fsw.relative_launch.step_count == 1U);
    CHECK(fsw.external_guidance.step_count == 1U);
    CHECK(fsw.performance_management.step_count == 1U);
    return 0;
}

static int test_mission_mode_advances_waypoint_and_runs_vehicle_hooks(void)
{
    fsw_input_t in;
    fsw_output_t out;
    bayek_mission_plan_t mission;
    bayek_mission_status_t status;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.mode_switch = 2U;
    mission = make_two_waypoint_mission(&in);
    CHECK(altair_fsw_set_mission(&fsw, &mission) == 1);

    altair_fsw_step(&fsw, &in, &out);
    altair_fsw_get_mission_status(&fsw, &status);

    CHECK(out.mode == FSW_MODE_MISSION);
    CHECK(status.loaded == 1U);
    CHECK(status.active_waypoint_index == 1U);
    CHECK(mock.control_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.safe_calls == 0U);
    CHECK(fsw.relative_launch.step_count == 1U);
    CHECK(fsw.external_guidance.step_count == 1U);
    CHECK(fsw.performance_management.step_count == 1U);
    return 0;
}

static int test_mission_mode_without_loaded_mission_failsafe(void)
{
    fsw_input_t in;
    fsw_output_t out;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.mode_switch = 2U;
    altair_fsw_step(&fsw, &in, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_mock_safe_actuators(&out.actuators) == 0);
    CHECK(mock.safe_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.control_calls == 0U);
    return 0;
}

static int test_unknown_mode_switch_failsafe(void)
{
    fsw_input_t in;
    fsw_output_t out;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.mode_switch = 3U;
    altair_fsw_step(&fsw, &in, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(check_mock_safe_actuators(&out.actuators) == 0);
    CHECK(mock.safe_calls == 1U);
    CHECK(mock.manual_calls == 0U);
    CHECK(mock.control_calls == 0U);
    return 0;
}

static int check_outputs_match(const fsw_output_t *a, const fsw_output_t *b)
{
    CHECK(a->mode == b->mode);
    CHECK(near_real(a->actuators.motor, b->actuators.motor));
    CHECK(near_real(a->actuators.aileron, b->actuators.aileron));
    CHECK(near_real(a->actuators.elevator, b->actuators.elevator));
    CHECK(near_real(a->actuators.rudder, b->actuators.rudder));
    CHECK(near_real(a->estimate.euler.roll, b->estimate.euler.roll));
    CHECK(near_real(a->estimate.euler.pitch, b->estimate.euler.pitch));
    CHECK(near_real(a->estimate.euler.yaw, b->estimate.euler.yaw));
    CHECK(near_real(a->estimate.angular_rate_rps.x, b->estimate.angular_rate_rps.x));
    CHECK(near_real(a->estimate.angular_rate_rps.y, b->estimate.angular_rate_rps.y));
    CHECK(near_real(a->estimate.angular_rate_rps.z, b->estimate.angular_rate_rps.z));
    CHECK(near_real(a->estimate.position_m.z, b->estimate.position_m.z));
    CHECK(near_real(a->estimate.velocity_mps.x, b->estimate.velocity_mps.x));
    CHECK(near_real(a->estimate.airspeed_mps, b->estimate.airspeed_mps));
    return 0;
}

static int test_reset_repeats_valid_step_deterministically(void)
{
    fsw_input_t in;
    fsw_output_t first;
    fsw_output_t second;

    init_mock_fsw();
    make_valid_input(&in);
    in.rc.mode_switch = 1U;
    in.rc.roll = 0.75f;
    in.rc.pitch = -0.50f;
    in.rc.yaw = 0.25f;
    in.imu.gyro_rps.x = 0.20f;
    in.imu.gyro_rps.y = -0.10f;
    in.imu.gyro_rps.z = 0.30f;
    altair_fsw_step(&fsw, &in, &first);

    altair_fsw_reset(&fsw);
    altair_fsw_step(&fsw, &in, &second);

    CHECK(check_outputs_match(&first, &second) == 0);
    CHECK(mock.control_calls == 2U);
    CHECK(mock.safe_calls == 0U);
    CHECK(mock.manual_calls == 0U);
    return 0;
}

int main(void)
{
    CHECK(test_null_input_failsafe_uses_safe_actuators() == 0);
    CHECK(test_null_output_returns_without_callbacks() == 0);
    CHECK(test_uninitialized_or_missing_vehicle_failsafe_zero_actuators() == 0);
    CHECK(test_invalid_dt_boundaries_fail_safe() == 0);
    CHECK(test_missing_gps_fix_failsafe() == 0);
    CHECK(test_disarmed_input_uses_safe_actuators() == 0);
    CHECK(test_manual_mode_uses_manual_mixer() == 0);
    CHECK(test_stabilize_mode_uses_control_mixer() == 0);
    CHECK(test_mission_mode_advances_waypoint_and_runs_vehicle_hooks() == 0);
    CHECK(test_mission_mode_without_loaded_mission_failsafe() == 0);
    CHECK(test_unknown_mode_switch_failsafe() == 0);
    CHECK(test_reset_repeats_valid_step_deterministically() == 0);
    return 0;
}
