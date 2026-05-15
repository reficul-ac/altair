#include "altair_sim_params.h"

#include "altair_mixer.h"
#include "altair_params.h"
#include "math_utils.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALTAIR_SIM_PARAM_LINE_MAX 256

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

static void set_error(char *error, size_t error_size, const char *message, const char *detail)
{
    if (error != NULL && error_size > 0U)
    {
        (void)snprintf(
            error, error_size, "%s%s%s", message, detail ? ": " : "", detail ? detail : "");
    }
}

static char *trim(char *text)
{
    char *end;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
    {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        --end;
    }
    *end = '\0';
    return text;
}

static int parse_real(const char *text, real_t *value)
{
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0')
    {
        return 0;
    }
    *value = (real_t)parsed;
    return real_is_finite(*value);
}

static int parse_int_text(const char *text, int *value)
{
    if (strcmp(text, "ned") == 0)
    {
        *value = SIM6DOF_FRAME_NED;
        return 1;
    }
    if (strcmp(text, "ecef") == 0)
    {
        *value = SIM6DOF_FRAME_ECEF;
        return 1;
    }
    {
        char *end = NULL;
        long parsed;
        errno = 0;
        parsed = strtol(text, &end, 10);
        if (errno != 0 || end == text || *end != '\0')
        {
            return 0;
        }
        *value = (int)parsed;
        return 1;
    }
}

static int vec3_is_positive(vec3_t v)
{
    return real_is_finite(v.x) && real_is_finite(v.y) && real_is_finite(v.z) && v.x > 0.0f &&
           v.y > 0.0f && v.z > 0.0f;
}

