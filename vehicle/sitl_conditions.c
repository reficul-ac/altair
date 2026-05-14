#include "sitl_conditions.h"

#include "math_utils.h"
#include "sim6dof.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SITL_CONDITIONS_LINE_MAX 256

typedef struct
{
    const char *name;
    real_t *value;
} scalar_ref_t;

static int validate_assignment_target(const sitl_condition_assignment_t *assignment,
                                      char *error,
                                      size_t error_size);

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

static int parse_real_value(const char *text, real_t *value)
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

static int parse_uint32_from_real(real_t value, uint32_t *out)
{
    uint32_t parsed;
    if (value < 0.0f || value > 4294967295.0f)
    {
        return 0;
    }
    parsed = (uint32_t)value;
    if (value != (real_t)parsed)
    {
        return 0;
    }
    *out = parsed;
    return 1;
}

static int parse_uint8_from_real(real_t value, uint8_t *out)
{
    uint32_t parsed;
    if (!parse_uint32_from_real(value, &parsed) || parsed > 255U)
    {
        return 0;
    }
    *out = (uint8_t)parsed;
    return 1;
}

static int parse_bool_from_real(real_t value, uint8_t *out)
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

static int copy_target(char *dst, size_t dst_size, const char *src)
{
    size_t len = strlen(src);
    if (len == 0U || len >= dst_size)
    {
        return 0;
    }
    memcpy(dst, src, len + 1U);
    return 1;
}

void sitl_conditions_default(sitl_conditions_t *conditions)
{
    if (conditions != NULL)
    {
        memset(conditions, 0, sizeof(*conditions));
    }
}

static int parse_cmp(const char *text, sitl_condition_cmp_t *cmp)
{
    if (strcmp(text, ">") == 0)
    {
        *cmp = SITL_CONDITION_CMP_GT;
    }
    else if (strcmp(text, ">=") == 0)
    {
        *cmp = SITL_CONDITION_CMP_GE;
    }
    else if (strcmp(text, "<") == 0)
    {
        *cmp = SITL_CONDITION_CMP_LT;
    }
    else if (strcmp(text, "<=") == 0)
    {
        *cmp = SITL_CONDITION_CMP_LE;
    }
    else if (strcmp(text, "==") == 0)
    {
        *cmp = SITL_CONDITION_CMP_EQ;
    }
    else if (strcmp(text, "!=") == 0)
    {
        *cmp = SITL_CONDITION_CMP_NE;
    }
    else
    {
        return 0;
    }
    return 1;
}

static int parse_when(
    const char *text, sitl_condition_rule_t *rule, char *error, size_t error_size, int line_number)
{
    char lhs[16];
    char cmp_text[4];
    char rhs_text[64];
    char extra[2];
    int fields;

    fields = sscanf(text, "%15s %3s %63s %1s", lhs, cmp_text, rhs_text, extra);
    if (fields != 3)
    {
        set_error(error, error_size, "invalid when expression", text, line_number);
        return 0;
    }
    if (strcmp(lhs, "t_s") == 0)
    {
        rule->lhs = SITL_CONDITION_LHS_TIME_S;
    }
    else if (strcmp(lhs, "step") == 0)
    {
        rule->lhs = SITL_CONDITION_LHS_STEP;
    }
    else
    {
        set_error(error, error_size, "invalid when left operand", lhs, line_number);
        return 0;
    }
    if (!parse_cmp(cmp_text, &rule->cmp))
    {
        set_error(error, error_size, "invalid comparator", cmp_text, line_number);
        return 0;
    }
    if (!parse_double_value(rhs_text, &rule->rhs))
    {
        set_error(error, error_size, "invalid numeric value", rhs_text, line_number);
        return 0;
    }
    rule->has_when = 1U;
    return 1;
}

static int parse_section(const char *name,
                         sitl_conditions_t *conditions,
                         sitl_condition_rule_t **current_rule,
                         char *error,
                         size_t error_size,
                         int line_number)
{
    if (strncmp(name, "rule.", 5U) != 0 || name[5] == '\0')
    {
        set_error(error, error_size, "unknown section", name, line_number);
        return 0;
    }
    if (conditions->rule_count >= SITL_CONDITIONS_MAX_RULES)
    {
        set_error(error, error_size, "too many condition rules", name, line_number);
        return 0;
    }
    *current_rule = &conditions->rules[conditions->rule_count++];
    memset(*current_rule, 0, sizeof(**current_rule));
    return 1;
}

