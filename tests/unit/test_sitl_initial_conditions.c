#include "sitl_initial_conditions.h"

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
    char error[160];

    CHECK(write_file("sitl_initial_valid.ini",
                     "# comments and blank lines are ignored\n"
                     "\n"
                     "lat_deg = 37.5\n"
                     "lon_deg = -122.1 # inline comment\n"
                     "altitude_m = 250\n"
                     "roll_rad = 0.1\n"
                     "pitch_rad = -0.05\n"
                     "yaw_rad = 1.2\n"
                     "vel_n_mps = 21\n"
                     "vel_e_mps = 2\n"
                     "vel_d_mps = -1\n"
                     "p_rps = 0.01\n"
                     "q_rps = -0.02\n"
                     "r_rps = 0.03\n"
                     "airspeed_mps = 23\n"
                     "rc_throttle = 0.7\n"
                     "rc_roll = -0.2\n"
                     "rc_pitch = 0.3\n"
                     "rc_yaw = 0.1\n"
                     "rc_arm = 1\n"
                     "rc_mode = 2\n"));
    CHECK(sitl_initial_conditions_load("sitl_initial_valid.ini", &initial, error, sizeof(error)));
    CHECK(near_real(initial.lat_deg, 37.5f));
    CHECK(near_real(initial.lon_deg, -122.1f));
    CHECK(near_real(initial.altitude_m, 250.0f));
    CHECK(near_real(initial.roll_rad, 0.1f));
    CHECK(near_real(initial.pitch_rad, -0.05f));
    CHECK(near_real(initial.yaw_rad, 1.2f));
    CHECK(initial.has_velocity_ned == 1U);
    CHECK(near_real(initial.vel_n_mps, 21.0f));
    CHECK(near_real(initial.vel_e_mps, 2.0f));
    CHECK(near_real(initial.vel_d_mps, -1.0f));
    CHECK(near_real(initial.p_rps, 0.01f));
    CHECK(near_real(initial.q_rps, -0.02f));
    CHECK(near_real(initial.r_rps, 0.03f));
    CHECK(near_real(initial.airspeed_mps, 23.0f));
    CHECK(near_real(initial.rc.throttle, 0.7f));
    CHECK(near_real(initial.rc.roll, -0.2f));
    CHECK(near_real(initial.rc.pitch, 0.3f));
    CHECK(near_real(initial.rc.yaw, 0.1f));
    CHECK(initial.rc.arm_switch == 1U);
    CHECK(initial.rc.mode_switch == 2U);

    CHECK(write_file("sitl_initial_defaults.ini", "lat_deg = 1.25\n"));
    CHECK(
        sitl_initial_conditions_load("sitl_initial_defaults.ini", &initial, error, sizeof(error)));
    CHECK(near_real(initial.lat_deg, 1.25f));
    CHECK(near_real(initial.lon_deg, 0.0f));
    CHECK(near_real(initial.altitude_m, 120.0f));
    CHECK(initial.has_velocity_ned == 0U);
    CHECK(near_real(initial.airspeed_mps, 18.0f));
    CHECK(near_real(initial.rc.throttle, 0.58f));
    CHECK(initial.rc.arm_switch == 1U);
    CHECK(initial.rc.mode_switch == 1U);

    CHECK(write_file("sitl_initial_unknown.ini", "unknown_key = 1\n"));
    CHECK(
        !sitl_initial_conditions_load("sitl_initial_unknown.ini", &initial, error, sizeof(error)));

    CHECK(write_file("sitl_initial_invalid.ini", "lat_deg = nope\n"));
    CHECK(
        !sitl_initial_conditions_load("sitl_initial_invalid.ini", &initial, error, sizeof(error)));

    CHECK(write_file("sitl_initial_inf.ini", "lat_deg = inf\n"));
    CHECK(!sitl_initial_conditions_load("sitl_initial_inf.ini", &initial, error, sizeof(error)));

    return 0;
}
