#ifndef ALTAIR_SITL_CONDITIONS_H
#define ALTAIR_SITL_CONDITIONS_H

#include "common_types.h"
#include "sim_fixedwing.h"
#include "sitl_trim.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SITL_CONDITIONS_MAX_RULES 32U
#define SITL_CONDITIONS_MAX_ASSIGNMENTS_PER_RULE 16U
#define SITL_CONDITIONS_TARGET_MAX 64U
#define SITL_CONDITIONS_VALUE_MAX 64U

    typedef enum
    {
        SITL_CONDITION_LHS_TIME_S = 0,
        SITL_CONDITION_LHS_STEP
    } sitl_condition_lhs_t;

    typedef enum
    {
        SITL_CONDITION_CMP_GT = 0,
        SITL_CONDITION_CMP_GE,
        SITL_CONDITION_CMP_LT,
        SITL_CONDITION_CMP_LE,
        SITL_CONDITION_CMP_EQ,
        SITL_CONDITION_CMP_NE
    } sitl_condition_cmp_t;

    typedef struct
    {
        char target[SITL_CONDITIONS_TARGET_MAX];
        char value_text[SITL_CONDITIONS_VALUE_MAX];
        real_t value;
        int line_number;
    } sitl_condition_assignment_t;

    typedef struct
    {
        uint8_t has_when;
        sitl_condition_lhs_t lhs;
        sitl_condition_cmp_t cmp;
        double rhs;
        uint32_t assignment_count;
        sitl_condition_assignment_t assignments[SITL_CONDITIONS_MAX_ASSIGNMENTS_PER_RULE];
    } sitl_condition_rule_t;

    typedef struct
    {
        uint32_t rule_count;
        sitl_condition_rule_t rules[SITL_CONDITIONS_MAX_RULES];
    } sitl_conditions_t;

    typedef struct
    {
        double t_s;
        uint32_t step;
        rc_input_t *rc;
        fsw_input_t *input;
        vehicle_params_t *vehicle_params;
        sim_fixedwing_params_t *sim_params;
        sim_fixedwing_state_t *plant;
        sitl_trim_config_t *trim;
        uint8_t *mission_enabled;
        bayek_mission_plan_t *mission;
        uint8_t vehicle_params_dirty;
        uint8_t sim_params_dirty;
        uint8_t plant_ned_dirty;
        uint8_t plant_ecef_dirty;
        uint8_t mission_dirty;
    } sitl_condition_context_t;

    void sitl_conditions_default(sitl_conditions_t *conditions);
    int sitl_conditions_load(const char *path,
                             sitl_conditions_t *conditions,
                             char *error,
                             size_t error_size);
    int sitl_conditions_eval(const sitl_conditions_t *conditions,
                             sitl_condition_context_t *ctx,
                             char *error,
                             size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