static int add_assignment(sitl_condition_rule_t *rule,
                          const char *target,
                          const char *value_text,
                          char *error,
                          size_t error_size,
                          int line_number)
{
    sitl_condition_assignment_t *assignment;
    if (rule == NULL)
    {
        set_error(error, error_size, "assignment outside a rule", target, line_number);
        return 0;
    }
    if (rule->assignment_count >= SITL_CONDITIONS_MAX_ASSIGNMENTS_PER_RULE)
    {
        set_error(error, error_size, "too many assignments in rule", target, line_number);
        return 0;
    }
    assignment = &rule->assignments[rule->assignment_count];
    if (!copy_target(assignment->target, sizeof(assignment->target), target))
    {
        set_error(error, error_size, "invalid assignment target", target, line_number);
        return 0;
    }
    if (!copy_target(assignment->value_text, sizeof(assignment->value_text), value_text))
    {
        set_error(error, error_size, "invalid assignment value", value_text, line_number);
        return 0;
    }
    if (strcmp(target, "trim.mode") == 0)
    {
        assignment->value = 0.0f;
    }
    else if (!parse_real_value(value_text, &assignment->value))
    {
        set_error(error, error_size, "invalid numeric value", value_text, line_number);
        return 0;
    }
    assignment->line_number = line_number;
    ++rule->assignment_count;
    return 1;
}

int sitl_conditions_load(const char *path,
                         sitl_conditions_t *conditions,
                         char *error,
                         size_t error_size)
{
    FILE *file;
    char line[SITL_CONDITIONS_LINE_MAX];
    int line_number = 0;
    sitl_condition_rule_t *current_rule = NULL;
    uint32_t i;

    if (path == NULL || conditions == NULL)
    {
        set_error(error, error_size, "invalid condition-file arguments", NULL, 0);
        return 0;
    }

    sitl_conditions_default(conditions);
    file = fopen(path, "r");
    if (file == NULL)
    {
        set_error(error, error_size, "failed to open condition file", path, 0);
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
            if (!parse_section(key + 1U, conditions, &current_rule, error, error_size, line_number))
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
        if (strcmp(key, "when") == 0)
        {
            if (current_rule == NULL)
            {
                set_error(error, error_size, "when outside a rule", key, line_number);
                (void)fclose(file);
                return 0;
            }
            if (!parse_when(value_text, current_rule, error, error_size, line_number))
            {
                (void)fclose(file);
                return 0;
            }
        }
        else if (!add_assignment(current_rule, key, value_text, error, error_size, line_number))
        {
            (void)fclose(file);
            return 0;
        }
    }

    if (ferror(file))
    {
        set_error(error, error_size, "failed to read condition file", path, 0);
        (void)fclose(file);
        return 0;
    }
    if (fclose(file) != 0)
    {
        set_error(error, error_size, "failed to close condition file", path, 0);
        return 0;
    }
    for (i = 0U; i < conditions->rule_count; ++i)
    {
        if (!conditions->rules[i].has_when)
        {
            set_error(error, error_size, "rule is missing when", NULL, 0);
            return 0;
        }
        if (conditions->rules[i].assignment_count == 0U)
        {
            set_error(error, error_size, "rule has no assignments", NULL, 0);
            return 0;
        }
        for (uint32_t j = 0U; j < conditions->rules[i].assignment_count; ++j)
        {
            if (!validate_assignment_target(
                    &conditions->rules[i].assignments[j], error, error_size))
            {
                return 0;
            }
        }
    }
    return 1;
}

static int condition_matches(const sitl_condition_rule_t *rule, const sitl_condition_context_t *ctx)
{
    double lhs = rule->lhs == SITL_CONDITION_LHS_TIME_S ? ctx->t_s : (double)ctx->step;
    switch (rule->cmp)
    {
    case SITL_CONDITION_CMP_GT:
        return lhs > rule->rhs;
    case SITL_CONDITION_CMP_GE:
        return lhs >= rule->rhs;
    case SITL_CONDITION_CMP_LT:
        return lhs < rule->rhs;
    case SITL_CONDITION_CMP_LE:
        return lhs <= rule->rhs;
    case SITL_CONDITION_CMP_EQ:
        return lhs == rule->rhs;
    case SITL_CONDITION_CMP_NE:
        return lhs != rule->rhs;
    default:
        return 0;
    }
}

