#ifndef ALTAIR_SITL_CASE_H
#define ALTAIR_SITL_CASE_H

#include "common_types.h"
#include "sim_fixedwing.h"
#include "sitl_initial_conditions.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SITL_CASE_TEXT_MAX 32U
#define SITL_CASE_PATH_MAX 128U

    typedef struct
    {
        uint8_t has_scenario;
        char scenario[SITL_CASE_TEXT_MAX];
        uint8_t has_profile;
        char profile[SITL_CASE_TEXT_MAX];
        uint8_t has_duration_s;
        double duration_s;
        uint8_t has_dt_s;
        double dt_s;
        uint8_t has_seed;
        uint32_t seed;
        uint8_t has_frame_mode;
        int frame_mode;
        uint8_t has_condition_file;
        char condition_file[SITL_CASE_PATH_MAX];
    } sitl_case_run_t;

    typedef struct
    {
        sitl_case_run_t run;
        uint8_t has_initial;
        sitl_initial_conditions_t initial;
        uint8_t has_vehicle_params;
        vehicle_params_t vehicle_params;
        uint8_t has_sim_params;
        sim_fixedwing_params_t sim_params;
        uint8_t has_mission;
        uint8_t mission_enabled;
        bayek_mission_plan_t mission;
    } sitl_case_t;

    void sitl_case_default(sitl_case_t *case_file);
    int sitl_case_load(const char *path, sitl_case_t *case_file, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
