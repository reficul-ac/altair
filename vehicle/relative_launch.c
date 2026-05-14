#include "relative_launch.h"

void altair_relative_launch_init(altair_relative_launch_state_t *state) {
    altair_relative_launch_reset(state);
}

void altair_relative_launch_reset(altair_relative_launch_state_t *state) {
    if (!state) {
        return;
    }
    state->step_count = 0U;
}

void altair_relative_launch_step(altair_relative_launch_state_t *state,
                                 altair_fsw_step_context_t *ctx) {
    if (!state || !ctx) {
        return;
    }
    ++state->step_count;
    ctx->relative_launch_ran = 1U;
}
