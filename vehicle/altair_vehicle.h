#ifndef ALTAIR_VEHICLE_H
#define ALTAIR_VEHICLE_H

#include "fsw.h"

#ifdef __cplusplus
extern "C"
{
#endif

    const bayek_vehicle_interface_t *altair_vehicle_interface(void);
    const bayek_vehicle_interface_t *
    altair_vehicle_interface_with_params(const vehicle_params_t *params);

#ifdef __cplusplus
}
#endif

#endif
