#ifndef ALTAIR_SIM_MODEL_H
#define ALTAIR_SIM_MODEL_H

#include "altair_params.h"
#include "sim_fixedwing.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void altair_fixedwing_sim_params(sim_fixedwing_params_t *params);
int altair_fixedwing_sim_params_are_valid(const sim_fixedwing_params_t *params,
                                          const vehicle_params_t *vehicle,
                                          char *error,
                                          size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
