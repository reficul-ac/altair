#include "altair_vehicle.h"
#include "fsw.h"
#include "math_utils.h"
#include "sim_fixedwing.h"
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
          "usage: sitl_runner [--scenario smoke|cruise6dof] [--duration seconds] [--dt seconds]\n"
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

  if (strcmp(cfg->scenario, "smoke") != 0 && strcmp(cfg->scenario, "cruise6dof") != 0) {
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

static int run_smoke(const sitl_config_t *cfg, int steps, FILE *csv) {
  sim_plant_t plant;
  fsw_input_t input;
  fsw_output_t output;
  rc_input_t rc = {0.55f, 0.10f, 0.02f, 0.0f, 1U, 1U};
  int i;

  bayek_fsw_init(altair_vehicle_interface());
  sim_plant_init(&plant);

  if (fprintf(csv, "step,time_s,mode,motor,aileron,elevator,rudder,airspeed_mps,altitude_m\n") < 0) {
    fprintf(stderr, "failed to write output\n");
    if (csv != stdout) {
      (void)fclose(csv);
    }
    return 1;
  }
  for (i = 0; i < steps; ++i) {
    sim_make_fsw_input(&plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
    bayek_fsw_step(&input, &output);
    if (!sim_output_is_bounded(&output)) {
      fprintf(stderr, "unbounded_output at step %d\n", i);
      return 2;
    }
    sim_plant_step(&plant, &output.actuators, (real_t)cfg->dt_s);
    if (fprintf(csv,
                "%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                i,
                (double)(i * cfg->dt_s),
                (int)output.mode,
                (double)output.actuators.motor,
                (double)output.actuators.aileron,
                (double)output.actuators.elevator,
                (double)output.actuators.rudder,
                (double)plant.airspeed_mps,
                (double)plant.altitude_m) < 0) {
      fprintf(stderr, "failed to write output\n");
      return 1;
    }
  }
  return 0;
}

static int run_cruise6dof(const sitl_config_t *cfg, int steps, FILE *csv) {
  sim_fixedwing_params_t params;
  sim_fixedwing_state_t plant;
  fsw_input_t input;
  fsw_output_t output;
  rc_input_t rc = {0.58f, 0.0f, 0.02f, 0.0f, 1U, 1U};
  int i;

  bayek_fsw_init(altair_vehicle_interface());
  sim_fixedwing_default_params(&params);
  sim_fixedwing_init_default(&plant);

  if (fprintf(csv,
              "step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,"
              "pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,roll_rad,pitch_rad,yaw_rad,"
              "p_rps,q_rps,r_rps,airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2\n") < 0) {
    fprintf(stderr, "failed to write output\n");
    return 1;
  }

  for (i = 0; i < steps; ++i) {
    euler_t euler;
    sim_fixedwing_make_fsw_input(&plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
    bayek_fsw_step(&input, &output);
    if (!sim_output_is_bounded(&output)) {
      fprintf(stderr, "unbounded_output at step %d\n", i);
      return 2;
    }
    if (!sim_fixedwing_step(&plant, &params, &output.actuators, (real_t)cfg->dt_s)) {
      fprintf(stderr, "invalid_sim_state at step %d\n", i);
      return 2;
    }
    euler = euler_from_quat(plant.body.attitude_body_to_ned);
    if (fprintf(csv,
                "%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                i,
                (double)(i * cfg->dt_s),
                (int)output.mode,
                (double)output.actuators.motor,
                (double)output.actuators.aileron,
                (double)output.actuators.elevator,
                (double)output.actuators.rudder,
                (double)rc.throttle,
                (double)rc.roll,
                (double)rc.pitch,
                (double)rc.yaw,
                (double)plant.body.position_ned_m.x,
                (double)plant.body.position_ned_m.y,
                (double)plant.body.position_ned_m.z,
                (double)plant.body.velocity_ned_mps.x,
                (double)plant.body.velocity_ned_mps.y,
                (double)plant.body.velocity_ned_mps.z,
                (double)euler.roll,
                (double)euler.pitch,
                (double)euler.yaw,
                (double)plant.body.omega_body_rps.x,
                (double)plant.body.omega_body_rps.y,
                (double)plant.body.omega_body_rps.z,
                (double)plant.last_airspeed_mps,
                (double)(-plant.body.position_ned_m.z),
                (double)plant.body.specific_force_body_mps2.x,
                (double)plant.body.specific_force_body_mps2.y,
                (double)plant.body.specific_force_body_mps2.z) < 0) {
      fprintf(stderr, "failed to write output\n");
      return 1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  sitl_config_t cfg;
  int steps;
  clock_t start;
  clock_t end;
  FILE *csv = stdout;
  int parse_result;
  int run_result;

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

  start = clock();
  if (strcmp(cfg.scenario, "cruise6dof") == 0) {
    run_result = run_cruise6dof(&cfg, steps, csv);
  } else {
    run_result = run_smoke(&cfg, steps, csv);
  }
  end = clock();
  if (run_result != 0) {
    if (csv != stdout) {
      (void)fclose(csv);
    }
    return run_result;
  }
  fprintf(stderr, "sitl_steps=%d elapsed_s=%.6f\n", steps, (double)(end - start) / (double)CLOCKS_PER_SEC);
  if (!checked_close(csv, cfg.output_path != NULL ? cfg.output_path : "stdout")) {
    return 1;
  }
  return 0;
}
