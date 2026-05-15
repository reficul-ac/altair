#include "altair_params.h"

#include "altair_limits.h"
#include "math_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALTAIR_PARAM_LINE_MAX 256

static const vehicle_params_t k_altair_params = {.max_airspeed_mps = 35.0f,
                                                 .min_airspeed_mps = 8.0f,
                                                 .max_roll_rad = 0.7853982f,
                                                 .max_pitch_rad = 0.3490658f,
                                                 .max_yaw_rate_rps = 1.5707963f,
                                                 .max_actuator = ALTAIR_SURFACE_MAX,
                                                 .min_actuator = ALTAIR_SURFACE_MIN,
                                                 .safe_motor = 0.0f,
                                                 .safe_surface = 0.0f};

const vehicle_params_t *altair_default_params(void)
{
    return &k_altair_params;
}

static void set_error(char *error, size_t error_size, const char *message, const char *detail)
{
    if (error == NULL || error_size == 0U)
    {
        return;
    }
    (void)snprintf(
        error, error_size, "%s%s%s", message, detail != NULL ? ": " : "", detail ? detail : "");
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

void altair_vehicle_params_default(vehicle_params_t *out)
{
    if (out != NULL)
    {
        *out = k_altair_params;
    }
}

int altair_vehicle_params_validate(const vehicle_params_t *params, char *error, size_t error_size)
{
    if (params == NULL)
    {
        set_error(error, error_size, "vehicle params are null", NULL);
        return 0;
    }
    if (!real_is_finite(params->min_airspeed_mps) || !real_is_finite(params->max_airspeed_mps) ||
        params->min_airspeed_mps <= 0.0f || params->max_airspeed_mps <= params->min_airspeed_mps ||
        params->max_airspeed_mps > 120.0f)
    {
        set_error(error, error_size, "vehicle airspeed limits are inconsistent", NULL);
        return 0;
    }
    if (!real_is_finite(params->max_roll_rad) || params->max_roll_rad <= 0.0f ||
        params->max_roll_rad > BAYEK_PI)
    {
        set_error(error, error_size, "vehicle roll limit is invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->max_pitch_rad) || params->max_pitch_rad <= 0.0f ||
        params->max_pitch_rad > (BAYEK_PI * 0.5f))
    {
        set_error(error, error_size, "vehicle pitch limit is invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->max_yaw_rate_rps) || params->max_yaw_rate_rps <= 0.0f)
    {
        set_error(error, error_size, "vehicle yaw-rate limit is invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->min_actuator) || !real_is_finite(params->max_actuator) ||
        params->min_actuator < -1.0f || params->max_actuator > 1.0f ||
        params->min_actuator >= params->max_actuator)
    {
        set_error(error, error_size, "vehicle actuator range is invalid", NULL);
        return 0;
    }
    if (!real_is_finite(params->safe_motor) || params->safe_motor < 0.0f ||
        params->safe_motor > 1.0f || !real_is_finite(params->safe_surface) ||
        params->safe_surface < params->min_actuator || params->safe_surface > params->max_actuator)
    {
        set_error(error, error_size, "vehicle safe outputs are invalid", NULL);
        return 0;
    }
    if (error != NULL && error_size > 0U)
    {
        error[0] = '\0';
    }
    return 1;
}

int altair_vehicle_params_apply(vehicle_params_t *params,
                                const char *key,
                                const char *value_text,
                                char *error,
                                size_t error_size)
{
    real_t value;
    if (params == NULL || key == NULL || value_text == NULL)
    {
        set_error(error, error_size, "invalid vehicle param apply arguments", NULL);
        return 0;
    }
    if (!parse_real(value_text, &value))
    {
        set_error(error, error_size, "invalid vehicle param value", value_text);
        return 0;
    }
    if (strcmp(key, "max_airspeed_mps") == 0)
    {
        params->max_airspeed_mps = value;
    }
    else if (strcmp(key, "min_airspeed_mps") == 0)
    {
        params->min_airspeed_mps = value;
    }
    else if (strcmp(key, "max_roll_rad") == 0)
    {
        params->max_roll_rad = value;
    }
    else if (strcmp(key, "max_pitch_rad") == 0)
    {
        params->max_pitch_rad = value;
    }
    else if (strcmp(key, "max_yaw_rate_rps") == 0)
    {
        params->max_yaw_rate_rps = value;
    }
    else if (strcmp(key, "max_actuator") == 0)
    {
        params->max_actuator = value;
    }
    else if (strcmp(key, "min_actuator") == 0)
    {
        params->min_actuator = value;
    }
    else if (strcmp(key, "safe_motor") == 0)
    {
        params->safe_motor = value;
    }
    else if (strcmp(key, "safe_surface") == 0)
    {
        params->safe_surface = value;
    }
    else
    {
        set_error(error, error_size, "unknown vehicle param", key);
        return 0;
    }
    return 1;
}

int altair_vehicle_params_load(const char *path,
                               vehicle_params_t *params,
                               char *error,
                               size_t error_size)
{
#ifdef ARDUINO
    (void)path;
    (void)params;
    set_error(
        error, error_size, "vehicle param file loading is not available on embedded builds", NULL);
    return 0;
#else
    FILE *file;
    char line[ALTAIR_PARAM_LINE_MAX];
    int in_section = 0;

    if (path == NULL || params == NULL)
    {
        set_error(error, error_size, "invalid vehicle param load arguments", NULL);
        return 0;
    }
    altair_vehicle_params_default(params);
    file = fopen(path, "r");
    if (file == NULL)
    {
        set_error(error, error_size, "failed to open vehicle param file", path);
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
                set_error(error, error_size, "invalid vehicle param section", key);
                return 0;
            }
            key[len - 1U] = '\0';
            in_section = strcmp(key + 1U, "vehicle_params") == 0;
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
            set_error(error, error_size, "expected vehicle param key = value", key);
            return 0;
        }
        *equals = '\0';
        if (!altair_vehicle_params_apply(params, trim(key), trim(equals + 1), error, error_size))
        {
            (void)fclose(file);
            return 0;
        }
    }
    if (ferror(file) || fclose(file) != 0)
    {
        set_error(error, error_size, "failed to read vehicle param file", path);
        return 0;
    }
    return altair_vehicle_params_validate(params, error, error_size);
#endif
}

void altair_vehicle_param_store_init(altair_vehicle_param_store_t *store,
                                     const vehicle_params_t *initial)
{
    if (store == NULL)
    {
        return;
    }
    store->active = initial != NULL ? *initial : k_altair_params;
    store->staged = store->active;
    store->generation = 0U;
    store->staged_valid = 0U;
}

const vehicle_params_t *altair_vehicle_param_store_active(const altair_vehicle_param_store_t *store)
{
    return store != NULL ? &store->active : NULL;
}

uint32_t altair_vehicle_param_store_generation(const altair_vehicle_param_store_t *store)
{
    return store != NULL ? store->generation : 0U;
}

void altair_vehicle_param_store_begin(altair_vehicle_param_store_t *store)
{
    if (store != NULL)
    {
        store->staged = store->active;
        store->staged_valid = 1U;
    }
}

int altair_vehicle_param_store_stage(altair_vehicle_param_store_t *store,
                                     const char *key,
                                     const char *value_text,
                                     char *error,
                                     size_t error_size)
{
    if (store == NULL)
    {
        set_error(error, error_size, "vehicle param store is null", NULL);
        return 0;
    }
    if (!store->staged_valid)
    {
        altair_vehicle_param_store_begin(store);
    }
    return altair_vehicle_params_apply(&store->staged, key, value_text, error, error_size);
}

int altair_vehicle_param_store_commit(altair_vehicle_param_store_t *store,
                                      char *error,
                                      size_t error_size)
{
    if (store == NULL || !store->staged_valid)
    {
        set_error(error, error_size, "no staged vehicle params to commit", NULL);
        return 0;
    }
    if (!altair_vehicle_params_validate(&store->staged, error, error_size))
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
