#include "sitl_case.h"

#include "altair_params.h"
#include "altair_sim_params.h"
#include "math_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SITL_CASE_LINE_MAX 256

typedef enum
{
    CASE_SECTION_NONE = 0,
    CASE_SECTION_RUN,
    CASE_SECTION_INITIAL,
    CASE_SECTION_RC,
    CASE_SECTION_VEHICLE_PARAMS,
    CASE_SECTION_SIM_PARAMS,
    CASE_SECTION_MISSION,
    CASE_SECTION_WAYPOINT
} case_section_t;

typedef struct
{
    case_section_t section;
    uint32_t waypoint_index;
} case_section_ref_t;

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

static int parse_double_value(const char *text, double *value)
{
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0')
    {
        return 0;
    }
    *value = parsed;
    return parsed > -1.0e300 && parsed < 1.0e300;
}

static int parse_uint32_value(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > 0xffffffffUL)
    {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
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

static int parse_bool_value(real_t value, uint8_t *out)
{
    if (value == 0.0f)
    {
        *out = 0U;
        return 1;
    }
    if (value == 1.0f)
    {
        *out = 1U;
        return 1;
    }
    return 0;
}

static int parse_frame_mode_text(const char *text, int *frame_mode)
{
    if (strcmp(text, "ned") == 0)
    {
        *frame_mode = SIM6DOF_FRAME_NED;
        return 1;
    }
    if (strcmp(text, "ecef") == 0)
    {
        *frame_mode = SIM6DOF_FRAME_ECEF;
        return 1;
    }
    return 0;
}

static int copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len == 0U || len >= dst_size)
    {
        return 0;
    }
    memcpy(dst, src, len + 1U);
    return 1;
}

void sitl_case_default(sitl_case_t *case_file)
{
    if (case_file == NULL)
    {
        return;
    }
    memset(case_file, 0, sizeof(*case_file));
    sitl_initial_conditions_default(&case_file->initial);
    case_file->vehicle_params = *altair_default_params();
    altair_default_fixedwing_sim_params(&case_file->sim_params);
    case_file->mission_enabled = 1U;
}

static int parse_section(
    const char *name, case_section_ref_t *section, char *error, size_t error_size, int line_number)
{
    if (strcmp(name, "run") == 0)
    {
        section->section = CASE_SECTION_RUN;
        section->waypoint_index = 0U;
    }
    else if (strcmp(name, "initial") == 0)
    {
        section->section = CASE_SECTION_INITIAL;
        section->waypoint_index = 0U;
    }
    else if (strcmp(name, "rc") == 0)
    {
        section->section = CASE_SECTION_RC;
        section->waypoint_index = 0U;
    }
    else if (strcmp(name, "vehicle_params") == 0)
    {
        section->section = CASE_SECTION_VEHICLE_PARAMS;
        section->waypoint_index = 0U;
    }
    else if (strcmp(name, "sim_params") == 0)
    {
        section->section = CASE_SECTION_SIM_PARAMS;
        section->waypoint_index = 0U;
    }
    else if (strcmp(name, "mission") == 0)
    {
        section->section = CASE_SECTION_MISSION;
        section->waypoint_index = 0U;
    }
    else if (strncmp(name, "waypoint.", 9U) == 0)
    {
        uint32_t index;
        if (!parse_uint32_value(name + 9U, &index) || index >= BAYEK_MISSION_MAX_WAYPOINTS)
        {
            set_error(error, error_size, "invalid waypoint index", name, line_number);
            return 0;
        }
        section->section = CASE_SECTION_WAYPOINT;
        section->waypoint_index = index;
    }
    else
    {
        set_error(error, error_size, "unknown section", name, line_number);
        return 0;
    }
    return 1;
}

static int apply_initial_key(sitl_initial_conditions_t *initial,
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
    else
    {
        set_error(error, error_size, "unknown initial key", key, line_number);
        return 0;
    }
    return 1;
}

