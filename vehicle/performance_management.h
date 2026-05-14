#ifndef ALTAIR_PERFORMANCE_MANAGEMENT_H
#define ALTAIR_PERFORMANCE_MANAGEMENT_H

#include "altair_fsw.h"

void altair_performance_management_init(altair_performance_management_state_t *state);
void altair_performance_management_reset(altair_performance_management_state_t *state);
void altair_performance_management_step(altair_performance_management_state_t *state,
                                        altair_fsw_step_context_t *ctx);

#endif