static int set_real(real_t *target, real_t value)
{
    if (*target != value)
    {
        *target = value;
        return 1;
    }
    return 0;
}

static int set_uint8(uint8_t *target, real_t value, char *error, size_t error_size, int line_number)
{
    uint8_t parsed;
    if (!parse_uint8_from_real(value, &parsed))
    {
        set_error(error, error_size, "invalid integer value", NULL, line_number);
        return -1;
    }
    if (*target != parsed)
    {
        *target = parsed;
        return 1;
    }
    return 0;
}

static int
set_uint32(uint32_t *target, real_t value, char *error, size_t error_size, int line_number)
{
    uint32_t parsed;
    if (!parse_uint32_from_real(value, &parsed))
    {
        set_error(error, error_size, "invalid integer value", NULL, line_number);
        return -1;
    }
    if (*target != parsed)
    {
        *target = parsed;
        return 1;
    }
    return 0;
}

static int set_bool(uint8_t *target, real_t value, char *error, size_t error_size, int line_number)
{
    uint8_t parsed;
    if (!parse_bool_from_real(value, &parsed))
    {
        set_error(error, error_size, "invalid boolean value", NULL, line_number);
        return -1;
    }
    if (*target != parsed)
    {
        *target = parsed;
        return 1;
    }
    return 0;
}

static real_t *find_scalar(const scalar_ref_t *refs, size_t count, const char *name)
{
    size_t i;
    for (i = 0U; i < count; ++i)
    {
        if (strcmp(refs[i].name, name) == 0)
        {
            return refs[i].value;
        }
    }
    return NULL;
}

static int assign_rc(
    rc_input_t *rc, const char *key, real_t value, char *error, size_t error_size, int line_number)
{
    scalar_ref_t refs[] = {
        {"throttle", &rc->throttle}, {"roll", &rc->roll}, {"pitch", &rc->pitch}, {"yaw", &rc->yaw}};
    real_t *target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), key);
    if (target != NULL)
    {
        return set_real(target, value);
    }
    if (strcmp(key, "arm") == 0 || strcmp(key, "arm_switch") == 0)
    {
        return set_uint8(&rc->arm_switch, value, error, error_size, line_number);
    }
    if (strcmp(key, "mode") == 0 || strcmp(key, "mode_switch") == 0)
    {
        return set_uint8(&rc->mode_switch, value, error, error_size, line_number);
    }
    set_error(error, error_size, "unknown rc target", key, line_number);
    return -1;
}

static int assign_vehicle_params(vehicle_params_t *params,
                                 const char *key,
                                 real_t value,
                                 char *error,
                                 size_t error_size,
                                 int line_number)
{
    scalar_ref_t refs[] = {{"max_airspeed_mps", &params->max_airspeed_mps},
                           {"min_airspeed_mps", &params->min_airspeed_mps},
                           {"max_roll_rad", &params->max_roll_rad},
                           {"max_pitch_rad", &params->max_pitch_rad},
                           {"max_yaw_rate_rps", &params->max_yaw_rate_rps},
                           {"max_actuator", &params->max_actuator},
                           {"min_actuator", &params->min_actuator},
                           {"safe_motor", &params->safe_motor},
                           {"safe_surface", &params->safe_surface}};
    real_t *target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), key);
    if (target == NULL)
    {
        set_error(error, error_size, "unknown vehicle param target", key, line_number);
        return -1;
    }
    return set_real(target, value);
}

