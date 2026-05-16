#include "trim_solver.h"

#include <math.h>
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

typedef struct
{
    real_t x[2];
    int invalid;
} synthetic_context_t;

static int apply_variables(const bayek_trim_problem_t *problem,
                           const bayek_trim_variable_t *variables,
                           void *user)
{
    synthetic_context_t *ctx = (synthetic_context_t *)user;
    uint32_t i;
    (void)problem;
    for (i = 0U; i < 2U; ++i)
    {
        ctx->x[i] = variables[i].value;
    }
    return 1;
}

static int residual_scalar(const bayek_trim_problem_t *problem, real_t *residuals, void *user)
{
    synthetic_context_t *ctx = (synthetic_context_t *)user;
    (void)problem;
    residuals[0] = 2.0f * ctx->x[0] - 6.0f;
    return 1;
}

static int residual_lsq(const bayek_trim_problem_t *problem, real_t *residuals, void *user)
{
    synthetic_context_t *ctx = (synthetic_context_t *)user;
    (void)problem;
    residuals[0] = ctx->x[0] + ctx->x[1] - 3.0f;
    residuals[1] = ctx->x[0] - ctx->x[1] - 1.0f;
    residuals[2] = 2.0f * ctx->x[0] + ctx->x[1] - 5.0f;
    return 1;
}

static int residual_invalid(const bayek_trim_problem_t *problem, real_t *residuals, void *user)
{
    synthetic_context_t *ctx = (synthetic_context_t *)user;
    (void)problem;
    residuals[0] = ctx->invalid ? NAN : ctx->x[0] - 1.0f;
    return 1;
}

static int residual_singular(const bayek_trim_problem_t *problem, real_t *residuals, void *user)
{
    (void)problem;
    (void)user;
    residuals[0] = 1.0f;
    return 1;
}

static void set_var(bayek_trim_variable_t *variable,
                    const char *name,
                    real_t value,
                    real_t min_value,
                    real_t max_value)
{
    variable->name = name;
    variable->value = value;
    variable->min_value = min_value;
    variable->max_value = max_value;
    variable->scale = 1.0f;
    variable->perturbation = 1.0e-3f;
}

int main(void)
{
    bayek_trim_problem_t problem;
    bayek_trim_result_t result;
    synthetic_context_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    bayek_trim_problem_default(&problem);
    problem.variable_count = 1U;
    problem.residual_count = 1U;
    problem.user = &ctx;
    problem.apply = apply_variables;
    problem.evaluate = residual_scalar;
    set_var(&problem.variables[0], "x", 0.0f, -10.0f, 10.0f);
    CHECK(bayek_trim_solve(&problem, &result) == BAYEK_TRIM_STATUS_CONVERGED);
    CHECK(fabsf(result.variables[0].value - 3.0f) < 1.0e-3f);

    memset(&ctx, 0, sizeof(ctx));
    bayek_trim_problem_default(&problem);
    problem.variable_count = 2U;
    problem.residual_count = 3U;
    problem.user = &ctx;
    problem.apply = apply_variables;
    problem.evaluate = residual_lsq;
    set_var(&problem.variables[0], "x", 0.0f, -10.0f, 10.0f);
    set_var(&problem.variables[1], "y", 0.0f, -10.0f, 10.0f);
    CHECK(bayek_trim_solve(&problem, &result) == BAYEK_TRIM_STATUS_CONVERGED);
    CHECK(fabsf(result.variables[0].value - 2.0f) < 5.0e-3f);
    CHECK(fabsf(result.variables[1].value - 1.0f) < 5.0e-3f);

    memset(&ctx, 0, sizeof(ctx));
    bayek_trim_problem_default(&problem);
    problem.variable_count = 1U;
    problem.residual_count = 1U;
    problem.max_iterations = 5U;
    problem.tolerance = 1.0e-5f;
    problem.user = &ctx;
    problem.apply = apply_variables;
    problem.evaluate = residual_scalar;
    set_var(&problem.variables[0], "x", 0.0f, 0.0f, 2.0f);
    CHECK(bayek_trim_solve(&problem, &result) != BAYEK_TRIM_STATUS_CONVERGED);
    CHECK(result.variables[0].value >= 0.0f && result.variables[0].value <= 2.0f);

    memset(&ctx, 0, sizeof(ctx));
    ctx.invalid = 1;
    bayek_trim_problem_default(&problem);
    problem.variable_count = 1U;
    problem.residual_count = 1U;
    problem.user = &ctx;
    problem.apply = apply_variables;
    problem.evaluate = residual_invalid;
    set_var(&problem.variables[0], "x", 0.0f, -10.0f, 10.0f);
    CHECK(bayek_trim_solve(&problem, &result) == BAYEK_TRIM_STATUS_INVALID_RESIDUAL);

    memset(&ctx, 0, sizeof(ctx));
    bayek_trim_problem_default(&problem);
    problem.variable_count = 1U;
    problem.residual_count = 1U;
    problem.user = &ctx;
    problem.apply = apply_variables;
    problem.evaluate = residual_singular;
    set_var(&problem.variables[0], "x", 0.0f, -10.0f, 10.0f);
    CHECK(bayek_trim_solve(&problem, &result) == BAYEK_TRIM_STATUS_NON_IMPROVING);

    memset(&ctx, 0, sizeof(ctx));
    bayek_trim_problem_default(&problem);
    problem.variable_count = 1U;
    problem.residual_count = 1U;
    problem.damping = 0.0f;
    problem.user = &ctx;
    problem.apply = apply_variables;
    problem.evaluate = residual_singular;
    set_var(&problem.variables[0], "x", 0.0f, -10.0f, 10.0f);
    CHECK(bayek_trim_solve(&problem, &result) == BAYEK_TRIM_STATUS_SINGULAR);

    return 0;
}
