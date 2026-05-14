#include "sitl_conditions.h"
#include "sitl_initial_conditions.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d\n", __FILE__, __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
    {
        return 0;
    }
    if (fputs(text, file) < 0)
    {
        (void)fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int near_real(real_t a, real_t b)
{
    return fabsf(a - b) < 1.0e-5f;
}

int main(void)
{
    sitl_initial_conditions_t initial;
    sitl_conditions_t conditions;
    sitl_condition_context_t ctx;
    rc_input_t rc = {0};
    fsw_input_t input = {0};
    vehicle_params_t vehicle_params = {0};
    sim_fixedwing_params_t sim_params = {0};
    sim_fixedwing_state_t plant;
    sitl_trim_config_t trim_config;
    uint8_t mission_enabled = 0U;
    bayek_mission_plan_t mission = {0};
    char error[200];

    CHECK(write_file("bayek_host_initial.ini",
                     "lat_deg = 37.5\n"
                     "lon_deg = -122.1\n"
                     "vel_n_mps = 20\n"
                     "rc_throttle = 0.65\n"
                     "rc_mode = 2\n"));
    CHECK(sitl_initial_conditions_load("bayek_host_initial.ini", &initial, error, sizeof(error)));
    CHECK(near_real(initial.lat_deg, 37.5f));
    CHECK(near_real(initial.lon_deg, -122.1f));
    CHECK(initial.has_velocity_ned == 1U);
    CHECK(near_real(initial.rc.throttle, 0.65f));
    CHECK(initial.rc.mode_switch == 2U);

    CHECK(write_file("bayek_host_conditions.ini",
                     "[rule.step]\n"
                     "when = step >= 2\n"
                     "rc.throttle = 0.25\n"
                     "input.gps.fix_valid = 0\n"
                     "plant.position_ned_m.x = 12\n"
                     "vehicle_params.max_airspeed_mps = 31\n"
                     "sim_params.wing_area_m2 = 0.7\n"
                     "trim.enabled = 1\n"
                     "mission.enabled = 1\n"
                     "mission.waypoint_count = 1\n"
                     "mission.waypoint.0.lat_deg = 37.6\n"));
    CHECK(sitl_conditions_load("bayek_host_conditions.ini", &conditions, error, sizeof(error)));

    sim_fixedwing_init_default(&plant);
    sitl_trim_config_default(&trim_config);
    rc = initial.rc;
    input.rc = rc;
    input.gps.fix_valid = 1U;
    ctx.rc = &rc;
    ctx.input = &input;
    ctx.vehicle_params = &vehicle_params;
    ctx.sim_params = &sim_params;
    ctx.plant = &plant;
    ctx.trim = &trim_config;
    ctx.mission_enabled = &mission_enabled;
    ctx.mission = &mission;
    ctx.step = 2U;

    CHECK(sitl_conditions_eval(&conditions, &ctx, error, sizeof(error)));
    CHECK(near_real(rc.throttle, 0.25f));
    CHECK(near_real(input.rc.throttle, 0.25f));
    CHECK(input.gps.fix_valid == 0U);
    CHECK(near_real(plant.body.position_ned_m.x, 12.0f));
    CHECK(near_real(vehicle_params.max_airspeed_mps, 31.0f));
    CHECK(near_real(sim_params.wing_area_m2, 0.7f));
    CHECK(trim_config.enabled == 1U);
    CHECK(mission_enabled == 1U);
    CHECK(mission.waypoint_count == 1U);
    CHECK(near_real(mission.waypoints[0].lat_deg, 37.6f));
    CHECK(ctx.vehicle_params_dirty == 1U);
    CHECK(ctx.sim_params_dirty == 1U);
    CHECK(ctx.plant_ned_dirty == 1U);
    CHECK(ctx.mission_dirty == 1U);

    CHECK(write_file("bayek_host_unknown.ini",
                     "[rule.bad]\n"
                     "when = step >= 0\n"
                     "altair.only = 1\n"));
    CHECK(!sitl_conditions_load("bayek_host_unknown.ini", &conditions, error, sizeof(error)));
    return 0;
}
