#include "altair_vehicle.h"
#include "fsw.h"
#include "math_utils.h"
#include "sim_plant.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *scenario;
    uint32_t seed;
    int runs;
    double duration_s;
    double dt_s;
    const char *output_path;
    real_t throttle_bias_span;
} mc_config_t;

enum
{
    MC_EXIT_CLI_OR_OUTPUT_ERROR = 1,
    MC_EXIT_FAILED_GATE = 3
};

static const real_t MC_MIN_AIRSPEED_MPS = 1.0f;
static const real_t MC_MAX_AIRSPEED_MPS = 80.0f;
static const real_t MC_MIN_ALTITUDE_M = -100.0f;
static const real_t MC_MAX_ALTITUDE_M = 10000.0f;
static const real_t MC_MAX_ABS_ROLL_RAD = 1.5708f;

static int mc_mode_is_valid(fsw_mode_t mode)
{
    return mode == FSW_MODE_DISARMED || mode == FSW_MODE_MANUAL || mode == FSW_MODE_STABILIZE ||
           mode == FSW_MODE_FAILSAFE || mode == FSW_MODE_MISSION;
}

static int mc_plant_state_is_finite(const sim_plant_t *plant)
{
    return real_is_finite(plant->attitude.roll) && real_is_finite(plant->attitude.pitch) &&
           real_is_finite(plant->attitude.yaw) && real_is_finite(plant->rates_rps.x) &&
           real_is_finite(plant->rates_rps.y) && real_is_finite(plant->rates_rps.z) &&
           real_is_finite(plant->altitude_m) && real_is_finite(plant->airspeed_mps) &&
           real_is_finite(plant->actuator_state.motor) &&
           real_is_finite(plant->actuator_state.aileron) &&
           real_is_finite(plant->actuator_state.elevator) &&
           real_is_finite(plant->actuator_state.rudder);
}

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "usage: mc_runner [--scenario smoke] [--seed uint] [--runs count]\n"
            "                 [--duration seconds] [--dt seconds] [--output path]\n"
            "       mc_runner [seed runs]\n");
}