static int assign_sim_params(sim_fixedwing_params_t *params,
                             const char *key,
                             real_t value,
                             char *error,
                             size_t error_size,
                             int line_number)
{
    scalar_ref_t refs[] = {{"core.mass_kg", &params->core.mass_kg},
                           {"core.inertia_kgm2.x", &params->core.inertia_kgm2.x},
                           {"core.inertia_kgm2.y", &params->core.inertia_kgm2.y},
                           {"core.inertia_kgm2.z", &params->core.inertia_kgm2.z},
                           {"core.gravity_mps2", &params->core.gravity_mps2},
                           {"core.air_density_kgpm3", &params->core.air_density_kgpm3},
                           {"core.actuator_lag_hz", &params->core.actuator_lag_hz},
                           {"core.earth_radius_m", &params->core.earth_radius_m},
                           {"wing_area_m2", &params->wing_area_m2},
                           {"wing_span_m", &params->wing_span_m},
                           {"mean_chord_m", &params->mean_chord_m},
                           {"max_thrust_n", &params->max_thrust_n},
                           {"drag_cd0", &params->drag_cd0},
                           {"drag_cd_alpha", &params->drag_cd_alpha},
                           {"lift_cl0", &params->lift_cl0},
                           {"lift_cl_alpha", &params->lift_cl_alpha},
                           {"lift_cl_elevator", &params->lift_cl_elevator},
                           {"stall_alpha_rad", &params->stall_alpha_rad},
                           {"roll_aileron_nm", &params->roll_aileron_nm},
                           {"pitch_elevator_nm", &params->pitch_elevator_nm},
                           {"yaw_rudder_nm", &params->yaw_rudder_nm},
                           {"rate_damping_nms.x", &params->rate_damping_nms.x},
                           {"rate_damping_nms.y", &params->rate_damping_nms.y},
                           {"rate_damping_nms.z", &params->rate_damping_nms.z}};
    real_t *target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), key);
    uint32_t parsed;
    if (target != NULL)
    {
        return set_real(target, value);
    }
    if (strcmp(key, "core.frame_mode") == 0)
    {
        if (!parse_uint32_from_real(value, &parsed))
        {
            set_error(error, error_size, "invalid integer value", key, line_number);
            return -1;
        }
        if (params->core.frame_mode != (int)parsed)
        {
            params->core.frame_mode = (int)parsed;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "core.earth_model") == 0)
    {
        if (!parse_uint32_from_real(value, &parsed))
        {
            set_error(error, error_size, "invalid integer value", key, line_number);
            return -1;
        }
        if (params->core.earth_model != (int)parsed)
        {
            params->core.earth_model = (int)parsed;
            return 1;
        }
        return 0;
    }
    set_error(error, error_size, "unknown sim param target", key, line_number);
    return -1;
}

static int assign_input(fsw_input_t *input,
                        const char *key,
                        real_t value,
                        char *error,
                        size_t error_size,
                        int line_number)
{
    scalar_ref_t refs[] = {{"dt_s", &input->dt_s},
                           {"imu.accel_mps2.x", &input->imu.accel_mps2.x},
                           {"imu.accel_mps2.y", &input->imu.accel_mps2.y},
                           {"imu.accel_mps2.z", &input->imu.accel_mps2.z},
                           {"imu.gyro_rps.x", &input->imu.gyro_rps.x},
                           {"imu.gyro_rps.y", &input->imu.gyro_rps.y},
                           {"imu.gyro_rps.z", &input->imu.gyro_rps.z},
                           {"gps.lat_deg", &input->gps.lat_deg},
                           {"gps.lon_deg", &input->gps.lon_deg},
                           {"gps.alt_m", &input->gps.alt_m},
                           {"gps.vel_mps.x", &input->gps.vel_mps.x},
                           {"gps.vel_mps.y", &input->gps.vel_mps.y},
                           {"gps.vel_mps.z", &input->gps.vel_mps.z},
                           {"baro.pressure_pa", &input->baro.pressure_pa},
                           {"baro.altitude_m", &input->baro.altitude_m},
                           {"airspeed.true_airspeed_mps", &input->airspeed.true_airspeed_mps}};
    real_t *target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), key);
    if (target != NULL)
    {
        return set_real(target, value);
    }
    if (strncmp(key, "rc.", 3U) == 0)
    {
        return assign_rc(&input->rc, key + 3U, value, error, error_size, line_number);
    }
    if (strcmp(key, "imu.timestamp_us") == 0)
    {
        return set_uint32(&input->imu.timestamp_us, value, error, error_size, line_number);
    }
    if (strcmp(key, "gps.timestamp_us") == 0)
    {
        return set_uint32(&input->gps.timestamp_us, value, error, error_size, line_number);
    }
    if (strcmp(key, "gps.fix_valid") == 0)
    {
        return set_bool(&input->gps.fix_valid, value, error, error_size, line_number);
    }
    if (strcmp(key, "baro.timestamp_us") == 0)
    {
        return set_uint32(&input->baro.timestamp_us, value, error, error_size, line_number);
    }
    if (strcmp(key, "airspeed.timestamp_us") == 0)
    {
        return set_uint32(&input->airspeed.timestamp_us, value, error, error_size, line_number);
    }
    set_error(error, error_size, "unknown input target", key, line_number);
    return -1;
}

