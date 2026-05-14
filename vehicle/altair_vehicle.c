#include "altair_vehicle.h"

#include "altair_mixer.h"
#include "altair_params.h"

#include <stddef.h>

static const bayek_vehicle_interface_t k_altair_vehicle = {
    0, altair_mix_manual, altair_mix_control, altair_safe_actuators};

const bayek_vehicle_interface_t *altair_vehicle_interface(void)
{
    return altair_vehicle_interface_with_params(altair_default_params());
}

const bayek_vehicle_interface_t *
altair_vehicle_interface_with_params(const vehicle_params_t *params)
{
    static bayek_vehicle_interface_t vehicle;
    vehicle = k_altair_vehicle;
    vehicle.params = params != NULL ? params : altair_default_params();
    return &vehicle;
}