static int apply_rc_key(
    rc_input_t *rc, const char *key, real_t value, char *error, size_t error_size, int line_number)
{
    if (strcmp(key, "throttle") == 0 || strcmp(key, "rc_throttle") == 0)
    {
        rc->throttle = value;
    }
    else if (strcmp(key, "roll") == 0 || strcmp(key, "rc_roll") == 0)
    {
        rc->roll = value;
    }
    else if (strcmp(key, "pitch") == 0 || strcmp(key, "rc_pitch") == 0)
    {
        rc->pitch = value;
    }
    else if (strcmp(key, "yaw") == 0 || strcmp(key, "rc_yaw") == 0)
    {
        rc->yaw = value;
    }
    else if (strcmp(key, "arm") == 0 || strcmp(key, "rc_arm") == 0)
    {
        if (!parse_switch_value(value, &rc->arm_switch))
        {
            set_error(error, error_size, "invalid switch value", key, line_number);
            return 0;
        }
    }
    else if (strcmp(key, "mode") == 0 || strcmp(key, "rc_mode") == 0)
    {
        if (!parse_switch_value(value, &rc->mode_switch))
        {
            set_error(error, error_size, "invalid switch value", key, line_number);
            return 0;
        }
    }
    else
    {
        set_error(error, error_size, "unknown rc key", key, line_number);
        return 0;
    }
    return 1;
}

static int apply_vehicle_param_key(vehicle_params_t *params,
                                   const char *key,
                                   const char *value_text,
                                   char *error,
                                   size_t error_size,
                                   int line_number)
{
    if (!altair_vehicle_params_apply(params, key, value_text, error, error_size))
    {
        if (error != NULL && error_size > 0U)
        {
            char detail[96];
            (void)snprintf(detail, sizeof(detail), "%s", error);
            set_error(error, error_size, detail, NULL, line_number);
        }
        return 0;
    }
    return 1;
}

static int apply_sim_param_key(sim_fixedwing_params_t *params,
                               const char *key,
                               const char *value_text,
                               char *error,
                               size_t error_size,
                               int line_number)
{
    if (!altair_sim_params_apply(params, key, value_text, error, error_size))
    {
        if (error != NULL && error_size > 0U)
        {
            char detail[96];
            (void)snprintf(detail, sizeof(detail), "%s", error);
            set_error(error, error_size, detail, NULL, line_number);
        }
        return 0;
    }
    return 1;
}

static int apply_waypoint_key(bayek_mission_waypoint_t *waypoint,
                              const char *key,
                              real_t value,
                              char *error,
                              size_t error_size,
                              int line_number)
{
    if (strcmp(key, "lat_deg") == 0)
    {
        waypoint->lat_deg = value;
    }
    else if (strcmp(key, "lon_deg") == 0)
    {
        waypoint->lon_deg = value;
    }
    else if (strcmp(key, "alt_m") == 0 || strcmp(key, "altitude_m") == 0)
    {
        waypoint->alt_m = value;
    }
    else if (strcmp(key, "throttle") == 0)
    {
        waypoint->throttle = value;
    }
    else if (strcmp(key, "acceptance_radius_m") == 0)
    {
        waypoint->acceptance_radius_m = value;
    }
    else
    {
        set_error(error, error_size, "unknown waypoint key", key, line_number);
        return 0;
    }
    return 1;
}

