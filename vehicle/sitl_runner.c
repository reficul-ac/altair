#include "altair_vehicle.h"
#include "fsw.h"
#include "sim_plant.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  const char *scenario;
  double duration_s;
  double dt_s;
  uint32_t seed;
  const char *output_path;
} sitl_config_t;

static void print_usage(FILE *stream) {
  fprintf(stream,
          "usage: sitl_runner [--scenario smoke] [--duration seconds] [--dt seconds]\n"
          "                   [--seed uint] [--output path]\n");
}

static int parse_double_arg(const char *text, const char *name, double *value) {
  char *end = NULL;
  double parsed;

  errno = 0;
  parsed = strtod(text, &end);
  if (errno != 0 || end == text || *end != '\0') {
    fprintf(stderr, "invalid %s: %s\n", name, text);
    return 0;
  }
  *value = parsed;
  return 1;
}

static int parse_uint_arg(const char *text, const char *name, uint32_t *value) {
  char *end = NULL;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed > 0xffffffffUL) {
    fprintf(stderr, "invalid %s: %s\n", name, text);
    return 0;
  }
  *value = (uint32_t)parsed;
  return 1;
}

static int parse_args(int argc, char **argv, sitl_config_t *cfg) {
  int i;

  cfg->scenario = "smoke";
  cfg->duration_s = 10.0;
  cfg->dt_s = 0.01;
  cfg->seed = 1U;
  cfg->output_path = NULL;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0) {
      print_usage(stdout);
      return 1;
    } else if (strcmp(argv[i], "--scenario") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--scenario requires a value\n");
        return -1;
      }
      cfg->scenario = argv[i];
    } else if (strcmp(argv[i], "--duration") == 0) {
      if (++i >= argc || !parse_double_arg(argv[i], "duration", &cfg->duration_s)) {
        return -1;
      }
    } else if (strcmp(argv[i], "--dt") == 0) {
      if (++i >= argc || !parse_double_arg(argv[i], "dt", &cfg->dt_s)) {
        return -1;
      }
    } else if (strcmp(argv[i], "--seed") == 0) {
      if (++i >= argc || !parse_uint_arg(argv[i], "seed", &cfg->seed)) {
        return -1;
      }
    } else if (strcmp(argv[i], "--output") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--output requires a value\n");
        return -1;
      }
      cfg->output_path = argv[i];
    } else {
      fprintf(stderr, "unknown option: %s\n", argv[i]);
      return -1;
    }
  }

  if (strcmp(cfg->scenario, "smoke") != 0) {
    fprintf(stderr, "unknown scenario: %s\n", cfg->scenario);
    return -1;
  }
  if (cfg->duration_s <= 0.0) {
    fprintf(stderr, "duration must be positive\n");
    return -1;
  }
  if (cfg->dt_s <= 0.0) {
    fprintf(stderr, "dt must be positive\n");
    return -1;
  }
  (void)cfg->seed;
  return 0;
}

static int checked_close(FILE *stream, const char *path) {
  if (stream == stdout) {
    if (fflush(stream) != 0) {
      fprintf(stderr, "failed to flush stdout\n");
      return 0;
    }
    return 1;
  }
  if (fclose(stream) != 0) {
    fprintf(stderr, "failed to close output %s\n", path);
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  sitl_config_t cfg;
  int steps;
  sim_plant_t plant;
  fsw_input_t input;
  fsw_output_t output;
  rc_input_t rc = {0.55f, 0.10f, 0.02f, 0.0f, 1U, 1U};
  int i;
  clock_t start;
  clock_t end;
  FILE *csv = stdout;
  int parse_result;

  parse_result = parse_args(argc, argv, &cfg);
  if (parse_result > 0) {
    return 0;
  }
  if (parse_result < 0) {
    print_usage(stderr);
    return 1;
  }

  steps = (int)(cfg.duration_s / cfg.dt_s);
  if (steps <= 0) {
    fprintf(stderr, "duration and dt produce no simulation steps\n");
    return 1;
  }
  if (cfg.output_path != NULL) {
    csv = fopen(cfg.output_path, "w");
    if (csv == NULL) {
      fprintf(stderr, "failed to open output %s\n", cfg.output_path);
      return 1;
    }
  }

  bayek_fsw_init(altair_vehicle_interface());
  sim_plant_init(&plant);

  if (fprintf(csv, "step,time_s,mode,motor,aileron,elevator,rudder,airspeed_mps,altitude_m\n") < 0) {
    fprintf(stderr, "failed to write output\n");
    if (csv != stdout) {
      (void)fclose(csv);
    }
    return 1;
  }
  start = clock();
  for (i = 0; i < steps; ++i) {
    sim_make_fsw_input(&plant, &rc, (real_t)cfg.dt_s, (uint32_t)(i * cfg.dt_s * 1000000.0), &input);
    bayek_fsw_step(&input, &output);
    if (!sim_output_is_bounded(&output)) {
      if (csv != stdout) {
        (void)fclose(csv);
      }
      fprintf(stderr, "unbounded_output at step %d\n", i);
      return 2;
    }
    sim_plant_step(&plant, &output.actuators, (real_t)cfg.dt_s);
    if (fprintf(csv,
                "%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                i,
                (double)(i * cfg.dt_s),
                (int)output.mode,
                (double)output.actuators.motor,
                (double)output.actuators.aileron,
                (double)output.actuators.elevator,
                (double)output.actuators.rudder,
                (double)plant.airspeed_mps,
                (double)plant.altitude_m) < 0) {
      fprintf(stderr, "failed to write output\n");
      if (csv != stdout) {
        (void)fclose(csv);
      }
      return 1;
    }
  }
  end = clock();
  fprintf(stderr, "sitl_steps=%d elapsed_s=%.6f\n", steps, (double)(end - start) / (double)CLOCKS_PER_SEC);
  if (!checked_close(csv, cfg.output_path != NULL ? cfg.output_path : "stdout")) {
    return 1;
  }
  return 0;
}