static int assign_plant(sim_fixedwing_state_t *plant,
                        const char *key,
                        real_t value,
                        uint8_t *ned_dirty,
                        uint8_t *ecef_dirty,
                        char *error,
                        size_t error_size,
                        int line_number)
{
    scalar_ref_t refs[] = {{"origin_lat_deg", &plant->body.origin_lat_deg},
                           {"origin_lon_deg", &plant->body.origin_lon_deg},
                           {"origin_altitude_m", &plant->body.origin_altitude_m},
                           {"position_ned_m.x", &plant->body.position_ned_m.x},
                           {"position_ned_m.y", &plant->body.position_ned_m.y},
                           {"position_ned_m.z", &plant->body.position_ned_m.z},
                           {"velocity_ned_mps.x", &plant->body.velocity_ned_mps.x},
                           {"velocity_ned_mps.y", &plant->body.velocity_ned_mps.y},
                           {"velocity_ned_mps.z", &plant->body.velocity_ned_mps.z},
                           {"attitude_body_to_ned.w", &plant->body.attitude_body_to_ned.w},
                           {"attitude_body_to_ned.x", &plant->body.attitude_body_to_ned.x},
                           {"attitude_body_to_ned.y", &plant->body.attitude_body_to_ned.y},
                           {"attitude_body_to_ned.z", &plant->body.attitude_body_to_ned.z},
                           {"position_ecef_m.x", &plant->body.position_ecef_m.x},
                           {"position_ecef_m.y", &plant->body.position_ecef_m.y},
                           {"position_ecef_m.z", &plant->body.position_ecef_m.z},
                           {"velocity_ecef_mps.x", &plant->body.velocity_ecef_mps.x},
                           {"velocity_ecef_mps.y", &plant->body.velocity_ecef_mps.y},
                           {"velocity_ecef_mps.z", &plant->body.velocity_ecef_mps.z},
                           {"attitude_body_to_ecef.w", &plant->body.attitude_body_to_ecef.w},
                           {"attitude_body_to_ecef.x", &plant->body.attitude_body_to_ecef.x},
                           {"attitude_body_to_ecef.y", &plant->body.attitude_body_to_ecef.y},
                           {"attitude_body_to_ecef.z", &plant->body.attitude_body_to_ecef.z},
                           {"omega_body_rps.x", &plant->body.omega_body_rps.x},
                           {"omega_body_rps.y", &plant->body.omega_body_rps.y},
                           {"omega_body_rps.z", &plant->body.omega_body_rps.z},
                           {"actuator_state.motor", &plant->body.actuator_state.motor},
                           {"actuator_state.aileron", &plant->body.actuator_state.aileron},
                           {"actuator_state.elevator", &plant->body.actuator_state.elevator},
                           {"actuator_state.rudder", &plant->body.actuator_state.rudder},
                           {"specific_force_body_mps2.x", &plant->body.specific_force_body_mps2.x},
                           {"specific_force_body_mps2.y", &plant->body.specific_force_body_mps2.y},
                           {"specific_force_body_mps2.z", &plant->body.specific_force_body_mps2.z},
                           {"time_s", &plant->body.time_s},
                           {"last_force_body_n.x", &plant->last_force_body_n.x},
                           {"last_force_body_n.y", &plant->last_force_body_n.y},
                           {"last_force_body_n.z", &plant->last_force_body_n.z},
                           {"last_moment_body_nm.x", &plant->last_moment_body_nm.x},
                           {"last_moment_body_nm.y", &plant->last_moment_body_nm.y},
                           {"last_moment_body_nm.z", &plant->last_moment_body_nm.z},
                           {"last_airspeed_mps", &plant->last_airspeed_mps}};
    real_t *target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), key);
    int changed;
    if (target == NULL)
    {
        set_error(error, error_size, "unknown plant target", key, line_number);
        return -1;
    }
    changed = set_real(target, value);
    if (changed && (strncmp(key, "origin_", 7U) == 0 || strncmp(key, "position_ned_m.", 15U) == 0 ||
                    strncmp(key, "velocity_ned_mps.", 17U) == 0 ||
                    strncmp(key, "attitude_body_to_ned.", 21U) == 0))
    {
        *ned_dirty = 1U;
    }
    if (changed && (strncmp(key, "position_ecef_m.", 16U) == 0 ||
                    strncmp(key, "velocity_ecef_mps.", 18U) == 0 ||
                    strncmp(key, "attitude_body_to_ecef.", 22U) == 0))
    {
        *ecef_dirty = 1U;
    }
    return changed;
}

