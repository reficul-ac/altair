#include "altair_fsw.h"

#include "external_guidance.h"
#include "fault.h"
#include "guidance.h"
#include "nav.h"
#include "performance_management.h"
#include "relative_launch.h"

#include <string.h>

static actuator_cmd_t zero_actuators(void)
{
    actuator_cmd_t cmd;
    cmd.motor = 0.0f;
    cmd.aileron = 0.0f;
    cmd.elevator = 0.0f;
    cmd.rudder = 0.0f;
    return cmd;
}

void altair_fsw_init(altair_fsw_t *fsw, const bayek_vehicle_interface_t *vehicle)
{
    if (!fsw)
    {
        return;
    }
    memset(fsw, 0, sizeof(*fsw));
    fsw->vehicle = vehicle;
    fsw->params = vehicle ? vehicle->params : 0;
    bayek_control_init(&fsw->control);
    bayek_mission_init(&fsw->mission);
    altair_relative_launch_init(&fsw->relative_launch);
    altair_external_guidance_init(&fsw->external_guidance);
    altair_performance_management_init(&fsw->performance_management);
    altair_fsw_reset(fsw);
}

void altair_fsw_reset(altair_fsw_t *fsw)
{
    if (!fsw)
    {
        return;
    }
    bayek_control_reset(&fsw->control);
    bayek_nav_reset(&fsw->estimate);
    altair_relative_launch_reset(&fsw->relative_launch);
    altair_external_guidance_reset(&fsw->external_guidance);
    altair_performance_management_reset(&fsw->performance_management);
}

void altair_fsw_step(altair_fsw_t *fsw, const fsw_input_t *in, fsw_output_t *out)
{
    altair_fsw_step_context_t step;

    if (!out)
    {
        return;
    }

    if (!fsw || !fsw->vehicle || !fsw->params)
    {
        out->estimate = fsw ? fsw->estimate : (state_estimate_t){0};
        out->mode = FSW_MODE_FAILSAFE;
        out->actuators = zero_actuators();
        return;
    }

    memset(&step, 0, sizeof(step));
    step.input = in;
    step.input_valid = bayek_fault_input_is_valid(in);
    step.selected_mode = bayek_fault_select_mode(in, step.input_valid);

    if (step.input_valid)
    {
        bayek_nav_update(in, &fsw->estimate);
    }
    step.estimate = fsw->estimate;
    step.output.estimate = fsw->estimate;
    step.output.mode = step.selected_mode;

    altair_relative_launch_step(&fsw->relative_launch, &step);

    if (step.output.mode == FSW_MODE_DISARMED || step.output.mode == FSW_MODE_FAILSAFE)
    {
        step.actuators = fsw->vehicle->safe_actuators(fsw->params);
        step.output.actuators = step.actuators;
        *out = step.output;
        return;
    }

    if (step.output.mode == FSW_MODE_MANUAL)
    {
        step.actuators = fsw->vehicle->mix_manual(&in->rc);
        step.output.actuators = step.actuators;
        *out = step.output;
        return;
    }

    if (step.output.mode == FSW_MODE_MISSION)
    {
        if (!bayek_mission_select_active_waypoint(
                &fsw->mission, in, &fsw->estimate, fsw->params, &step.guidance_setpoint))
        {
            step.output.mode = FSW_MODE_FAILSAFE;
            step.selected_mode = FSW_MODE_FAILSAFE;
            step.actuators = fsw->vehicle->safe_actuators(fsw->params);
            step.output.actuators = step.actuators;
            *out = step.output;
            return;
        }
        step.guidance_setpoint_valid = 1U;
    }
    else
    {
        step.guidance_setpoint = bayek_guidance_stabilize_from_rc(&in->rc, fsw->params);
        step.guidance_setpoint_valid = 1U;
    }

    altair_external_guidance_step(&fsw->external_guidance, &step);
    altair_performance_management_step(&fsw->performance_management, &step);

    if (!step.guidance_setpoint_valid)
    {
        step.output.mode = FSW_MODE_FAILSAFE;
        step.actuators = fsw->vehicle->safe_actuators(fsw->params);
        step.output.actuators = step.actuators;
        *out = step.output;
        return;
    }

    step.control_request =
        bayek_control_stabilize_step(&fsw->control, &step.guidance_setpoint, &fsw->estimate, in);
    step.actuators = fsw->vehicle->mix_control(step.control_request.throttle,
                                               step.control_request.roll,
                                               step.control_request.pitch,
                                               step.control_request.yaw);
    step.output.actuators = step.actuators;
    bayek_mission_get_status(&fsw->mission, &step.mission_status);
    *out = step.output;
}

int altair_fsw_set_mission(altair_fsw_t *fsw, const bayek_mission_plan_t *mission)
{
    if (!fsw)
    {
        return 0;
    }
    return bayek_mission_set(&fsw->mission, mission);
}

void altair_fsw_clear_mission(altair_fsw_t *fsw)
{
    if (!fsw)
    {
        return;
    }
    bayek_mission_clear(&fsw->mission);
}

void altair_fsw_get_mission_status(const altair_fsw_t *fsw, bayek_mission_status_t *status)
{
    bayek_mission_get_status(fsw ? &fsw->mission : 0, status);
}
