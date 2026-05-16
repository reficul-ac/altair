#include "performance_management.h"

void altair_performance_management_init(altair_performance_management_state_t *state)
{
    altair_performance_management_reset(state);
}

void altair_performance_management_reset(altair_performance_management_state_t *state)
{
    if (!state)
    {
        return;
    }
    state->step_count = 0U;
}

void altair_performance_management_step(altair_performance_management_state_t *state,
                                        altair_fsw_step_context_t *ctx)
{
    if (!state || !ctx)
    {
        return;
    }
    ++state->step_count;
    ctx->performance_management_ran = 1U;
}
