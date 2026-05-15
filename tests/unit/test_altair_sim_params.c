#include "altair_params.h"
#include "altair_sim_params.h"

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
    vehicle_params_t vehicle;
    sim_fixedwing_params_t params;
    altair_sim_param_store_t store;
    char error[160];

    altair_vehicle_params_default(&vehicle);
    altair_default_fixedwing_sim_params(&params);
    CHECK(altair_sim_params_validate(&params, &vehicle, error, sizeof(error)));

    params.core.mass_kg = -1.0f;
    CHECK(!altair_sim_params_validate(&params, &vehicle, error, sizeof(error)));

    altair_default_fixedwing_sim_params(&params);
    CHECK(altair_sim_params_apply(&params, "core.frame_mode", "ned", error, sizeof(error)));
    CHECK(params.core.frame_mode == SIM6DOF_FRAME_NED);
    CHECK(altair_sim_params_apply(&params, "max_thrust_n", "20.5", error, sizeof(error)));
    CHECK(near_real(params.max_thrust_n, 20.5f));
    CHECK(!altair_sim_params_apply(&params, "bad_key", "1", error, sizeof(error)));
    CHECK(!altair_sim_params_apply(&params, "max_thrust_n", "bad", error, sizeof(error)));

    CHECK(write_file("sim_params_valid.ini",
                     "[sim_params]\n"
                     "core.mass_kg = 3.1\n"
                     "core.frame_mode = ecef\n"
                     "max_thrust_n = 21\n"));
    CHECK(altair_sim_params_load("sim_params_valid.ini", &params, &vehicle, error, sizeof(error)));
    CHECK(near_real(params.core.mass_kg, 3.1f));
    CHECK(near_real(params.max_thrust_n, 21.0f));
    CHECK(params.core.frame_mode == SIM6DOF_FRAME_ECEF);

    CHECK(write_file("sim_params_invalid.ini",
                     "[sim_params]\n"
                     "core.mass_kg = 0\n"));
    CHECK(
        !altair_sim_params_load("sim_params_invalid.ini", &params, &vehicle, error, sizeof(error)));

    altair_default_fixedwing_sim_params(&params);
    altair_sim_param_store_init(&store, &params);
    altair_sim_param_store_begin(&store);
    CHECK(altair_sim_param_store_stage(&store, "max_thrust_n", "19", error, sizeof(error)));
    CHECK(!near_real(store.active.max_thrust_n, 19.0f));
    CHECK(altair_sim_param_store_commit(&store, &vehicle, error, sizeof(error)));
    CHECK(near_real(store.active.max_thrust_n, 19.0f));
    CHECK(altair_sim_param_store_generation(&store) == 1U);

    altair_sim_param_store_begin(&store);
    CHECK(altair_sim_param_store_stage(&store, "core.mass_kg", "-2", error, sizeof(error)));
    CHECK(!altair_sim_param_store_commit(&store, &vehicle, error, sizeof(error)));
    CHECK(near_real(store.active.max_thrust_n, 19.0f));

    return 0;
}
