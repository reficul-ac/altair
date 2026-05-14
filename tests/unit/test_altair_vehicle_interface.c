#include "altair_params.h"
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

    return 0;
}
