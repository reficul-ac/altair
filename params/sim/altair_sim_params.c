#include "altair_sim_params.h"

#include <stddef.h>

void altair_default_fixedwing_sim_params(sim_fixedwing_params_t *params)
{
    if (params == NULL)
    {
        return;
    }

    sim_fixedwing_default_params(params);
    params->core.mass_kg = 2.5f;
    params->wing_area_m2 = 0.45f;
}