static int apply_run_key(sitl_case_run_t *run,
                         const char *key,
                         const char *value_text,
                         char *error,
                         size_t error_size,
                         int line_number)
{
    if (strcmp(key, "scenario") == 0)
    {
        if (!copy_text(run->scenario, sizeof(run->scenario), value_text))
        {
            set_error(error, error_size, "invalid scenario value", value_text, line_number);
            return 0;
        }
        run->has_scenario = 1U;
    }
    else if (strcmp(key, "profile") == 0)
    {
        if (!copy_text(run->profile, sizeof(run->profile), value_text))
        {
            set_error(error, error_size, "invalid profile value", value_text, line_number);
            return 0;
        }
        run->has_profile = 1U;
    }
    else if (strcmp(key, "duration_s") == 0)
    {
        if (!parse_double_value(value_text, &run->duration_s))
        {
            set_error(error, error_size, "invalid numeric value", value_text, line_number);
            return 0;
        }
        run->has_duration_s = 1U;
    }
    else if (strcmp(key, "dt_s") == 0)
    {
        if (!parse_double_value(value_text, &run->dt_s))
        {
            set_error(error, error_size, "invalid numeric value", value_text, line_number);
            return 0;
        }
        run->has_dt_s = 1U;
    }
    else if (strcmp(key, "seed") == 0)
    {
        if (!parse_uint32_value(value_text, &run->seed))
        {
            set_error(error, error_size, "invalid seed value", value_text, line_number);
            return 0;
        }
        run->has_seed = 1U;
    }
    else if (strcmp(key, "frame_mode") == 0)
    {
        if (!parse_frame_mode_text(value_text, &run->frame_mode))
        {
            set_error(error, error_size, "invalid frame mode", value_text, line_number);
            return 0;
        }
        run->has_frame_mode = 1U;
    }
    else if (strcmp(key, "condition_file") == 0)
    {
        if (!copy_text(run->condition_file, sizeof(run->condition_file), value_text))
        {
            set_error(error, error_size, "invalid condition file value", value_text, line_number);
            return 0;
        }
        run->has_condition_file = 1U;
    }
    else if (strcmp(key, "vehicle_param_file") == 0 || strcmp(key, "flight_param_file") == 0)
    {
        if (!copy_text(run->vehicle_param_file, sizeof(run->vehicle_param_file), value_text))
        {
            set_error(
                error, error_size, "invalid vehicle param file value", value_text, line_number);
            return 0;
        }
        run->has_vehicle_param_file = 1U;
    }
    else if (strcmp(key, "sim_param_file") == 0)
    {
        if (!copy_text(run->sim_param_file, sizeof(run->sim_param_file), value_text))
        {
            set_error(error, error_size, "invalid sim param file value", value_text, line_number);
            return 0;
        }
        run->has_sim_param_file = 1U;
    }
    else
    {
        set_error(error, error_size, "unknown run key", key, line_number);
        return 0;
    }
    return 1;
}

static int apply_key_value(sitl_case_t *case_file,
                           case_section_ref_t section,
                           const char *key,
                           const char *value_text,
                           uint8_t *waypoint_present,
                           char *error,
                           size_t error_size,
                           int line_number)
{
    real_t value;
    switch (section.section)
    {
    case CASE_SECTION_RUN:
        return apply_run_key(&case_file->run, key, value_text, error, error_size, line_number);
    case CASE_SECTION_INITIAL:
        if (!parse_scalar(value_text, &value))
        {
            set_error(error, error_size, "invalid numeric value", value_text, line_number);
            return 0;
        }
        case_file->has_initial = 1U;
        return apply_initial_key(&case_file->initial, key, value, error, error_size, line_number);
    case CASE_SECTION_RC:
        if (!parse_scalar(value_text, &value))
        {
            set_error(error, error_size, "invalid numeric value", value_text, line_number);
            return 0;
        }
        case_file->has_initial = 1U;
        return apply_rc_key(&case_file->initial.rc, key, value, error, error_size, line_number);
    case CASE_SECTION_VEHICLE_PARAMS:
        case_file->has_vehicle_params = 1U;
        return apply_vehicle_param_key(
            &case_file->vehicle_params, key, value_text, error, error_size, line_number);
    case CASE_SECTION_SIM_PARAMS:
        case_file->has_sim_params = 1U;
        return apply_sim_param_key(
            &case_file->sim_params, key, value_text, error, error_size, line_number);
    case CASE_SECTION_MISSION:
        if (strcmp(key, "enabled") != 0)
        {
            set_error(error, error_size, "unknown mission key", key, line_number);
            return 0;
        }
        if (!parse_scalar(value_text, &value) ||
            !parse_bool_value(value, &case_file->mission_enabled))
        {
            set_error(error, error_size, "invalid boolean value", value_text, line_number);
            return 0;
        }
        case_file->has_mission = 1U;
        return 1;
    case CASE_SECTION_WAYPOINT:
        if (!parse_scalar(value_text, &value))
        {
            set_error(error, error_size, "invalid numeric value", value_text, line_number);
            return 0;
        }
        case_file->has_mission = 1U;
        case_file->mission_enabled = 1U;
        waypoint_present[section.waypoint_index] = 1U;
        return apply_waypoint_key(&case_file->mission.waypoints[section.waypoint_index],
                                  key,
                                  value,
                                  error,
                                  error_size,
                                  line_number);
    case CASE_SECTION_NONE:
    default:
        set_error(error, error_size, "key outside a section", key, line_number);
        return 0;
    }
}