int altair_sim_params_validate(const sim_fixedwing_params_t *params,
                               const vehicle_params_t *vehicle,
                               char *error,
                               size_t error_size)
{
    actuator_cmd_t safe;

    if (params == NULL)
    {
        set_error(error, error_size, "sim params are null", NULL);
        return 0;
    }
    if (!altair_vehicle_params_validate(vehicle, error, error_size))
    {
        return 0;
    }
    if (!real_is_finite(params->core.mass_kg) || params->core.mass_kg <= 0.0f ||
        !vec3_is_positive(params->core.inertia_kgm2) ||
        !real_is_finite(params->core.gravity_mps2) || params->core.gravity_mps2 <= 0.0f ||
        !real_is_finite(params->core.air_density_kgpm3) || params->core.air_density_kgpm3 <= 0.0f ||
        !real_is_finite(params->core.actuator_lag_hz) || params->core.actuator_lag_hz <= 0.0f)
    {
        set_error(error, error_size, "sim core dynamics params are invalid", NULL);
        return 0;
    }
    if (params->core.frame_mode != SIM6DOF_FRAME_NED &&
        params->core.frame_mode != SIM6DOF_FRAME_ECEF)
    {
        set_error(error, error_size, "sim frame mode is invalid", NULL);
        return 0;
    }
    if (params->core.earth_model != SIM6DOF_EARTH_SPHERICAL ||
        !real_is_finite(params->core.earth_radius_m) || params->core.earth_radius_m <= 1000.0f)
    {
        set_error(error, error_size, "sim earth params are invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->wing_area_m2) || params->wing_area_m2 <= 0.0f ||
        !real_is_finite(params->wing_span_m) || params->wing_span_m <= 0.0f ||
        !real_is_finite(params->mean_chord_m) || params->mean_chord_m <= 0.0f ||
        !real_is_finite(params->max_thrust_n) || params->max_thrust_n <= 0.0f)
    {
        set_error(error, error_size, "sim geometry and thrust are invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->drag_cd0) || params->drag_cd0 < 0.0f ||
        !real_is_finite(params->drag_cd_alpha) || params->drag_cd_alpha < 0.0f ||
        !real_is_finite(params->lift_cl0) || !real_is_finite(params->lift_cl_alpha) ||
        !real_is_finite(params->lift_cl_elevator) || !real_is_finite(params->stall_alpha_rad) ||
        params->stall_alpha_rad <= 0.0f)
    {
        set_error(error, error_size, "sim aerodynamic coefficients are invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->roll_aileron_nm) || !real_is_finite(params->pitch_elevator_nm) ||
        !real_is_finite(params->yaw_rudder_nm) || !real_is_finite(params->rate_damping_nms.x) ||
        !real_is_finite(params->rate_damping_nms.y) || !real_is_finite(params->rate_damping_nms.z))
    {
        set_error(error, error_size, "sim control moments or damping are non-finite", NULL);
        return 0;
    }
    safe = altair_safe_actuators(vehicle);
    if (safe.motor < 0.0f || safe.motor > 1.0f || safe.aileron < vehicle->min_actuator ||
        safe.aileron > vehicle->max_actuator || safe.elevator < vehicle->min_actuator ||
        safe.elevator > vehicle->max_actuator || safe.rudder < vehicle->min_actuator ||
        safe.rudder > vehicle->max_actuator)
    {
        set_error(error, error_size, "safe actuator outputs violate vehicle/sim ranges", NULL);
        return 0;
    }
    if (error != NULL && error_size > 0U)
    {
        error[0] = '\0';
    }
    return 1;
}

int altair_sim_params_apply(sim_fixedwing_params_t *params,
                            const char *key,
                            const char *value_text,
                            char *error,
                            size_t error_size)
{
    real_t value;
    if (params == NULL || key == NULL || value_text == NULL)
    {
        set_error(error, error_size, "invalid sim param apply arguments", NULL);
        return 0;
    }
    if (strcmp(key, "core.frame_mode") == 0)
    {
        int frame_mode;
        if (!parse_int_text(value_text, &frame_mode))
        {
            set_error(error, error_size, "invalid sim frame mode", value_text);
            return 0;
        }
        params->core.frame_mode = frame_mode;
        return 1;
    }
    if (strcmp(key, "core.earth_model") == 0)
    {
        int earth_model;
        if (!parse_int_text(value_text, &earth_model))
        {
            set_error(error, error_size, "invalid sim earth model", value_text);
            return 0;
        }
        params->core.earth_model = earth_model;
        return 1;
    }
    if (!parse_real(value_text, &value))
    {
        set_error(error, error_size, "invalid sim param value", value_text);
        return 0;
    }
    if (strcmp(key, "core.mass_kg") == 0)
    {
        params->core.mass_kg = value;
    }
    else if (strcmp(key, "core.inertia_kgm2.x") == 0)
    {
        params->core.inertia_kgm2.x = value;
    }
    else if (strcmp(key, "core.inertia_kgm2.y") == 0)
    {
        params->core.inertia_kgm2.y = value;
    }
    else if (strcmp(key, "core.inertia_kgm2.z") == 0)
    {
        params->core.inertia_kgm2.z = value;
    }
    else if (strcmp(key, "core.gravity_mps2") == 0)
    {
        params->core.gravity_mps2 = value;
    }
    else if (strcmp(key, "core.air_density_kgpm3") == 0)
    {
        params->core.air_density_kgpm3 = value;
    }
    else if (strcmp(key, "core.actuator_lag_hz") == 0)
    {
        params->core.actuator_lag_hz = value;
    }
    else if (strcmp(key, "core.earth_radius_m") == 0)
    {
        params->core.earth_radius_m = value;
    }
    else if (strcmp(key, "wing_area_m2") == 0)
    {
        params->wing_area_m2 = value;
    }
    else if (strcmp(key, "wing_span_m") == 0)
    {
        params->wing_span_m = value;
    }
    else if (strcmp(key, "mean_chord_m") == 0)
    {
        params->mean_chord_m = value;
    }
    else if (strcmp(key, "max_thrust_n") == 0)
    {
        params->max_thrust_n = value;
    }
    else if (strcmp(key, "drag_cd0") == 0)
    {
        params->drag_cd0 = value;
    }
    else if (strcmp(key, "drag_cd_alpha") == 0)
    {
        params->drag_cd_alpha = value;
    }
    else if (strcmp(key, "lift_cl0") == 0)
    {
        params->lift_cl0 = value;
    }
    else if (strcmp(key, "lift_cl_alpha") == 0)
    {
        params->lift_cl_alpha = value;
    }
    else if (strcmp(key, "lift_cl_elevator") == 0)
    {
        params->lift_cl_elevator = value;
    }
    else if (strcmp(key, "stall_alpha_rad") == 0)
    {
        params->stall_alpha_rad = value;
    }
    else if (strcmp(key, "roll_aileron_nm") == 0)
    {
        params->roll_aileron_nm = value;
    }
    else if (strcmp(key, "pitch_elevator_nm") == 0)
    {
        params->pitch_elevator_nm = value;
    }
    else if (strcmp(key, "yaw_rudder_nm") == 0)
    {
        params->yaw_rudder_nm = value;
    }
    else if (strcmp(key, "rate_damping_nms.x") == 0)
    {
        params->rate_damping_nms.x = value;
    }
    else if (strcmp(key, "rate_damping_nms.y") == 0)
    {
        params->rate_damping_nms.y = value;
    }
    else if (strcmp(key, "rate_damping_nms.z") == 0)
    {
        params->rate_damping_nms.z = value;
    }
    else
    {
        set_error(error, error_size, "unknown sim param", key);
        return 0;
    }
    return 1;
}

int altair_sim_params_load(const char *path,
                           sim_fixedwing_params_t *params,
                           const vehicle_params_t *vehicle,
                           char *error,
                           size_t error_size)
{
    FILE *file;
    char line[ALTAIR_SIM_PARAM_LINE_MAX];
    int in_section = 0;

    if (path == NULL || params == NULL)
    {
        set_error(error, error_size, "invalid sim param load arguments", NULL);
        return 0;
    }
    altair_default_fixedwing_sim_params(params);
    file = fopen(path, "r");
    if (file == NULL)
    {
        set_error(error, error_size, "failed to open sim param file", path);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *comment = strchr(line, '#');
        char *key;
        char *equals;
        if (comment != NULL)
        {
            *comment = '\0';
        }
        key = trim(line);
        if (*key == '\0')
        {
            continue;
        }
        if (*key == '[')
        {
            size_t len = strlen(key);
            if (len < 3U || key[len - 1U] != ']')
            {
                (void)fclose(file);
                set_error(error, error_size, "invalid sim param section", key);
                return 0;
            }
            key[len - 1U] = '\0';
            in_section = strcmp(key + 1U, "sim_params") == 0;
            continue;
        }
        if (!in_section)
        {
            continue;
        }
        equals = strchr(key, '=');
        if (equals == NULL)
        {
            (void)fclose(file);
            set_error(error, error_size, "expected sim param key = value", key);
            return 0;
        }
        *equals = '\0';
        if (!altair_sim_params_apply(params, trim(key), trim(equals + 1), error, error_size))
        {
            (void)fclose(file);
            return 0;
        }
    }
    if (ferror(file) || fclose(file) != 0)
    {
        set_error(error, error_size, "failed to read sim param file", path);
        return 0;
    }
    return altair_sim_params_validate(params, vehicle, error, error_size);
}

void altair_sim_param_store_init(altair_sim_param_store_t *store,
                                 const sim_fixedwing_params_t *initial)
{
    if (store == NULL)
    {
        return;
    }
    if (initial != NULL)
    {
        store->active = *initial;
    }
    else
    {
        altair_default_fixedwing_sim_params(&store->active);
    }
    store->staged = store->active;
    store->generation = 0U;
    store->staged_valid = 0U;
}

const sim_fixedwing_params_t *altair_sim_param_store_active(const altair_sim_param_store_t *store)
{
    return store != NULL ? &store->active : NULL;
}

uint32_t altair_sim_param_store_generation(const altair_sim_param_store_t *store)
{
    return store != NULL ? store->generation : 0U;
}

void altair_sim_param_store_begin(altair_sim_param_store_t *store)
{
    if (store != NULL)
    {
        store->staged = store->active;
        store->staged_valid = 1U;
    }
}

int altair_sim_param_store_stage(altair_sim_param_store_t *store,
                                 const char *key,
                                 const char *value_text,
                                 char *error,
                                 size_t error_size)
{
    if (store == NULL)
    {
        set_error(error, error_size, "sim param store is null", NULL);
        return 0;
    }
    if (!store->staged_valid)
    {
        altair_sim_param_store_begin(store);
    }
    return altair_sim_params_apply(&store->staged, key, value_text, error, error_size);
}

int altair_sim_param_store_commit(altair_sim_param_store_t *store,
                                  const vehicle_params_t *vehicle,
                                  char *error,
                                  size_t error_size)
{
    if (store == NULL || !store->staged_valid)
    {
        set_error(error, error_size, "no staged sim params to commit", NULL);
        return 0;
    }
    if (!altair_sim_params_validate(&store->staged, vehicle, error, error_size))
    {
        store->staged = store->active;
        store->staged_valid = 0U;
        return 0;
    }
    store->active = store->staged;
    ++store->generation;
    store->staged_valid = 0U;
    return 1;
}
