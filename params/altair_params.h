#ifndef ALTAIR_PARAMS_H
#define ALTAIR_PARAMS_H

#include "common_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        vehicle_params_t active;
        vehicle_params_t staged;
        uint32_t generation;
        uint8_t staged_valid;
    } altair_vehicle_param_store_t;

    const vehicle_params_t *altair_default_params(void);
    void altair_vehicle_params_default(vehicle_params_t *out);
    int
    altair_vehicle_params_validate(const vehicle_params_t *params, char *error, size_t error_size);
    int altair_vehicle_params_apply(vehicle_params_t *params,
                                    const char *key,
                                    const char *value_text,
                                    char *error,
                                    size_t error_size);
    int altair_vehicle_params_load(const char *path,
                                   vehicle_params_t *params,
                                   char *error,
                                   size_t error_size);

    void altair_vehicle_param_store_init(altair_vehicle_param_store_t *store,
                                         const vehicle_params_t *initial);
    const vehicle_params_t *
    altair_vehicle_param_store_active(const altair_vehicle_param_store_t *store);
    uint32_t altair_vehicle_param_store_generation(const altair_vehicle_param_store_t *store);
    void altair_vehicle_param_store_begin(altair_vehicle_param_store_t *store);
    int altair_vehicle_param_store_stage(altair_vehicle_param_store_t *store,
                                         const char *key,
                                         const char *value_text,
                                         char *error,
                                         size_t error_size);
    int altair_vehicle_param_store_commit(altair_vehicle_param_store_t *store,
                                          char *error,
                                          size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
