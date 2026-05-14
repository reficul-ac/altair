#include "sitl_trim.h"

#include "math_utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    sim_fixedwing_state_t base;
    sim_fixedwing_state_t candidate;
    const sim_fixedwing_params_t *params;
    real_t target_airspeed_mps;
    real_t dt_s;
    uint32_t settle_steps;
    uint32_t eval_steps;
} fixedwing_trim_context_t;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0U)
    {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static real_t config_positive_or(real_t value, real_t fallback)
{
    return real_is_finite(value) && value > 0.0f ? value : fallback;
}

void sitl_trim_config_default(sitl_trim_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->mode = SITL_TRIM_MODE_FIXEDWING_LEVEL;
    config->fail_on_error = 1U;
    config->target_airspeed_mps = 0.0f;
    config->tolerance = 2.0e-3f;
    config->max_iterations = 20U;
    config->settle_time_s = 0.35f;
    config->eval_time_s = 0.15f;
}

void sitl_trim_status_default(sitl_trim_status_t *status)
{
    if (status == NULL)
    {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->solver_status = BAYEK_TRIM_STATUS_INVALID_PROBLEM;
}

int sitl_trim_parse_mode(const char *text, sitl_trim_mode_t *mode)
{
    if (text == NULL || mode == NULL)
    {
        return 0;
    }
    if (strcmp(text, "fixedwing_level") == 0 || strcmp(text, "1") == 0)
    {
        *mode = SITL_TRIM_MODE_FIXEDWING_LEVEL;
        return 1;
    }
    if (strcmp(text, "none") == 0 || strcmp(text, "0") == 0)
    {
        *mode = SITL_TRIM_MODE_NONE;
        return 1;
    }
    return 0;
}

static void set_pitch_and_forward_speed(sim_fixedwing_state_t *state,
                                        real_t pitch_rad,
                                        real_t airspeed_mps,
                                        real_t earth_radius_m)
{
    euler_t euler = euler_from_quat(state->body.attitude_body_to_ned);
    vec3_t forward_body;
    euler.roll = 0.0f;
    euler.pitch = pitch_rad;
    state->body.attitude_body_to_ned = quat_from_euler(euler);
    forward_body.x = airspeed_mps;
    forward_body.y = 0.0f;
    forward_body.z = 0.0f;
    state->body.velocity_ned_mps = quat_rotate_vec3(state->body.attitude_body_to_ned, forward_body);
    state->body.omega_body_rps.x = 0.0f;
    state->body.omega_body_rps.y = 0.0f;
    state->body.omega_body_rps.z = 0.0f;
    sim6dof_sync_ecef_from_ned(&state->body, earth_radius_m);
}

static actuator_cmd_t variables_to_actuators(const bayek_trim_variable_t *variables)
{
    actuator_cmd_t cmd;
    cmd.motor = variables[0].value;
    cmd.elevator = variables[1].value;
    cmd.aileron = variables[2].value;
    cmd.rudder = variables[3].value;
    return cmd;
}

static int fixedwing_apply(const bayek_trim_problem_t *problem,
                           const bayek_trim_variable_t *variables,
                           void *user)
{
    fixedwing_trim_context_t *ctx = (fixedwing_trim_context_t *)user;
    actuator_cmd_t cmd;
    uint32_t i;
    (void)problem;
    if (ctx == NULL || variables == NULL)
    {
        return 0;
    }
    cmd = variables_to_actuators(variables);
    ctx->candidate = ctx->base;
    ctx->candidate.body.actuator_state = cmd;
    set_pitch_and_forward_speed(&ctx->candidate,
                                variables[4].value,
                                ctx->target_airspeed_mps,
                                ctx->params->core.earth_radius_m);
    for (i = 0U; i < ctx->settle_steps; ++i)
    {
        if (!sim_fixedwing_step(&ctx->candidate, ctx->params, &cmd, ctx->dt_s))
        {
            return 0;
        }
    }
    return 1;
}

static int fixedwing_residual(const bayek_trim_problem_t *problem, real_t *residuals, void *user)
{
    fixedwing_trim_context_t *ctx = (fixedwing_trim_context_t *)user;
    sim_fixedwing_state_t before;
    sim_fixedwing_state_t after;
    actuator_cmd_t cmd;
    real_t dt_total;
    uint32_t i;
    (void)problem;
    if (ctx == NULL || residuals == NULL || ctx->eval_steps == 0U)
    {
        return 0;
    }
    before = ctx->candidate;
    after = before;
    cmd = after.body.actuator_state;
    for (i = 0U; i < ctx->eval_steps; ++i)
    {
        if (!sim_fixedwing_step(&after, ctx->params, &cmd, ctx->dt_s))
        {
            return 0;
        }
    }
    dt_total = ctx->dt_s * (real_t)ctx->eval_steps;
    residuals[0] = ((after.last_airspeed_mps - before.last_airspeed_mps) / dt_total) / 5.0f;
    residuals[1] = ((after.body.velocity_ned_mps.z - before.body.velocity_ned_mps.z) / dt_total) / 5.0f;
    residuals[2] = after.body.omega_body_rps.x;
    residuals[3] = after.body.omega_body_rps.y;
    residuals[4] = after.body.omega_body_rps.z;
    residuals[5] = after.body.velocity_ned_mps.y / ctx->target_airspeed_mps;
    ctx->candidate = after;
    return 1;
}

static void set_variable(bayek_trim_variable_t *variable,
                         const char *name,
                         real_t value,
                         real_t min_value,
                         real_t max_value,
                         real_t scale,
                         real_t perturbation)
{
    variable->name = name;
    variable->value = value;
    variable->min_value = min_value;
    variable->max_value = max_value;
    variable->scale = scale;
    variable->perturbation = perturbation;
}

int sitl_trim_fixedwing_level(sim_fixedwing_state_t *plant,
                              const sim_fixedwing_params_t *params,
                              const vehicle_params_t *vehicle_params,
                              const sitl_trim_config_t *config,
                              sitl_trim_status_t *status,
                              char *error,
                              size_t error_size)
{
    fixedwing_trim_context_t ctx;
    bayek_trim_problem_t problem;
    bayek_trim_result_t result;
    real_t target_airspeed_mps;
    real_t dt_s = 0.01f;
    euler_t euler;
    actuator_cmd_t solved;

    if (plant == NULL || params == NULL || vehicle_params == NULL || config == NULL ||
        status == NULL)
    {
        set_error(error, error_size, "invalid trim arguments");
        return 0;
    }
    sitl_trim_status_default(status);
    status->active = 1U;
    if (!config->enabled || config->mode == SITL_TRIM_MODE_NONE)
    {
        status->active = 0U;
        return 1;
    }
    if (config->mode != SITL_TRIM_MODE_FIXEDWING_LEVEL)
    {
        status->failed = 1U;
        set_error(error, error_size, "unsupported trim mode");
        return 0;
    }
    target_airspeed_mps = config->target_airspeed_mps > 0.0f ? config->target_airspeed_mps
                                                             : plant->last_airspeed_mps;
    if (!real_is_finite(target_airspeed_mps) ||
        target_airspeed_mps < vehicle_params->min_airspeed_mps ||
        target_airspeed_mps > vehicle_params->max_airspeed_mps)
    {
        status->failed = 1U;
        set_error(error, error_size, "trim target airspeed is outside vehicle limits");
        return 0;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.base = *plant;
    ctx.params = params;
    ctx.target_airspeed_mps = target_airspeed_mps;
    ctx.dt_s = dt_s;
    ctx.settle_steps = (uint32_t)lrintf(config_positive_or(config->settle_time_s, 0.35f) / dt_s);
    ctx.eval_steps = (uint32_t)lrintf(config_positive_or(config->eval_time_s, 0.15f) / dt_s);
    if (ctx.settle_steps < 1U)
    {
        ctx.settle_steps = 1U;
    }
    if (ctx.eval_steps < 1U)
    {
        ctx.eval_steps = 1U;
    }

    bayek_trim_problem_default(&problem);
    problem.variable_count = 5U;
    problem.residual_count = 6U;
    problem.max_iterations = config->max_iterations > 0U ? config->max_iterations : 20U;
    problem.tolerance = config_positive_or(config->tolerance, 2.0e-3f);
    problem.damping = 1.0e-3f;
    problem.finite_difference_step = 1.0e-3f;
    problem.user = &ctx;
    problem.apply = fixedwing_apply;
    problem.evaluate = fixedwing_residual;

    euler = euler_from_quat(plant->body.attitude_body_to_ned);
    set_variable(&problem.variables[0],
                 "throttle",
                 clamp_real(plant->body.actuator_state.motor, 0.15f, 0.95f),
                 0.0f,
                 1.0f,
                 1.0f,
                 0.01f);
    set_variable(&problem.variables[1],
                 "elevator",
                 clamp_real(plant->body.actuator_state.elevator, vehicle_params->min_actuator,
                            vehicle_params->max_actuator),
                 vehicle_params->min_actuator,
                 vehicle_params->max_actuator,
                 1.0f,
                 0.01f);
    set_variable(&problem.variables[2],
                 "aileron",
                 clamp_real(plant->body.actuator_state.aileron, vehicle_params->min_actuator,
                            vehicle_params->max_actuator),
                 vehicle_params->min_actuator,
                 vehicle_params->max_actuator,
                 1.0f,
                 0.01f);
    set_variable(&problem.variables[3],
                 "rudder",
                 clamp_real(plant->body.actuator_state.rudder, vehicle_params->min_actuator,
                            vehicle_params->max_actuator),
                 vehicle_params->min_actuator,
                 vehicle_params->max_actuator,
                 1.0f,
                 0.01f);
    set_variable(&problem.variables[4],
                 "pitch_rad",
                 clamp_real(euler.pitch, -0.25f, 0.25f),
                 -0.35f,
                 0.35f,
                 0.2f,
                 0.005f);

    status->solver_status = bayek_trim_solve(&problem, &result);
    status->iteration_count = result.iteration_count;
    status->residual_norm = result.residual_norm;
    if (status->solver_status != BAYEK_TRIM_STATUS_CONVERGED)
    {
        status->failed = 1U;
        (void)snprintf(error,
                       error_size,
                       "fixed-wing trim failed: %s residual=%.6f iterations=%u",
                       bayek_trim_status_name(status->solver_status),
                       (double)status->residual_norm,
                       (unsigned)status->iteration_count);
        return 0;
    }

    if (!fixedwing_apply(&problem, result.variables, &ctx))
    {
        status->failed = 1U;
        set_error(error, error_size, "failed to apply fixed-wing trim solution");
        return 0;
    }
    solved = variables_to_actuators(result.variables);
    *plant = ctx.candidate;
    plant->body.actuator_state = solved;
    status->active = 0U;
    status->achieved = 1U;
    status->actuators = solved;
    status->pitch_rad = result.variables[4].value;
    if (error != NULL && error_size > 0U)
    {
        error[0] = '\0';
    }
    return 1;
}
