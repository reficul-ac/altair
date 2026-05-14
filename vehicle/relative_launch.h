#ifndef ALTAIR_RELATIVE_LAUNCH_H
#define ALTAIR_RELATIVE_LAUNCH_H

#include "altair_fsw.h"

void altair_relative_launch_init(altair_relative_launch_state_t *state);
void altair_relative_launch_reset(altair_relative_launch_state_t *state);
void altair_relative_launch_step(altair_relative_launch_state_t *state,
                                 altair_fsw_step_context_t *ctx);

#endif