static int assign_mission(bayek_mission_plan_t *mission,
                          uint8_t *mission_enabled,
                          const char *key,
                          real_t value,
                          char *error,
                          size_t error_size,
                          int line_number)
{
    if (strcmp(key, "enabled") == 0)
    {
        return set_bool(mission_enabled, value, error, error_size, line_number);
    }
    if (strcmp(key, "waypoint_count") == 0)
    {
        return set_uint32(&mission->waypoint_count, value, error, error_size, line_number);
    }
    if (strncmp(key, "waypoint.", 9U) == 0)
    {
        const char *tail = key + 9U;
        char *end = NULL;
        unsigned long index;
        const char *field;
        bayek_mission_waypoint_t *wp;
        scalar_ref_t refs[5];
        real_t *target;

        errno = 0;
        index = strtoul(tail, &end, 10);
        if (errno != 0 || end == tail || *end != '.' || index >= BAYEK_MISSION_MAX_WAYPOINTS)
        {
            set_error(error, error_size, "invalid mission waypoint target", key, line_number);
            return -1;
        }
        field = end + 1;
        wp = &mission->waypoints[index];
        refs[0] = (scalar_ref_t){"lat_deg", &wp->lat_deg};
        refs[1] = (scalar_ref_t){"lon_deg", &wp->lon_deg};
        refs[2] = (scalar_ref_t){"alt_m", &wp->alt_m};
        refs[3] = (scalar_ref_t){"throttle", &wp->throttle};
        refs[4] = (scalar_ref_t){"acceptance_radius_m", &wp->acceptance_radius_m};
        target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), field);
        if (target == NULL)
        {
            set_error(error, error_size, "unknown mission waypoint target", key, line_number);
            return -1;
        }
        return set_real(target, value);
    }
    set_error(error, error_size, "unknown mission target", key, line_number);
    return -1;
}

static int assign_trim(sitl_trim_config_t *trim,
                       const sitl_condition_assignment_t *assignment,
                       const char *key,
                       char *error,
                       size_t error_size)
{
    scalar_ref_t refs[] = {{"target_airspeed_mps", &trim->target_airspeed_mps},
                           {"tolerance", &trim->tolerance},
                           {"settle_time_s", &trim->settle_time_s},
                           {"eval_time_s", &trim->eval_time_s}};
    real_t *target = find_scalar(refs, sizeof(refs) / sizeof(refs[0]), key);
    if (target != NULL)
    {
        return set_real(target, assignment->value);
    }
    if (strcmp(key, "enabled") == 0)
    {
        return set_bool(
            &trim->enabled, assignment->value, error, error_size, assignment->line_number);
    }
    if (strcmp(key, "fail_on_error") == 0)
    {
        return set_bool(
            &trim->fail_on_error, assignment->value, error, error_size, assignment->line_number);
    }
    if (strcmp(key, "max_iterations") == 0)
    {
        return set_uint32(
            &trim->max_iterations, assignment->value, error, error_size, assignment->line_number);
    }
    if (strcmp(key, "mode") == 0)
    {
        sitl_trim_mode_t mode;
        if (!sitl_trim_parse_mode(assignment->value_text, &mode))
        {
            set_error(error,
                      error_size,
                      "unknown trim mode",
                      assignment->value_text,
                      assignment->line_number);
            return -1;
        }
        if (trim->mode != mode)
        {
            trim->mode = mode;
            return 1;
        }
        return 0;
    }
    set_error(error, error_size, "unknown trim target", key, assignment->line_number);
    return -1;
}

