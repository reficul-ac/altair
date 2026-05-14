#ifndef ALTAIR_SITL_TRIM_H
#define ALTAIR_SITL_TRIM_H

#include "altair_vehicle.h"
#include "sim_fixedwing.h"
#include "trim_solver.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        SITL_TRIM_MODE_NONE = 0,
        SITL_TRIM_MODE_FIXEDWING_LEVEL = 1
    } sitl_trim_mode_t;

    typedef struct
    {
        uint8_t enabled;
        sitl_trim_mode_t mode;
        uint8_t fail_on_error;
        real_t target_airspeed_mps;
        real_t tolerance;
        uint32_t max_iterations;
        real_t settle_time_s;
        real_t eval_time_s;
    } sitl_trim_config_t;

    typedef struct
    {
        uint8_t active;
        uint8_t achieved;
        uint8_t failed;
        uint32_t iteration_count;
        real_t residual_norm;
        bayek_trim_status_t solver_status;
        actuator_cmd_t actuators;
        real_t pitch_rad;
    } sitl_trim_status_t;

    void sitl_trim_config_default(sitl_trim_config_t *config);
    void sitl_trim_status_default(sitl_trim_status_t *status);
    int sitl_trim_parse_mode(const char *text, sitl_trim_mode_t *mode);
    int sitl_trim_fixedwing_level(sim_fixedwing_state_t *plant,
                                  const sim_fixedwing_params_t *params,
                                  const vehicle_params_t *vehicle_params,
                                  const sitl_trim_config_t *config,
                                  sitl_trim_status_t *status,
                                  char *error,
                                  size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
