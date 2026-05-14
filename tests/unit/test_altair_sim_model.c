#include "altair_sim_model.h"
#include "altair_vehicle.h"
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
#define CHECK_MSG(c, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d: %s\n", __FILE__, __LINE__, msg);                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int close_real(real_t a, real_t b)
{
    return fabsf(a - b) <= 1.0e-6f;
}

static int finite_positive(real_t value)
{
    return real_is_finite(value) && value > 0.0f;
}

static int close_vec3(vec3_t a, vec3_t b)
{
    return close_real(a.x, b.x) && close_real(a.y, b.y) && close_real(a.z, b.z);
}

static int close_quat(quat_t a, quat_t b)
{
    return close_real(a.w, b.w) && close_real(a.x, b.x) && close_real(a.y, b.y) &&
           close_real(a.z, b.z);
}

static int close_actuator(actuator_cmd_t a, actuator_cmd_t b)
{
    return close_real(a.motor, b.motor) && close_real(a.aileron, b.aileron) &&
           close_real(a.elevator, b.elevator) && close_real(a.rudder, b.rudder);
}

static int close_state(sim_fixedwing_state_t a, sim_fixedwing_state_t b)
{
    return close_vec3(a.body.position_ned_m, b.body.position_ned_m) &&
           close_vec3(a.body.velocity_ned_mps, b.body.velocity_ned_mps) &&
           close_quat(a.body.attitude_body_to_ned, b.body.attitude_body_to_ned) &&
           close_vec3(a.body.position_ecef_m, b.body.position_ecef_m) &&
           close_vec3(a.body.velocity_ecef_mps, b.body.velocity_ecef_mps) &&
           close_quat(a.body.attitude_body_to_ecef, b.body.attitude_body_to_ecef) &&
           close_vec3(a.body.omega_body_rps, b.body.omega_body_rps) &&
           close_actuator(a.body.actuator_state, b.body.actuator_state) &&
           close_vec3(a.body.specific_force_body_mps2, b.body.specific_force_body_mps2) &&
           close_real(a.body.time_s, b.body.time_s) &&
           close_vec3(a.last_force_body_n, b.last_force_body_n) &&
           close_vec3(a.last_moment_body_nm, b.last_moment_body_nm) &&
           close_real(a.last_airspeed_mps, b.last_airspeed_mps);
}

static int expect_invalid_sim(const sim_fixedwing_params_t *params, const vehicle_params_t *vehicle)
{
    char error[160];
    return !altair_fixedwing_sim_params_are_valid(params, vehicle, error, sizeof(error)) &&
           error[0] != '\0';
}

static int expect_invalid_vehicle(const sim_fixedwing_params_t *params,
                                  const vehicle_params_t *vehicle)
{
    char error[160];
    return !altair_fixedwing_sim_params_are_valid(params, vehicle, error, sizeof(error)) &&
           error[0] != '\0';
}

static sim_fixedwing_state_t run_sequence(const sim_fixedwing_params_t *params, int frame_mode)
{
    sim_fixedwing_state_t state;
    sim_fixedwing_params_t local_params = *params;
    actuator_cmd_t cmd;
    int i;

    local_params.core.frame_mode = frame_mode;
    sim_fixedwing_init_default(&state);
    for (i = 0; i < 250; ++i)
    {
        cmd.motor = i < 80 ? 0.62f : 0.54f;
        cmd.aileron = i < 60 ? 0.0f : (i < 170 ? 0.18f : -0.08f);
        cmd.elevator = i < 120 ? 0.04f : -0.03f;
        cmd.rudder = i < 150 ? 0.0f : 0.05f;
        if (!sim_fixedwing_step(&state, &local_params, &cmd, 0.01f))
        {
            state.last_airspeed_mps = NAN;
            return state;
        }
    }
    return state;
}

