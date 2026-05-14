#include "sitl_initial_conditions.h"

#include "math_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SITL_INITIAL_LINE_MAX 256

static void
set_error(char *error, size_t error_size, const char *message, const char *detail, int line_number)
{
    if (error == NULL || error_size == 0U)
    {
        return;
    }
    if (line_number > 0)
    {
        (void)snprintf(error,
                       error_size,
                       "line %d: %s%s%s",
                       line_number,
                       message,
                       detail ? ": " : "",
                       detail ? detail : "");
    }
    else
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

static int parse_scalar(const char *text, real_t *value)
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

static int parse_switch_value(real_t value, uint8_t *out)
{
    int rounded = (int)value;
    if (value != (real_t)rounded || rounded < 0 || rounded > 255)
    {
        return 0;
    }
    *out = (uint8_t)rounded;
    return 1;
}

void sitl_initial_conditions_default(sitl_initial_conditions_t *initial)
{
    if (initial == NULL)
    {
        return;
    }
    initial->lat_deg = 0.0f;
    initial->lon_deg = 0.0f;
    initial->altitude_m = 120.0f;
    initial->roll_rad = 0.0f;
    initial->pitch_rad = 0.0f;
    initial->yaw_rad = 0.0f;
    initial->vel_n_mps = 0.0f;
    initial->vel_e_mps = 0.0f;
    initial->vel_d_mps = 0.0f;
    initial->has_velocity_ned = 0U;
    initial->p_rps = 0.0f;
    initial->q_rps = 0.0f;
    initial->r_rps = 0.0f;
    initial->airspeed_mps = 18.0f;
    initial->rc.throttle = 0.58f;
    initial->rc.roll = 0.0f;
    initial->rc.pitch = 0.02f;
    initial->rc.yaw = 0.0f;
    initial->rc.arm_switch = 1U;
    initial->rc.mode_switch = 1U;
}

static int apply_key_value(sitl_initial_conditions_t *initial,
                           const char *key,
                           real_t value,
                           char *error,
                           size_t error_size,
                           int line_number)
{
    if (strcmp(key, "lat_deg") == 0)
    {
        initial->lat_deg = value;
    }
    else if (strcmp(key, "lon_deg") == 0)
    {
        initial->lon_deg = value;
    }
    else if (strcmp(key, "altitude_m") == 0)
    {
        initial->altitude_m = value;
    }
    else if (strcmp(key, "roll_rad") == 0)
    {
        initial->roll_rad = value;
    }
    else if (strcmp(key, "pitch_rad") == 0)
    {
        initial->pitch_rad = value;
    }
    else if (strcmp(key, "yaw_rad") == 0)
    {
        initial->yaw_rad = value;
    }
    else if (strcmp(key, "vel_n_mps") == 0)
    {
        initial->vel_n_mps = value;
        initial->has_velocity_ned = 1U;
    }
    else if (strcmp(key, "vel_e_mps") == 0)
    {
        initial->vel_e_mps = value;
        initial->has_velocity_ned = 1U;
    }
    else if (strcmp(key, "vel_d_mps") == 0)
    {
        initial->vel_d_mps = value;
        initial->has_velocity_ned = 1U;
    }
    else if (strcmp(key, "p_rps") == 0)
    {
        initial->p_rps = value;
    }
    else if (strcmp(key, "q_rps") == 0)
    {
        initial->q_rps = value;
    }
    else if (strcmp(key, "r_rps") == 0)
    {
        initial->r_rps = value;
    }
    else if (strcmp(key, "airspeed_mps") == 0)
    {
        initial->airspeed_mps = value;
    }
    else if (strcmp(key, "rc_throttle") == 0)
    {
        initial->rc.throttle = value;
    }
    else if (strcmp(key, "rc_roll") == 0)
    {
        initial->rc.roll = value;
    }
    else if (strcmp(key, "rc_pitch") == 0)
    {
        initial->rc.pitch = value;
    }
    else if (strcmp(key, "rc_yaw") == 0)
    {
        initial->rc.yaw = value;
    }
    else if (strcmp(key, "rc_arm") == 0)
    {
        if (!parse_switch_value(value, &initial->rc.arm_switch))
        {
            set_error(error, error_size, "invalid switch value", key, line_number);
            return 0;
        }
    }
    else if (strcmp(key, "rc_mode") == 0)
    {
        if (!parse_switch_value(value, &initial->rc.mode_switch))
        {
            set_error(error, error_size, "invalid switch value", key, line_number);
            return 0;
        }
    }
    else
    {
        set_error(error, error_size, "unknown key", key, line_number);
        return 0;
    }
    return 1;
}

int sitl_initial_conditions_load(const char *path,
                                 sitl_initial_conditions_t *initial,
                                 char *error,
                                 size_t error_size)
{
    FILE *file;
    char line[SITL_INITIAL_LINE_MAX];
    int line_number = 0;

    if (path == NULL || initial == NULL)
    {
        set_error(error, error_size, "invalid initial-condition arguments", NULL, 0);
        return 0;
    }

    sitl_initial_conditions_default(initial);
    file = fopen(path, "r");
    if (file == NULL)
    {
        set_error(error, error_size, "failed to open initial-condition file", path, 0);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *comment;
        char *equals;
        char *key;
        char *value_text;
        real_t value;

        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file))
        {
            set_error(error, error_size, "line is too long", NULL, line_number);
            (void)fclose(file);
            return 0;
        }

        comment = strchr(line, '#');
        if (comment != NULL)
        {
            *comment = '\0';
        }
        key = trim(line);
        if (*key == '\0')
        {
            continue;
        }
        equals = strchr(key, '=');
        if (equals == NULL)
        {
            set_error(error, error_size, "expected key = value", NULL, line_number);
            (void)fclose(file);
            return 0;
        }
        *equals = '\0';
        value_text = trim(equals + 1);
        key = trim(key);
        if (*key == '\0' || *value_text == '\0')
        {
            set_error(error, error_size, "expected key = value", NULL, line_number);
            (void)fclose(file);
            return 0;
        }
        if (!parse_scalar(value_text, &value))
        {
            set_error(error, error_size, "invalid numeric value", value_text, line_number);
            (void)fclose(file);
            return 0;
        }
        if (!apply_key_value(initial, key, value, error, error_size, line_number))
        {
            (void)fclose(file);
            return 0;
        }
    }

    if (ferror(file))
    {
        set_error(error, error_size, "failed to read initial-condition file", path, 0);
        (void)fclose(file);
        return 0;
    }
    if (fclose(file) != 0)
    {
        set_error(error, error_size, "failed to close initial-condition file", path, 0);
        return 0;
    }
    return 1;
}
