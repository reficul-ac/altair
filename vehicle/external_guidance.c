#include "external_guidance.h"

void altair_external_guidance_init(altair_external_guidance_state_t *state) {
    altair_external_guidance_reset(state);
}

void altair_external_guidance_reset(altair_external_guidance_state_t *state) {
    if (!state) {
        return;
    }
    state->step_count = 0U;
}

void altair_external_guidance_step(altair_external_guidance_state_t *state,
                                   altair_fsw_step_context_t *ctx) {
    if (!state || !ctx) {
        return;
    }
    ++state->step_count;
    ctx->external_guidance_available = 0U;
}