int main(void)
{
    sim_fixedwing_params_t params;
    sim_fixedwing_params_t invalid;
    sim_fixedwing_state_t first;
    sim_fixedwing_state_t second;
    const vehicle_params_t *vehicle = altair_default_params();
    vehicle_params_t invalid_vehicle;
    real_t weight_n;
    real_t wing_loading;
    char error[160];

    altair_fixedwing_sim_params(&params);
    CHECK_MSG(altair_fixedwing_sim_params_are_valid(&params, vehicle, error, sizeof(error)), error);
    CHECK(close_real(params.core.mass_kg, 2.5f));
    CHECK(close_real(params.core.inertia_kgm2.x, 0.08f));
    CHECK(close_real(params.core.inertia_kgm2.y, 0.12f));
    CHECK(close_real(params.core.inertia_kgm2.z, 0.18f));
    CHECK(close_real(params.core.gravity_mps2, 9.80665f));
    CHECK(close_real(params.core.air_density_kgpm3, 1.225f));
    CHECK(close_real(params.core.actuator_lag_hz, 12.0f));
    CHECK(params.core.frame_mode == SIM6DOF_FRAME_ECEF);
    CHECK(params.core.earth_model == SIM6DOF_EARTH_SPHERICAL);
    CHECK(close_real(params.core.earth_radius_m, 6378137.0f));
    CHECK(close_real(params.wing_area_m2, 0.45f));
    CHECK(close_real(params.wing_span_m, 1.8f));
    CHECK(close_real(params.mean_chord_m, 0.25f));
    CHECK(close_real(params.max_thrust_n, 18.0f));
    CHECK(close_real(params.drag_cd0, 0.035f));
    CHECK(close_real(params.drag_cd_alpha, 0.80f));
    CHECK(close_real(params.lift_cl0, 0.18f));
    CHECK(close_real(params.lift_cl_alpha, 4.2f));
    CHECK(close_real(params.lift_cl_elevator, 0.35f));
    CHECK(close_real(params.stall_alpha_rad, 0.28f));
    CHECK(close_real(params.roll_aileron_nm, 1.6f));
    CHECK(close_real(params.pitch_elevator_nm, 1.0f));
    CHECK(close_real(params.yaw_rudder_nm, 0.6f));
    CHECK(close_real(params.rate_damping_nms.x, 0.35f));
    CHECK(close_real(params.rate_damping_nms.y, 0.45f));
    CHECK(close_real(params.rate_damping_nms.z, 0.30f));
    CHECK(finite_positive(params.core.mass_kg));
    CHECK(finite_positive(params.core.inertia_kgm2.x));
    CHECK(finite_positive(params.core.inertia_kgm2.y));
    CHECK(finite_positive(params.core.inertia_kgm2.z));
    CHECK(finite_positive(params.wing_area_m2));
    CHECK(finite_positive(params.wing_span_m));
    CHECK(finite_positive(params.mean_chord_m));
    CHECK(finite_positive(params.max_thrust_n));
    CHECK(finite_positive(params.lift_cl_alpha));
    CHECK(finite_positive(params.stall_alpha_rad));
    CHECK(params.drag_cd0 >= 0.0f);
    CHECK(params.drag_cd_alpha >= 0.0f);
    CHECK(params.rate_damping_nms.x >= 0.0f);
    CHECK(params.rate_damping_nms.y >= 0.0f);
    CHECK(params.rate_damping_nms.z >= 0.0f);
    weight_n = params.core.mass_kg * params.core.gravity_mps2;
    CHECK(params.max_thrust_n > 0.5f * weight_n);
    wing_loading = weight_n / params.wing_area_m2;
    CHECK(wing_loading > 20.0f && wing_loading < 120.0f);
    CHECK(vehicle->min_airspeed_mps < 18.0f && vehicle->max_airspeed_mps > 18.0f);
    CHECK(params.core.actuator_lag_hz > 1.0f && params.core.actuator_lag_hz < 50.0f);

    invalid = params;
    invalid.core.mass_kg = -1.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));

    invalid = params;
    invalid.wing_area_m2 = -1.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));

    invalid = params;
    invalid.max_thrust_n = -1.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));

    invalid_vehicle = *vehicle;
    invalid_vehicle.min_airspeed_mps = 0.0f;
    CHECK(expect_invalid_vehicle(&params, &invalid_vehicle));
    invalid_vehicle = *vehicle;
    invalid_vehicle.max_airspeed_mps = invalid_vehicle.min_airspeed_mps;
    CHECK(expect_invalid_vehicle(&params, &invalid_vehicle));
    invalid_vehicle = *vehicle;
    invalid_vehicle.max_airspeed_mps = 121.0f;
    CHECK(expect_invalid_vehicle(&params, &invalid_vehicle));
    invalid_vehicle = *vehicle;
    invalid_vehicle.min_actuator = -1.1f;
    CHECK(expect_invalid_vehicle(&params, &invalid_vehicle));
    invalid_vehicle = *vehicle;
    invalid_vehicle.max_actuator = 1.1f;
    CHECK(expect_invalid_vehicle(&params, &invalid_vehicle));
    invalid_vehicle = *vehicle;
    invalid_vehicle.min_actuator = invalid_vehicle.max_actuator;
    CHECK(expect_invalid_vehicle(&params, &invalid_vehicle));

    invalid = params;
    invalid.core.inertia_kgm2.x = 0.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.core.gravity_mps2 = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.core.air_density_kgpm3 = 0.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.core.actuator_lag_hz = -1.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.core.frame_mode = 99;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.core.earth_model = 99;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.core.earth_radius_m = 1000.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.wing_span_m = 0.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.mean_chord_m = INFINITY;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.drag_cd0 = -0.01f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.drag_cd_alpha = -0.01f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.lift_cl0 = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.lift_cl_alpha = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.lift_cl_alpha = 0.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.lift_cl_elevator = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.stall_alpha_rad = 0.0f;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.roll_aileron_nm = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.pitch_elevator_nm = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.yaw_rudder_nm = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.rate_damping_nms.y = NAN;
    CHECK(expect_invalid_sim(&invalid, vehicle));
    invalid = params;
    invalid.rate_damping_nms.y = -0.01f;
    CHECK(expect_invalid_sim(&invalid, vehicle));

    first = run_sequence(&params, SIM6DOF_FRAME_ECEF);
    second = run_sequence(&params, SIM6DOF_FRAME_ECEF);
    CHECK(sim_fixedwing_state_is_valid(&first));
    CHECK(sim_fixedwing_state_is_valid(&second));
    CHECK(close_state(first, second));
    first = run_sequence(&params, SIM6DOF_FRAME_NED);
    second = run_sequence(&params, SIM6DOF_FRAME_NED);
    CHECK(sim_fixedwing_state_is_valid(&first));
    CHECK(sim_fixedwing_state_is_valid(&second));
    CHECK(close_state(first, second));
    return 0;
}
