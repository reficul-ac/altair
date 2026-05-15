#ifndef ALTAIR_SIM_PARAMS_H
#define ALTAIR_SIM_PARAMS_H

#include "sim_fixedwing.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        sim_fixedwing_params_t active;
        sim_fixedwing_params_t staged;
        uint32_t generation;
        uint8_t staged_valid;
    } altair_sim_param_store_t;

    void altair_default_fixedwing_sim_params(sim_fixedwing_params_t *params);
    int altair_sim_params_validate(const sim_fixedwing_params_t *params,
                                   const vehicle_params_t *vehicle,
                                   char *error,
                                   size_t error_size);
    int altair_sim_params_apply(sim_fixedwing_params_t *params,
                                const char *key,
                                const char *value_text,
                                char *error,
                                size_t error_size);
    int altair_sim_params_load(const char *path,
                               sim_fixedwing_params_t *params,
                               const vehicle_params_t *vehicle,
                               char *error,
                               size_t error_size);

    void altair_sim_param_store_init(altair_sim_param_store_t *store,
                                     const sim_fixedwing_params_t *initial);
    const sim_fixedwing_params_t *
    altair_sim_param_store_active(const altair_sim_param_store_t *store);
    uint32_t altair_sim_param_store_generation(const altair_sim_param_store_t *store);
    void altair_sim_param_store_begin(altair_sim_param_store_t *store);
    int altair_sim_param_store_stage(altair_sim_param_store_t *store,
                                     const char *key,
                                     const char *value_text,
                                     char *error,
                                     size_t error_size);
    int altair_sim_param_store_commit(altair_sim_param_store_t *store,
                                      const vehicle_params_t *vehicle,
                                      char *error,
                                      size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
