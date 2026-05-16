#include "guidance.h"
#include "math_utils.h"
#include "mission.h"

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
    bayek_mission_state_t state;
    bayek_mission_plan_t mission;
    bayek_mission_status_t status;

    bayek_mission_init(&state);

    CHECK(bayek_mission_set(&state, NULL) == 0);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoint_count = 0U;
    CHECK(bayek_mission_set(&state, &mission) == 0);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoint_count = BAYEK_MISSION_MAX_WAYPOINTS + 1U;
    CHECK(bayek_mission_set(&state, &mission) == 0);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoints[0].lat_deg = NAN;
    CHECK(bayek_mission_set(&state, &mission) == 0);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoints[0].throttle = 1.1f;
    CHECK(bayek_mission_set(&state, &mission) == 0);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    mission.waypoints[0].acceptance_radius_m = 0.0f;
    CHECK(bayek_mission_set(&state, &mission) == 0);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);

    mission = valid_mission();
    CHECK(bayek_mission_set(&state, &mission) == 1);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 1U);
    CHECK(status.waypoint_count == 1U);
    CHECK(status.active_waypoint_index == 0U);

    bayek_mission_clear(&state);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 0U);
    return 0;
}

static int test_active_waypoint_advancement(void)
{
    bayek_mission_state_t state;
    bayek_mission_plan_t mission = {0};
    bayek_mission_status_t status;
    fsw_input_t in;
    state_estimate_t estimate = {0};
    bayek_guidance_setpoint_t setpoint;

    bayek_mission_init(&state);
    valid_input(&in);
    mission.waypoint_count = 2U;
    mission.waypoints[0] = valid_mission().waypoints[0];
    mission.waypoints[0].lat_deg = in.gps.lat_deg;
    mission.waypoints[0].lon_deg = in.gps.lon_deg;
    mission.waypoints[0].alt_m = in.gps.alt_m;
    mission.waypoints[0].acceptance_radius_m = 20.0f;
    mission.waypoints[1] = valid_mission().waypoints[0];
    mission.waypoints[1].lat_deg = in.gps.lat_deg + 0.001f;
    mission.waypoints[1].lon_deg = in.gps.lon_deg;
    mission.waypoints[1].alt_m = in.gps.alt_m;
    mission.waypoints[1].acceptance_radius_m = 20.0f;

    CHECK(bayek_mission_set(&state, &mission) == 1);
    CHECK(bayek_mission_select_active_waypoint(&state, &in, &estimate, &params, &setpoint) == 1);
    bayek_mission_get_status(&state, &status);
    CHECK(status.active_waypoint_index == 1U);
    CHECK(status.horizontal_distance_m > 100.0f);

    bayek_mission_reset(&state);
    bayek_mission_get_status(&state, &status);
    CHECK(status.loaded == 1U);
    CHECK(status.active_waypoint_index == 0U);
    return 0;
}

static int test_setpoint_selection_failure_paths(void)
{
    bayek_mission_state_t state;
    bayek_mission_plan_t mission;
    bayek_guidance_setpoint_t setpoint;
    fsw_input_t in;
    state_estimate_t estimate = {0};

    bayek_mission_init(&state);
    valid_input(&in);
    CHECK(bayek_mission_select_active_waypoint(&state, &in, &estimate, &params, &setpoint) == 0);
    mission = valid_mission();
    CHECK(bayek_mission_set(&state, &mission) == 1);
    CHECK(bayek_mission_select_active_waypoint(&state, NULL, &estimate, &params, &setpoint) == 0);
    CHECK(bayek_mission_select_active_waypoint(&state, &in, NULL, &params, &setpoint) == 0);
    CHECK(bayek_mission_select_active_waypoint(&state, &in, &estimate, NULL, &setpoint) == 0);
    CHECK(bayek_mission_select_active_waypoint(&state, &in, &estimate, &params, NULL) == 0);
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

int main(void)
{
    CHECK(test_mission_validation() == 0);
    CHECK(test_active_waypoint_advancement() == 0);
    CHECK(test_setpoint_selection_failure_paths() == 0);
    CHECK(test_guidance_math() == 0);
    return 0;
}
