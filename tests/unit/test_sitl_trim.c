#include "altair_params.h"
#include "altair_sim_model.h"
#include "math_utils.h"
#include "sitl_trim.h"

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

static int near_real(real_t a, real_t b, real_t tol)
{
    return fabsf(a - b) <= tol;
}

static int near_vec3(vec3_t a, vec3_t b, real_t tol)
{
    return near_real(a.x, b.x, tol) && near_real(a.y, b.y, tol) && near_real(a.z, b.z, tol);
}

static int near_actuator(actuator_cmd_t a, actuator_cmd_t b, real_t tol)
{
    return near_real(a.motor, b.motor, tol) && near_real(a.aileron, b.aileron, tol) &&
           near_real(a.elevator, b.elevator, tol) && near_real(a.rudder, b.rudder, tol);
}

static int near_state(sim_fixedwing_state_t a, sim_fixedwing_state_t b, real_t tol)
{
    return near_vec3(a.body.position_ned_m, b.body.position_ned_m, tol) &&
           near_vec3(a.body.velocity_ned_mps, b.body.velocity_ned_mps, tol) &&
           near_vec3(a.body.omega_body_rps, b.body.omega_body_rps, tol) &&
           near_actuator(a.body.actuator_state, b.body.actuator_state, tol) &&
           near_vec3(a.last_force_body_n, b.last_force_body_n, tol) &&
           near_vec3(a.last_moment_body_nm, b.last_moment_body_nm, tol) &&
           near_real(a.last_airspeed_mps, b.last_airspeed_mps, tol);
}

int main(void)
{
    sim_fixedwing_params_t params;
    sim_fixedwing_state_t plant;
    sim_fixedwing_state_t before;
    vehicle_params_t vehicle_params;
    sitl_trim_config_t config;
    sitl_trim_status_t status;
    actuator_cmd_t hold_cmd;
    real_t hold_initial_airspeed_mps;
    real_t hold_final_airspeed_mps;
    char error[200];
    int i;

    vehicle_params = *altair_default_params();
    altair_fixedwing_sim_params(&params);

    sim_fixedwing_init_default(&plant);
    before = plant;
    sitl_trim_config_default(&config);
    config.enabled = 0U;
    CHECK(sitl_trim_fixedwing_level(
        &plant, &params, &vehicle_params, &config, &status, error, sizeof(error)));
    CHECK(status.active == 0U);
    CHECK(status.achieved == 0U);
    CHECK(status.failed == 0U);
    CHECK(near_state(plant, before, 1.0e-6f));

    sim_fixedwing_init_default(&plant);
    sitl_trim_config_default(&config);
    config.enabled = 1U;
    config.mode = SITL_TRIM_MODE_FIXEDWING_LEVEL;
    config.target_airspeed_mps = 0.0f;
    config.tolerance = 7.5e-3f;
    config.max_iterations = 25U;
    CHECK(sitl_trim_fixedwing_level(
        &plant, &params, &vehicle_params, &config, &status, error, sizeof(error)));
    CHECK(status.achieved == 1U);
    CHECK(status.failed == 0U);
    CHECK(status.residual_norm <= config.tolerance);
    CHECK(status.actuators.motor >= 0.0f && status.actuators.motor <= 1.0f);
    CHECK(status.actuators.aileron >= vehicle_params.min_actuator &&
          status.actuators.aileron <= vehicle_params.max_actuator);
    CHECK(status.actuators.elevator >= vehicle_params.min_actuator &&
          status.actuators.elevator <= vehicle_params.max_actuator);
    CHECK(status.actuators.rudder >= vehicle_params.min_actuator &&
          status.actuators.rudder <= vehicle_params.max_actuator);
    CHECK(fabsf(status.pitch_rad) <= 0.35f);

    hold_cmd = status.actuators;
    hold_initial_airspeed_mps = plant.last_airspeed_mps;
    for (i = 0; i < 50; ++i)
    {
        CHECK(sim_fixedwing_step(&plant, &params, &hold_cmd, 0.01f));
    }
    hold_final_airspeed_mps = plant.last_airspeed_mps;
    CHECK(fabsf(plant.body.omega_body_rps.x) < 0.25f);
    CHECK(fabsf(plant.body.omega_body_rps.y) < 0.25f);
    CHECK(fabsf(plant.body.omega_body_rps.z) < 0.25f);
    CHECK(fabsf(hold_final_airspeed_mps - hold_initial_airspeed_mps) < 1.5f);

    sim_fixedwing_init_default(&plant);
    config.mode = (sitl_trim_mode_t)99;
    config.target_airspeed_mps = plant.last_airspeed_mps;
    CHECK(!sitl_trim_fixedwing_level(
        &plant, &params, &vehicle_params, &config, &status, error, sizeof(error)));
    CHECK(status.failed == 1U);

    sim_fixedwing_init_default(&plant);
    config.mode = SITL_TRIM_MODE_FIXEDWING_LEVEL;
    config.target_airspeed_mps = vehicle_params.max_airspeed_mps + 10.0f;
    CHECK(!sitl_trim_fixedwing_level(
        &plant, &params, &vehicle_params, &config, &status, error, sizeof(error)));
    CHECK(status.failed == 1U);

    return 0;
}
