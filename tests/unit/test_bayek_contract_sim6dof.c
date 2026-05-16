#include "sim6dof.h"

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

static real_t quat_norm(quat_t q)
{
    return (real_t)sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

static int near_real(real_t a, real_t b, real_t tol)
{
    return fabsf(a - b) <= tol;
}

int main(void)
{
    sim6dof_params_t params;
    sim6dof_state_t state;
    actuator_cmd_t cmd = {0.0f, 0.0f, 0.0f, 0.0f};
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    vec3_t force = {10.0f, 0.0f, 0.0f};
    vec3_t moment = {0.0f, 1.0f, 0.0f};
    vec3_t small_moment = {0.0f, 0.05f, 0.0f};
    vec3_t ecef;
    vec3_t ned;
    vec3_t north;
    vec3_t east;
    vec3_t down;
    real_t lat_deg;
    real_t lon_deg;
    real_t altitude_m;
    int i;

    sim6dof_default_params(&params);
    sim6dof_geodetic_to_ecef(0.0f, 0.0f, 0.0f, params.earth_radius_m, &ecef);
    CHECK(near_real(ecef.x, params.earth_radius_m, 1.0f));
    CHECK(near_real(ecef.y, 0.0f, 0.001f));
    CHECK(near_real(ecef.z, 0.0f, 0.001f));
    sim6dof_geodetic_to_ecef(0.0f, 90.0f, 12.0f, params.earth_radius_m, &ecef);
    CHECK(near_real(ecef.x, 0.0f, 1.0f));
    CHECK(near_real(ecef.y, params.earth_radius_m + 12.0f, 1.0f));
    sim6dof_ecef_to_geodetic(ecef, params.earth_radius_m, &lat_deg, &lon_deg, &altitude_m);
    CHECK(near_real(lat_deg, 0.0f, 0.001f));
    CHECK(near_real(lon_deg, 90.0f, 0.001f));
    CHECK(near_real(altitude_m, 12.0f, 0.5f));

    ned.x = 20.0f;
    ned.y = -7.0f;
    ned.z = 3.0f;
    ecef = sim6dof_ned_position_to_ecef(37.0f, -122.0f, 100.0f, params.earth_radius_m, ned);
    ned = sim6dof_ecef_position_to_ned(37.0f, -122.0f, 100.0f, params.earth_radius_m, ecef);
    CHECK(near_real(ned.x, 20.0f, 0.5f));
    CHECK(near_real(ned.y, -7.0f, 0.5f));
    CHECK(near_real(ned.z, 3.0f, 0.5f));
    sim6dof_ned_basis(37.0f, -122.0f, &north, &east, &down);
    CHECK(near_real(vec3_norm(north), 1.0f, 0.001f));
    CHECK(near_real(vec3_norm(east), 1.0f, 0.001f));
    CHECK(near_real(vec3_norm(down), 1.0f, 0.001f));
    CHECK(near_real(vec3_dot(north, east), 0.0f, 0.001f));
    CHECK(near_real(vec3_dot(north, down), 0.0f, 0.001f));
    CHECK(near_real(vec3_dot(east, down), 0.0f, 0.001f));

    params.frame_mode = SIM6DOF_FRAME_NED;
    sim6dof_init_level(&state, 0.0f, 0.0f);
    CHECK(sim6dof_step(&state, &params, &cmd, zero, zero, 0.01f));
    CHECK(state.velocity_ned_mps.x == 0.0f);
    CHECK(state.velocity_ned_mps.z > 0.09f);
    CHECK(vec3_norm(state.position_ecef_m) > params.earth_radius_m - 1.0f);

    sim6dof_init_level(&state, 0.0f, 0.0f);
    CHECK(sim6dof_step(&state, &params, &cmd, force, zero, 0.10f));
    CHECK(state.velocity_ned_mps.x > 0.3f);

    sim6dof_init_level(&state, 0.0f, 0.0f);
    CHECK(sim6dof_step(&state, &params, &cmd, zero, moment, 0.10f));
    CHECK(state.omega_body_rps.y > 0.1f);

    sim6dof_init_level(&state, 100.0f, 20.0f);
    for (i = 0; i < 1000; ++i)
    {
        CHECK(sim6dof_step(&state, &params, &cmd, zero, small_moment, 0.01f));
        CHECK(fabsf(quat_norm(state.attitude_body_to_ned) - 1.0f) < 0.001f);
    }

    params.frame_mode = SIM6DOF_FRAME_ECEF;
    sim6dof_init_level(&state, 100.0f, 20.0f);
    sim6dof_set_origin(&state, 37.4275f, -122.1697f, 150.0f, params.earth_radius_m);
    for (i = 0; i < 20; ++i)
    {
        CHECK(sim6dof_step(&state, &params, &cmd, zero, zero, 0.01f));
    }
    CHECK(state.position_ned_m.x > 3.0f);
    CHECK(state.velocity_ned_mps.z > 1.0f);
    CHECK(vec3_norm(state.position_ecef_m) > params.earth_radius_m);
    CHECK(fabsf(quat_norm(state.attitude_body_to_ecef) - 1.0f) < 0.001f);

    CHECK(!sim6dof_step(&state, &params, &cmd, zero, zero, 0.0f));
    state.velocity_ned_mps.x = NAN;
    CHECK(!sim6dof_state_is_valid(&state));
    return 0;
}
