#ifndef ALTAIR_FSW_H
#define ALTAIR_FSW_H

#include "control.h"
#include "fsw.h"
#include "mission.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint32_t step_count;
    } altair_relative_launch_state_t;

    typedef struct
    {
        uint32_t step_count;
    } altair_external_guidance_state_t;

    typedef struct
    {
        uint32_t step_count;
    } altair_performance_management_state_t;

    typedef struct
    {
        const fsw_input_t *input;
        int input_valid;
        fsw_mode_t selected_mode;
        state_estimate_t estimate;
        bayek_mission_status_t mission_status;
        bayek_guidance_setpoint_t guidance_setpoint;
        uint8_t guidance_setpoint_valid;
        uint8_t external_guidance_available;
        bayek_control_request_t control_request;
        actuator_cmd_t actuators;
        fsw_output_t output;
        uint8_t relative_launch_ran;
        uint8_t performance_management_ran;
    } altair_fsw_step_context_t;

    typedef struct
    {
        const bayek_vehicle_interface_t *vehicle;
        const vehicle_params_t *params;
        bayek_control_state_t control;
        state_estimate_t estimate;
        bayek_mission_state_t mission;
        altair_relative_launch_state_t relative_launch;
        altair_external_guidance_state_t external_guidance;
        altair_performance_management_state_t performance_management;
    } altair_fsw_t;

    void altair_fsw_init(altair_fsw_t *fsw, const bayek_vehicle_interface_t *vehicle);
    void altair_fsw_reset(altair_fsw_t *fsw);
    void altair_fsw_step(altair_fsw_t *fsw, const fsw_input_t *in, fsw_output_t *out);
    int altair_fsw_set_mission(altair_fsw_t *fsw, const bayek_mission_plan_t *mission);
    void altair_fsw_clear_mission(altair_fsw_t *fsw);
    void altair_fsw_get_mission_status(const altair_fsw_t *fsw, bayek_mission_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
