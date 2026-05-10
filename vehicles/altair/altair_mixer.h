#ifndef ALTAIR_MIXER_H
#define ALTAIR_MIXER_H

#include "common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

actuator_cmd_t altair_mix_manual(const rc_input_t *rc);
actuator_cmd_t altair_mix_control(real_t throttle, real_t roll_cmd, real_t pitch_cmd, real_t yaw_cmd);
actuator_cmd_t altair_safe_actuators(const vehicle_params_t *params);

#ifdef __cplusplus
}
#endif

#endif
