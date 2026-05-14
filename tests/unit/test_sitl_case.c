#include "sitl_case.h"

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
    sitl_case_t case_file;
    char error[200];

    sitl_case_default(&case_file);
    CHECK(case_file.has_initial == 0U);
    CHECK(case_file.has_vehicle_params == 0U);
    CHECK(case_file.has_sim_params == 0U);
    CHECK(case_file.has_mission == 0U);
    CHECK(case_file.mission_enabled == 1U);
    CHECK(near_real(case_file.initial.airspeed_mps, 18.0f));

    CHECK(write_file("sitl_case_full.ini",
                     "# Full sectioned case\n"
                     "[run]\n"
                     "scenario = cruise6dof\n"
                     "profile = mission\n"
                     "duration_s = 30\n"
                     "dt_s = 0.02\n"
                     "seed = 42\n"
                     "frame_mode = ned\n"
                     "\n"
                     "[initial]\n"
                     "lat_deg = 37.5\n"
                     "lon_deg = -122.1\n"
                     "altitude_m = 250\n"
                     "yaw_rad = 0.3\n"
                     "airspeed_mps = 22\n"
                     "\n"
                     "[rc]\n"
                     "throttle = 0.7\n"
                     "roll = -0.2\n"
                     "pitch = 0.04\n"
                     "yaw = 0.1\n"
                     "arm = 1\n"
                     "mode = 2\n"
                     "\n"
                     "[vehicle_params]\n"
                     "max_airspeed_mps = 32\n"
                     "max_roll_rad = 0.65\n"
                     "\n"
                     "[sim_params]\n"
                     "core.mass_kg = 2.8\n"
                     "wing_area_m2 = 0.46\n"
                     "max_thrust_n = 18\n"
                     "\n"
                     "[mission]\n"
                     "enabled = 1\n"
                     "\n"
                     "[waypoint.0]\n"
                     "lat_deg = 37.5\n"
                     "lon_deg = -122.1\n"
                     "alt_m = 250\n"
                     "throttle = 0.62\n"
                     "acceptance_radius_m = 50\n"
                     "\n"
                     "[waypoint.1]\n"
                     "lat_deg = 37.6\n"
                     "lon_deg = -122.0\n"
                     "alt_m = 300\n"
                     "throttle = 0.64\n"
                     "acceptance_radius_m = 55\n"));
    CHECK(sitl_case_load("sitl_case_full.ini", &case_file, error, sizeof(error)));
    CHECK(case_file.run.has_scenario == 1U);
    CHECK(strcmp(case_file.run.scenario, "cruise6dof") == 0);
    CHECK(strcmp(case_file.run.profile, "mission") == 0);
    CHECK(case_file.run.duration_s == 30.0);
    CHECK(case_file.run.dt_s == 0.02);
    CHECK(case_file.run.seed == 42U);
    CHECK(case_file.run.frame_mode == SIM6DOF_FRAME_NED);
    CHECK(case_file.has_initial == 1U);
    CHECK(near_real(case_file.initial.lat_deg, 37.5f));
    CHECK(near_real(case_file.initial.rc.throttle, 0.7f));
    CHECK(case_file.initial.rc.mode_switch == 2U);
    CHECK(case_file.has_vehicle_params == 1U);
    CHECK(near_real(case_file.vehicle_params.max_airspeed_mps, 32.0f));
    CHECK(case_file.has_sim_params == 1U);
    CHECK(near_real(case_file.sim_params.core.mass_kg, 2.8f));
    CHECK(near_real(case_file.sim_params.max_thrust_n, 18.0f));
    CHECK(case_file.has_mission == 1U);
    CHECK(case_file.mission_enabled == 1U);
    CHECK(case_file.mission.waypoint_count == 2U);
    CHECK(near_real(case_file.mission.waypoints[1].alt_m, 300.0f));

    CHECK(write_file("sitl_case_unknown.ini", "[initial]\nunknown = 1\n"));
    CHECK(!sitl_case_load("sitl_case_unknown.ini", &case_file, error, sizeof(error)));
    CHECK(strstr(error, "line 2") != NULL);

    CHECK(write_file("sitl_case_invalid_numeric.ini", "[initial]\nlat_deg = nope\n"));
    CHECK(!sitl_case_load("sitl_case_invalid_numeric.ini", &case_file, error, sizeof(error)));
    CHECK(strstr(error, "line 2") != NULL);

    CHECK(write_file("sitl_case_bad_wp.ini", "[waypoint.16]\nlat_deg = 1\n"));
    CHECK(!sitl_case_load("sitl_case_bad_wp.ini", &case_file, error, sizeof(error)));
    CHECK(strstr(error, "line 1") != NULL);

    CHECK(write_file("sitl_case_gap_wp.ini", "[waypoint.1]\nlat_deg = 1\n"));
    CHECK(!sitl_case_load("sitl_case_gap_wp.ini", &case_file, error, sizeof(error)));

    CHECK(write_file("sitl_case_bad_section.ini", "lat_deg = 1\n"));
    CHECK(!sitl_case_load("sitl_case_bad_section.ini", &case_file, error, sizeof(error)));
    CHECK(strstr(error, "line 1") != NULL);

    return 0;
}
