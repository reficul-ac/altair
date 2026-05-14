#include "altair_params.h"
#include "altair_sim_model.h"
#include "sitl_trim.h"

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

int main(void)
{
    sim_fixedwing_params_t params;
    sim_fixedwing_state_t plant;
    vehicle_params_t vehicle_params;
    sitl_trim_config_t config;
    sitl_trim_status_t status;
    char error[200];

    vehicle_params = *altair_default_params();
    altair_fixedwing_sim_params(&params);
    sim_fixedwing_init_default(&plant);
    sitl_trim_config_default(&config);
    config.enabled = 1U;
    config.mode = SITL_TRIM_MODE_FIXEDWING_LEVEL;
    config.target_airspeed_mps = plant.last_airspeed_mps;
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

    sim_fixedwing_init_default(&plant);
    config.target_airspeed_mps = vehicle_params.max_airspeed_mps + 10.0f;
    CHECK(!sitl_trim_fixedwing_level(
        &plant, &params, &vehicle_params, &config, &status, error, sizeof(error)));
    CHECK(status.failed == 1U);

    return 0;
}
