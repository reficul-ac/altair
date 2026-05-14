#include "altair_mixer.h"

#include "altair_limits.h"
#include "math_utils.h"

actuator_cmd_t altair_mix_manual(const rc_input_t *rc)
{
    return altair_mix_control(rc->throttle, rc->roll, rc->pitch, rc->yaw);
}

actuator_cmd_t
altair_mix_control(real_t throttle, real_t roll_cmd, real_t pitch_cmd, real_t yaw_cmd)
{
    actuator_cmd_t cmd;
    cmd.motor = clamp_real(throttle, ALTAIR_MOTOR_MIN, ALTAIR_MOTOR_MAX);
    cmd.aileron = clamp_real(roll_cmd, ALTAIR_SURFACE_MIN, ALTAIR_SURFACE_MAX);
    cmd.elevator = clamp_real(pitch_cmd, ALTAIR_SURFACE_MIN, ALTAIR_SURFACE_MAX);
    cmd.rudder = clamp_real(yaw_cmd, ALTAIR_SURFACE_MIN, ALTAIR_SURFACE_MAX);
    return cmd;
}

actuator_cmd_t altair_safe_actuators(const vehicle_params_t *params)
{
    actuator_cmd_t cmd;
    cmd.motor = params ? params->safe_motor : 0.0f;
    cmd.aileron = params ? params->safe_surface : 0.0f;
    cmd.elevator = params ? params->safe_surface : 0.0f;
    cmd.rudder = params ? params->safe_surface : 0.0f;
    return altair_mix_control(cmd.motor, cmd.aileron, cmd.elevator, cmd.rudder);
}
