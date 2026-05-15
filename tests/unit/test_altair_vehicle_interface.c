#include "altair_params.h"
#include "altair_fsw.h"
#include "altair_vehicle.h"

#include <stdio.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d\n", __FILE__, __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    vehicle_params_t custom = *altair_default_params();
    const bayek_vehicle_interface_t *vehicle;

    custom.max_airspeed_mps = 32.0f;
    custom.max_roll_rad = 0.65f;

    vehicle = altair_vehicle_interface_with_params(&custom);
    CHECK(vehicle->params == &custom);
    CHECK(vehicle->params->max_airspeed_mps == 32.0f);
    CHECK(vehicle->params->max_roll_rad == 0.65f);

    custom.max_airspeed_mps = 31.0f;
    CHECK(vehicle->params->max_airspeed_mps == 31.0f);

    vehicle = altair_vehicle_interface();
    CHECK(vehicle->params == altair_default_params());
    CHECK(vehicle->params->max_airspeed_mps != 31.0f);

    {
        altair_fsw_t fsw;
        vehicle_params_t runtime = *altair_default_params();
        const bayek_vehicle_interface_t *runtime_vehicle;

        runtime.max_airspeed_mps = 29.0f;
        runtime_vehicle = altair_vehicle_interface_with_params(&runtime);
        altair_fsw_init(&fsw, runtime_vehicle);
        CHECK(fsw.params == &runtime);
        altair_fsw_reset(&fsw);
        CHECK(fsw.params == &runtime);
    }

    return 0;
}