static int finalize_waypoints(sitl_case_t *case_file,
                              const uint8_t *waypoint_present,
                              char *error,
                              size_t error_size)
{
    uint32_t i;
    uint32_t count = 0U;

    for (i = 0U; i < BAYEK_MISSION_MAX_WAYPOINTS; ++i)
    {
        if (waypoint_present[i])
        {
            if (i != count)
            {
                set_error(error, error_size, "waypoint indices must be contiguous", NULL, 0);
                return 0;
            }
            ++count;
        }
    }
    case_file->mission.waypoint_count = count;
    return 1;
}

int sitl_case_load(const char *path, sitl_case_t *case_file, char *error, size_t error_size)
{
    FILE *file;
    char line[SITL_CASE_LINE_MAX];
    int line_number = 0;
    case_section_ref_t section = {CASE_SECTION_NONE, 0U};
    uint8_t waypoint_present[BAYEK_MISSION_MAX_WAYPOINTS] = {0};

    if (path == NULL || case_file == NULL)
    {
        set_error(error, error_size, "invalid case-file arguments", NULL, 0);
        return 0;
    }

    sitl_case_default(case_file);
    file = fopen(path, "r");
    if (file == NULL)
    {
        set_error(error, error_size, "failed to open case file", path, 0);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *comment;
        char *equals;
        char *key;
        char *value_text;

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
        if (*key == '[')
        {
            size_t len = strlen(key);
            if (len < 3U || key[len - 1U] != ']')
            {
                set_error(error, error_size, "invalid section header", key, line_number);
                (void)fclose(file);
                return 0;
            }
            key[len - 1U] = '\0';
            if (!parse_section(key + 1U, &section, error, error_size, line_number))
            {
                (void)fclose(file);
                return 0;
            }
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
        if (!apply_key_value(case_file,
                             section,
                             key,
                             value_text,
                             waypoint_present,
                             error,
                             error_size,
                             line_number))
        {
            (void)fclose(file);
            return 0;
        }
    }

    if (ferror(file))
    {
        set_error(error, error_size, "failed to read case file", path, 0);
        (void)fclose(file);
        return 0;
    }
    if (fclose(file) != 0)
    {
        set_error(error, error_size, "failed to close case file", path, 0);
        return 0;
    }
    if (case_file->run.has_vehicle_param_file)
    {
        if (!altair_vehicle_params_load(
                case_file->run.vehicle_param_file, &case_file->vehicle_params, error, error_size))
        {
            return 0;
        }
        case_file->has_vehicle_params = 1U;
    }
    if (case_file->run.has_sim_param_file)
    {
        if (!altair_sim_params_load(case_file->run.sim_param_file,
                                    &case_file->sim_params,
                                    &case_file->vehicle_params,
                                    error,
                                    error_size))
        {
            return 0;
        }
        case_file->has_sim_params = 1U;
    }
    if (case_file->has_vehicle_params &&
        !altair_vehicle_params_validate(&case_file->vehicle_params, error, error_size))
    {
        return 0;
    }
    if (case_file->has_sim_params &&
        !altair_sim_params_validate(
            &case_file->sim_params, &case_file->vehicle_params, error, error_size))
    {
        return 0;
    }
    return finalize_waypoints(case_file, waypoint_present, error, error_size);
}
