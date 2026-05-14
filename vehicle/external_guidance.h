#ifndef ALTAIR_EXTERNAL_GUIDANCE_H
#define ALTAIR_EXTERNAL_GUIDANCE_H

#include "altair_fsw.h"

void altair_external_guidance_init(altair_external_guidance_state_t *state);
void altair_external_guidance_reset(altair_external_guidance_state_t *state);
void altair_external_guidance_step(altair_external_guidance_state_t *state,
                                   altair_fsw_step_context_t *ctx);

#endif
