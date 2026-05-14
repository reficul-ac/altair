#include "altair_params.h"
#include "altair_sim_params.h"
#include "sitl_conditions.h"

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
    return fabsf(a - b) < 1.0e-5f;
}

static void make_context(sitl_condition_context_t *ctx,
                         rc_input_t *rc,
                         fsw_input_t *input,
                         vehicle_params_t *vehicle_params,
                         sim_fixedwing_params_t *sim_params,
                         sim_fixedwing_state_t *plant,
                         sitl_trim_config_t *trim_config,
                         uint8_t *mission_enabled,
                         bayek_mission_plan_t *mission)
{
    memset(ctx, 0, sizeof(*ctx));
    memset(input, 0, sizeof(*input));
    sim_fixedwing_init_default(plant);
    *rc = (rc_input_t){0.5f, 0.0f, 0.0f, 0.0f, 1U, 1U};
    input->rc = *rc;
    *vehicle_params = *altair_default_params();
    altair_default_fixedwing_sim_params(sim_params);
    sitl_trim_config_default(trim_config);
    memset(mission, 0, sizeof(*mission));
    *mission_enabled = 0U;
    ctx->rc = rc;
    ctx->input = input;
    ctx->vehicle_params = vehicle_params;
    ctx->sim_params = sim_params;
    ctx->plant = plant;
    ctx->trim = trim_config;
    ctx->mission_enabled = mission_enabled;
    ctx->mission = mission;
}

int main(void)
{
    sitl_conditions_t conditions;
    sitl_condition_context_t ctx;
    rc_input_t rc;
    fsw_input_t input;
    vehicle_params_t vehicle_params;
    sim_fixedwing_params_t sim_params;
    sim_fixedwing_state_t plant;
    sitl_trim_config_t trim_config;
    uint8_t mission_enabled;
    bayek_mission_plan_t mission;
    char error[200];

    CHECK(write_file("sitl_conditions_valid.ini",
                     "[rule.param_stomp_after_20s]\n"
                     "when = t_s > 20\n"
                     "vehicle_params.max_airspeed_mps = 7\n"
                     "\n"
                     "[rule.step_drop]\n"
                     "when = step >= 3\n"
                     "input.gps.fix_valid = 0\n"
                     "rc.throttle = 0.25\n"
                     "trim.enabled = 1\n"
                     "trim.mode = fixedwing_level\n"
                     "trim.max_iterations = 7\n"
                     "mission.enabled = 1\n"
                     "mission.waypoint_count = 1\n"
                     "mission.waypoint.0.lat_deg = 37.5\n"
                     "mission.waypoint.0.lon_deg = -122.1\n"
                     "mission.waypoint.0.alt_m = 120\n"
                     "mission.waypoint.0.throttle = 0.6\n"
                     "mission.waypoint.0.acceptance_radius_m = 30\n"));
    CHECK(sitl_conditions_load("sitl_conditions_valid.ini", &conditions, error, sizeof(error)));
    CHECK(conditions.rule_count == 2U);
    CHECK(conditions.rules[0].lhs == SITL_CONDITION_LHS_TIME_S);
    CHECK(conditions.rules[1].lhs == SITL_CONDITION_LHS_STEP);

    make_context(&ctx,
                 &rc,
                 &input,
                 &vehicle_params,
                 &sim_params,
                 &plant,
                 &trim_config,
                 &mission_enabled,
                 &mission);
    input.gps.fix_valid = 1U;
    ctx.t_s = 19.0;
    ctx.step = 2U;
    CHECK(sitl_conditions_eval(&conditions, &ctx, error, sizeof(error)));
    CHECK(near_real(vehicle_params.max_airspeed_mps, altair_default_params()->max_airspeed_mps));
    CHECK(input.gps.fix_valid == 1U);

    ctx.t_s = 21.0;
    ctx.step = 3U;
    CHECK(sitl_conditions_eval(&conditions, &ctx, error, sizeof(error)));
    CHECK(near_real(vehicle_params.max_airspeed_mps, 7.0f));
    CHECK(input.gps.fix_valid == 0U);
    CHECK(near_real(rc.throttle, 0.25f));
    CHECK(near_real(input.rc.throttle, 0.25f));
    CHECK(trim_config.enabled == 1U);
    CHECK(trim_config.mode == SITL_TRIM_MODE_FIXEDWING_LEVEL);
    CHECK(trim_config.max_iterations == 7U);
    CHECK(mission_enabled == 1U);
    CHECK(mission.waypoint_count == 1U);
    CHECK(ctx.vehicle_params_dirty == 1U);
    CHECK(ctx.mission_dirty == 1U);

    CHECK(write_file("sitl_conditions_unknown_target.ini",
                     "[rule.bad]\n"
                     "when = t_s > 1\n"
                     "unknown.value = 1\n"));
    CHECK(!sitl_conditions_load(
        "sitl_conditions_unknown_target.ini", &conditions, error, sizeof(error)));
    CHECK(strstr(error, "unknown assignment target") != NULL);

    CHECK(write_file("sitl_conditions_bad_comparator.ini",
                     "[rule.bad]\n"
                     "when = t_s <> 1\n"
                     "input.gps.fix_valid = 0\n"));
    CHECK(!sitl_conditions_load(
        "sitl_conditions_bad_comparator.ini", &conditions, error, sizeof(error)));
    CHECK(strstr(error, "line 2") != NULL);

    CHECK(write_file("sitl_conditions_bad_numeric.ini",
                     "[rule.bad]\n"
                     "when = step >= nope\n"
                     "input.gps.fix_valid = 0\n"));
    CHECK(!sitl_conditions_load(
        "sitl_conditions_bad_numeric.ini", &conditions, error, sizeof(error)));
    CHECK(strstr(error, "line 2") != NULL);

    CHECK(write_file("sitl_conditions_outside_rule.ini", "input.gps.fix_valid = 0\n"));
    CHECK(!sitl_conditions_load(
        "sitl_conditions_outside_rule.ini", &conditions, error, sizeof(error)));
    CHECK(strstr(error, "line 1") != NULL);

    return 0;
}
