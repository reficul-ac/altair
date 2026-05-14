#include "fsw.h"
#include "guidance.h"
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

static const vehicle_params_t params = {40.0f, 5.0f, 0.50f, 0.30f, 0.80f, 1.0f, -1.0f, 0.0f, 0.0f};

static actuator_cmd_t mix_manual(const rc_input_t *rc)
{
    actuator_cmd_t cmd = {
        rc ? rc->throttle : 0.0f, rc ? rc->roll : 0.0f, rc ? rc->pitch : 0.0f, rc ? rc->yaw : 0.0f};
    return cmd;
}

static actuator_cmd_t
mix_control(real_t throttle, real_t roll_cmd, real_t pitch_cmd, real_t yaw_cmd)
{
    actuator_cmd_t cmd = {throttle, roll_cmd, pitch_cmd, yaw_cmd};
    return cmd;
}

static actuator_cmd_t safe_actuators(const vehicle_params_t *unused)
{
    actuator_cmd_t cmd = {0.0f, 0.0f, 0.0f, 0.0f};
    (void)unused;
    return cmd;
}

static const bayek_vehicle_interface_t vehicle = {&params, mix_manual, mix_control, safe_actuators};

static bayek_mission_plan_t valid_mission(void)
{
    bayek_mission_plan_t mission = {0};
    mission.waypoint_count = 1U;
    mission.waypoints[0].lat_deg = 37.0f;
    mission.waypoints[0].lon_deg = -122.0f;
    mission.waypoints[0].alt_m = 120.0f;
    mission.waypoints[0].throttle = 0.55f;
    mission.waypoints[0].acceptance_radius_m = 10.0f;
    return mission;
}

static void valid_input(fsw_input_t *in)
{
    *in = (fsw_input_t){0};
    in->dt_s = 0.01f;
    in->rc.arm_switch = 1U;
    in->rc.mode_switch = 2U;
    in->gps.fix_valid = 1U;
    in->gps.lat_deg = 37.0f;
    in->gps.lon_deg = -122.0f;
    in->gps.alt_m = 100.0f;
    in->baro.altitude_m = 100.0f;
    in->airspeed.true_airspeed_mps = 20.0f;
}

static int near_real(real_t a, real_t b, real_t eps)
{
    return fabsf(a - b) <= eps;
}

static int test_mission_validation(void)
{
    bayek_mission_plan_t mission;
    bayek_mission_status_t status;

    bayek_fsw_init(&vehicle);

    bayek_fsw_set_mission(NULL);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoint_count = 0U;
    bayek_fsw_set_mission(&mission);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoint_count = BAYEK_MISSION_MAX_WAYPOINTS + 1U;
    bayek_fsw_set_mission(&mission);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoints[0].lat_deg = NAN;
    bayek_fsw_set_mission(&mission);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoints[0].throttle = 1.1f;
    bayek_fsw_set_mission(&mission);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoints[0].acceptance_radius_m = 0.0f;
    bayek_fsw_set_mission(&mission);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    bayek_fsw_set_mission(&mission);
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 1U);
    CHECK(status.waypoint_count == 1U);
    CHECK(status.active_waypoint_index == 0U);

    bayek_fsw_clear_mission();
    bayek_fsw_get_mission_status(&status);
    CHECK(status.loaded == 0U);
    return 0;
}

static int test_guidance_math(void)
{
    fsw_input_t in;
    state_estimate_t estimate = {0};
    bayek_mission_waypoint_t waypoint;
    bayek_guidance_setpoint_t setpoint;
    real_t distance_m;

    valid_input(&in);
    estimate.euler.yaw = 0.0f;

    waypoint = valid_mission().waypoints[0];
    waypoint.lat_deg = in.gps.lat_deg + 0.001f;
    waypoint.lon_deg = in.gps.lon_deg;
    waypoint.alt_m = in.gps.alt_m;
    CHECK(bayek_guidance_mission_to_waypoint(
              &in, &estimate, &waypoint, &params, &setpoint, &distance_m) == 1);
    CHECK(near_real(setpoint.roll_rad, 0.0f, 0.01f));
    CHECK(distance_m > 100.0f);

    waypoint.lat_deg = in.gps.lat_deg;
    waypoint.lon_deg = in.gps.lon_deg + 0.001f;
    CHECK(bayek_guidance_mission_to_waypoint(
              &in, &estimate, &waypoint, &params, &setpoint, &distance_m) == 1);
    CHECK(setpoint.roll_rad > 0.0f);

    waypoint.alt_m = in.gps.alt_m + 10.0f;
    CHECK(bayek_guidance_mission_to_waypoint(
              &in, &estimate, &waypoint, &params, &setpoint, &distance_m) == 1);
    CHECK(near_real(setpoint.pitch_rad, 0.10f, 0.001f));

    waypoint.throttle = 2.0f;
    waypoint.alt_m = in.gps.alt_m + 1000.0f;
    CHECK(bayek_guidance_mission_to_waypoint(
              &in, &estimate, &waypoint, &params, &setpoint, &distance_m) == 1);
    CHECK(near_real(setpoint.throttle, 1.0f, 1.0e-6f));
    CHECK(near_real(setpoint.roll_rad, params.max_roll_rad, 1.0e-6f));
    CHECK(near_real(setpoint.pitch_rad, params.max_pitch_rad, 1.0e-6f));
    CHECK(near_real(setpoint.yaw_rate_rps, 0.0f, 1.0e-6f));
    return 0;
}

static int test_mission_mode_without_loaded_mission_failsafe(void)
{
    fsw_input_t in;
    fsw_output_t out;

    bayek_fsw_init(&vehicle);
    valid_input(&in);
    bayek_fsw_clear_mission();
    bayek_fsw_step(&in, &out);
    CHECK(out.mode == FSW_MODE_FAILSAFE);
    CHECK(near_real(out.actuators.motor, 0.0f, 1.0e-6f));
    return 0;
}

int main(void)
{
    CHECK(test_mission_validation() == 0);
    CHECK(test_guidance_math() == 0);
    CHECK(test_mission_mode_without_loaded_mission_failsafe() == 0);
    return 0;
}