static int parse_double_arg(const char *text, const char *name, double *value)
{
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0')
    {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_uint_arg(const char *text, const char *name, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > 0xffffffffUL)
    {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_int_arg(const char *text, const char *name, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > 2147483647L || parsed < -2147483647L)
    {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static int parse_args(int argc, char **argv, mc_config_t *cfg)
{
    int i;

    cfg->scenario = "smoke";
    cfg->seed = 1U;
    cfg->runs = 10;
    cfg->duration_s = 5.0;
    cfg->dt_s = 0.01;
    cfg->output_path = NULL;
    cfg->throttle_bias_span = 0.08f;

    if (argc > 1 && argv[1][0] != '-')
    {
        if (!parse_uint_arg(argv[1], "seed", &cfg->seed))
        {
            return -1;
        }
        if (argc > 2 && !parse_int_arg(argv[2], "runs", &cfg->runs))
        {
            return -1;
        }
        if (argc > 3)
        {
            fprintf(stderr, "unexpected positional argument: %s\n", argv[3]);
            return -1;
        }
    }
    else
    {
        for (i = 1; i < argc; ++i)
        {
            if (strcmp(argv[i], "--help") == 0)
            {
                print_usage(stdout);
                return 1;
            }
            else if (strcmp(argv[i], "--scenario") == 0)
            {
                if (++i >= argc)
                {
                    fprintf(stderr, "--scenario requires a value\n");
                    return -1;
                }
                cfg->scenario = argv[i];
            }
            else if (strcmp(argv[i], "--seed") == 0)
            {
                if (++i >= argc || !parse_uint_arg(argv[i], "seed", &cfg->seed))
                {
                    return -1;
                }
            }
            else if (strcmp(argv[i], "--runs") == 0)
            {
                if (++i >= argc || !parse_int_arg(argv[i], "runs", &cfg->runs))
                {
                    return -1;
                }
            }
            else if (strcmp(argv[i], "--duration") == 0)
            {
                if (++i >= argc || !parse_double_arg(argv[i], "duration", &cfg->duration_s))
                {
                    return -1;
                }
            }
            else if (strcmp(argv[i], "--dt") == 0)
            {
                if (++i >= argc || !parse_double_arg(argv[i], "dt", &cfg->dt_s))
                {
                    return -1;
                }
            }
            else if (strcmp(argv[i], "--output") == 0)
            {
                if (++i >= argc)
                {
                    fprintf(stderr, "--output requires a value\n");
                    return -1;
                }
                cfg->output_path = argv[i];
            }
            else
            {
                fprintf(stderr, "unknown option: %s\n", argv[i]);
                return -1;
            }
        }
    }

    if (strcmp(cfg->scenario, "smoke") != 0)
    {
        fprintf(stderr, "unknown scenario: %s\n", cfg->scenario);
        return -1;
    }
    if (cfg->runs <= 0)
    {
        fprintf(stderr, "runs must be positive\n");
        return -1;
    }
    if (cfg->duration_s <= 0.0)
    {
        fprintf(stderr, "duration must be positive\n");
        return -1;
    }
    if (cfg->dt_s <= 0.0)
    {
        fprintf(stderr, "dt must be positive\n");
        return -1;
    }
    return 0;
}

static int checked_close(FILE *stream, const char *path)
{
    if (stream == stdout)
    {
        if (fflush(stream) != 0)
        {
            fprintf(stderr, "failed to flush stdout\n");
            return 0;
        }
        return 1;
    }
    if (fclose(stream) != 0)
    {
        fprintf(stderr, "failed to close output %s\n", path);
        return 0;
    }
    return 1;
}

static uint32_t lcg_next(uint32_t *state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state;
}

static real_t rand_unit(uint32_t *state)
{
    return (real_t)((lcg_next(state) >> 8) & 0x00ffffffU) / 16777215.0f;
}

int main(int argc, char **argv)
{
    mc_config_t cfg;
    int run;
    int steps;
    int any_failed = 0;
    int parse_result;
    FILE *csv = stdout;

    parse_result = parse_args(argc, argv, &cfg);
    if (parse_result > 0)
    {
        return 0;
    }
    if (parse_result < 0)
    {
        print_usage(stderr);
        return MC_EXIT_CLI_OR_OUTPUT_ERROR;
    }

    steps = (int)(cfg.duration_s / cfg.dt_s);
    if (steps <= 0)
    {
        fprintf(stderr, "duration and dt produce no simulation steps\n");
        return MC_EXIT_CLI_OR_OUTPUT_ERROR;
    }
    if (cfg.output_path != NULL)
    {
        csv = fopen(cfg.output_path, "w");
        if (csv == NULL)
        {
            fprintf(stderr, "failed to open output %s\n", cfg.output_path);
            return MC_EXIT_CLI_OR_OUTPUT_ERROR;
        }
    }

    if (fprintf(csv,
                "run,seed,scenario,passed,failure_reason,throttle_bias,final_airspeed_mps,final_"
                "altitude_m,max_abs_roll_rad\n") < 0)
    {
        fprintf(stderr, "failed to write output\n");
        if (csv != stdout)
        {
            (void)fclose(csv);
        }
        return MC_EXIT_CLI_OR_OUTPUT_ERROR;
    }
    for (run = 0; run < cfg.runs; ++run)
    {
        uint32_t rng = cfg.seed + (uint32_t)run;
        real_t throttle_bias = (rand_unit(&rng) * 2.0f - 1.0f) * cfg.throttle_bias_span;
        real_t max_abs_roll = 0.0f;
        int passed = 1;
        const char *failure_reason = "";
        int step;
        sim_plant_t plant;
        fsw_input_t input;
        fsw_output_t output;
        rc_input_t rc = {0.52f + throttle_bias, 0.05f, 0.0f, 0.0f, 1U, 1U};

        bayek_fsw_init(altair_vehicle_interface());
        sim_plant_init(&plant);
        for (step = 0; step < steps; ++step)
        {
            real_t abs_roll;
            sim_make_fsw_input(
                &plant, &rc, (real_t)cfg.dt_s, (uint32_t)(step * cfg.dt_s * 1000000.0), &input);
            bayek_fsw_step(&input, &output);
            if (!sim_output_is_bounded(&output))
            {
                passed = 0;
                failure_reason = "unbounded_output";
                any_failed = 1;
                break;
            }
            if (!mc_mode_is_valid(output.mode))
            {
                passed = 0;
                failure_reason = "invalid_mode";
                any_failed = 1;
                break;
            }
            sim_plant_step(&plant, &output.actuators, (real_t)cfg.dt_s);
            if (!mc_plant_state_is_finite(&plant))
            {
                passed = 0;
                failure_reason = "nonfinite_state";
                any_failed = 1;
                break;
            }
            abs_roll = plant.attitude.roll < 0.0f ? -plant.attitude.roll : plant.attitude.roll;
            if (abs_roll > max_abs_roll)
            {
                max_abs_roll = abs_roll;
            }
            if (!real_is_finite(max_abs_roll))
            {
                passed = 0;
                failure_reason = "nonfinite_state";
                any_failed = 1;
                break;
            }
            if (max_abs_roll > MC_MAX_ABS_ROLL_RAD)
            {
                passed = 0;
                failure_reason = "roll_limit";
                any_failed = 1;
                break;
            }
        }
        if (passed && !mc_plant_state_is_finite(&plant))
        {
            passed = 0;
            failure_reason = "nonfinite_state";
            any_failed = 1;
        }
        if (passed &&
            (plant.airspeed_mps < MC_MIN_AIRSPEED_MPS || plant.airspeed_mps > MC_MAX_AIRSPEED_MPS))
        {
            passed = 0;
            failure_reason = "airspeed_limit";
            any_failed = 1;
        }
        if (passed &&
            (plant.altitude_m < MC_MIN_ALTITUDE_M || plant.altitude_m > MC_MAX_ALTITUDE_M))
        {
            passed = 0;
            failure_reason = "altitude_limit";
            any_failed = 1;
        }
        if (fprintf(csv,
                    "%d,%u,%s,%d,%s,%.6f,%.6f,%.6f,%.6f\n",
                    run,
                    cfg.seed + (uint32_t)run,
                    cfg.scenario,
                    passed,
                    failure_reason,
                    (double)throttle_bias,
                    (double)plant.airspeed_mps,
                    (double)plant.altitude_m,
                    (double)max_abs_roll) < 0)
        {
            fprintf(stderr, "failed to write output\n");
            if (csv != stdout)
            {
                (void)fclose(csv);
            }
            return MC_EXIT_CLI_OR_OUTPUT_ERROR;
        }
    }
    if (!checked_close(csv, cfg.output_path != NULL ? cfg.output_path : "stdout"))
    {
        return MC_EXIT_CLI_OR_OUTPUT_ERROR;
    }
    return any_failed ? MC_EXIT_FAILED_GATE : 0;
}
