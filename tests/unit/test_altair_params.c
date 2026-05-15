#include "altair_params.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d\n", __FILE__, __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
    {
        return 0;
    }
    if (fputs(text, file) < 0)
    {
        (void)fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int near_real(real_t a, real_t b)
{
    real_t d = a - b;
    return d > -1.0e-5f && d < 1.0e-5f;
}

int main(void)
{
    vehicle_params_t params;
    altair_vehicle_param_store_t store;
    char error[160];

    altair_vehicle_params_default(&params);
    CHECK(altair_vehicle_params_validate(&params, error, sizeof(error)));

    params.min_airspeed_mps = params.max_airspeed_mps;
    CHECK(!altair_vehicle_params_validate(&params, error, sizeof(error)));
    CHECK(strstr(error, "airspeed") != NULL);

    altair_vehicle_params_default(&params);
    CHECK(altair_vehicle_params_apply(&params, "max_airspeed_mps", "31.5", error, sizeof(error)));
    CHECK(near_real(params.max_airspeed_mps, 31.5f));
    CHECK(!altair_vehicle_params_apply(&params, "unknown", "1", error, sizeof(error)));
    CHECK(!altair_vehicle_params_apply(&params, "max_airspeed_mps", "nope", error, sizeof(error)));

    CHECK(write_file("vehicle_params_valid.ini",
                     "[ignored]\n"
                     "max_airspeed_mps = 99\n"
                     "[vehicle_params]\n"
                     "min_airspeed_mps = 9\n"
                     "max_airspeed_mps = 34\n"
                     "safe_surface = 0.1\n"));
    CHECK(altair_vehicle_params_load("vehicle_params_valid.ini", &params, error, sizeof(error)));
    CHECK(near_real(params.min_airspeed_mps, 9.0f));
    CHECK(near_real(params.max_airspeed_mps, 34.0f));
    CHECK(near_real(params.safe_surface, 0.1f));

    CHECK(write_file("vehicle_params_invalid.ini",
                     "[vehicle_params]\n"
                     "min_airspeed_mps = 40\n"
                     "max_airspeed_mps = 30\n"));
    CHECK(!altair_vehicle_params_load("vehicle_params_invalid.ini", &params, error, sizeof(error)));

    altair_vehicle_params_default(&params);
    altair_vehicle_param_store_init(&store, &params);
    altair_vehicle_param_store_begin(&store);
    CHECK(altair_vehicle_param_store_stage(&store, "max_airspeed_mps", "30", error, sizeof(error)));
    CHECK(near_real(store.active.max_airspeed_mps, params.max_airspeed_mps));
    CHECK(altair_vehicle_param_store_commit(&store, error, sizeof(error)));
    CHECK(near_real(store.active.max_airspeed_mps, 30.0f));
    CHECK(altair_vehicle_param_store_generation(&store) == 1U);

    altair_vehicle_param_store_begin(&store);
    CHECK(altair_vehicle_param_store_stage(&store, "min_airspeed_mps", "31", error, sizeof(error)));
    CHECK(!altair_vehicle_param_store_commit(&store, error, sizeof(error)));
    CHECK(near_real(store.active.max_airspeed_mps, 30.0f));
    CHECK(!near_real(store.active.min_airspeed_mps, 31.0f));

    return 0;
}