static int apply_assignment(const sitl_condition_assignment_t *assignment,
                            sitl_condition_context_t *ctx,
                            char *error,
                            size_t error_size)
{
    int changed;
    const char *target = assignment->target;
    if (strncmp(target, "rc.", 3U) == 0)
    {
        changed = assign_rc(
            ctx->rc, target + 3U, assignment->value, error, error_size, assignment->line_number);
        if (changed > 0 && ctx->input != NULL)
        {
            ctx->input->rc = *ctx->rc;
        }
        return changed;
    }
    if (strncmp(target, "input.", 6U) == 0)
    {
        return assign_input(
            ctx->input, target + 6U, assignment->value, error, error_size, assignment->line_number);
    }
    if (strncmp(target, "vehicle_params.", 15U) == 0)
    {
        changed = assign_vehicle_params(ctx->vehicle_params,
                                        target + 15U,
                                        assignment->value,
                                        error,
                                        error_size,
                                        assignment->line_number);
        if (changed > 0)
        {
            ctx->vehicle_params_dirty = 1U;
        }
        return changed;
    }
    if (strncmp(target, "sim_params.", 11U) == 0)
    {
        changed = assign_sim_params(ctx->sim_params,
                                    target + 11U,
                                    assignment->value,
                                    error,
                                    error_size,
                                    assignment->line_number);
        if (changed > 0)
        {
            ctx->sim_params_dirty = 1U;
        }
        return changed;
    }
    if (strncmp(target, "plant.", 6U) == 0)
    {
        return assign_plant(ctx->plant,
                            target + 6U,
                            assignment->value,
                            &ctx->plant_ned_dirty,
                            &ctx->plant_ecef_dirty,
                            error,
                            error_size,
                            assignment->line_number);
    }
    if (strncmp(target, "trim.", 5U) == 0)
    {
        return assign_trim(ctx->trim, assignment, target + 5U, error, error_size);
    }
    if (strncmp(target, "mission.", 8U) == 0)
    {
        changed = assign_mission(ctx->mission,
                                 ctx->mission_enabled,
                                 target + 8U,
                                 assignment->value,
                                 error,
                                 error_size,
                                 assignment->line_number);
        if (changed > 0)
        {
            ctx->mission_dirty = 1U;
        }
        return changed;
    }
    set_error(error, error_size, "unknown assignment target", target, assignment->line_number);
    return -1;
}

static int validate_assignment_target(const sitl_condition_assignment_t *assignment,
                                      char *error,
                                      size_t error_size)
{
    sitl_condition_context_t ctx;
    rc_input_t rc = {0};
    fsw_input_t input = {0};
    vehicle_params_t vehicle_params = {0};
    sim_fixedwing_params_t sim_params = {0};
    sim_fixedwing_state_t plant = {0};
    sitl_trim_config_t trim_config;
    uint8_t mission_enabled = 0U;
    bayek_mission_plan_t mission = {0};

    memset(&ctx, 0, sizeof(ctx));
    ctx.rc = &rc;
    ctx.input = &input;
    ctx.vehicle_params = &vehicle_params;
    ctx.sim_params = &sim_params;
    ctx.plant = &plant;
    sitl_trim_config_default(&trim_config);
    ctx.trim = &trim_config;
    ctx.mission_enabled = &mission_enabled;
    ctx.mission = &mission;
    return apply_assignment(assignment, &ctx, error, error_size) >= 0;
}

int sitl_conditions_eval(const sitl_conditions_t *conditions,
                         sitl_condition_context_t *ctx,
                         char *error,
                         size_t error_size)
{
    uint32_t i;
    if (conditions == NULL || ctx == NULL || ctx->rc == NULL || ctx->input == NULL ||
        ctx->vehicle_params == NULL || ctx->sim_params == NULL || ctx->plant == NULL ||
        ctx->trim == NULL || ctx->mission_enabled == NULL || ctx->mission == NULL)
    {
        set_error(error, error_size, "invalid condition evaluation arguments", NULL, 0);
        return 0;
    }
    ctx->vehicle_params_dirty = 0U;
    ctx->sim_params_dirty = 0U;
    ctx->plant_ned_dirty = 0U;
    ctx->plant_ecef_dirty = 0U;
    ctx->mission_dirty = 0U;
    for (i = 0U; i < conditions->rule_count; ++i)
    {
        uint32_t j;
        if (!condition_matches(&conditions->rules[i], ctx))
        {
            continue;
        }
        for (j = 0U; j < conditions->rules[i].assignment_count; ++j)
        {
            if (apply_assignment(&conditions->rules[i].assignments[j], ctx, error, error_size) < 0)
            {
                return 0;
            }
        }
    }
    return 1;
}
