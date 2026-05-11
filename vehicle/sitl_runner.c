#include "altair_vehicle.h"
#include "fsw.h"
#include "math_utils.h"
#include "sim_fixedwing.h"
#include "sim_plant.h"
#include "sitl_initial_conditions.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

typedef struct {
  const char *scenario;
  double duration_s;
  double dt_s;
  uint32_t seed;
  const char *output_path;
  const char *initial_path;
  int realtime;
} sitl_config_t;

static void print_usage(FILE *stream) {
  fprintf(stream,
          "usage: sitl_runner [--scenario smoke|cruise6dof] [--duration seconds] [--dt seconds]\n"
          "                   [--seed uint] [--output path] [--initial path] [--realtime]\n");
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
  cfg->initial_path = NULL;
  cfg->realtime = 0;

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
    } else if (strcmp(argv[i], "--initial") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--initial requires a value\n");
        return -1;
      }
      cfg->initial_path = argv[i];
    } else if (strcmp(argv[i], "--realtime") == 0) {
      cfg->realtime = 1;
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
  if (cfg->initial_path != NULL && strcmp(cfg->scenario, "cruise6dof") != 0) {
    fprintf(stderr, "--initial is only supported with --scenario cruise6dof\n");
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

static double wall_time_s(void) {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    return 0.0;
  }
  return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void sleep_until_wall_s(double target_s) {
  double remaining_s = target_s - wall_time_s();
  struct timeval timeout;
  long usec;
  if (remaining_s <= 0.0) {
    return;
  }
  if (remaining_s > 1.0) {
    remaining_s = 1.0;
  }
  usec = (long)(remaining_s * 1000000.0);
  timeout.tv_sec = usec / 1000000L;
  timeout.tv_usec = usec % 1000000L;
  (void)select(0, NULL, NULL, NULL, &timeout);
}

static void pace_realtime_step(const sitl_config_t *cfg, double start_wall_s, int completed_steps) {
  if (!cfg->realtime) {
    return;
  }
  sleep_until_wall_s(start_wall_s + (double)completed_steps * cfg->dt_s);
}

static int run_smoke(const sitl_config_t *cfg, int steps, FILE *csv) {
  sim_plant_t plant;
  fsw_input_t input;
  fsw_output_t output;
  rc_input_t rc = {0.55f, 0.10f, 0.02f, 0.0f, 1U, 1U};
  int i;
  double start_wall_s;

  bayek_fsw_init(altair_vehicle_interface());
  sim_plant_init(&plant);
  start_wall_s = wall_time_s();

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
    pace_realtime_step(cfg, start_wall_s, i + 1);
  }
  return 0;
}

static void apply_initial_conditions(sim_fixedwing_state_t *plant, const sitl_initial_conditions_t *initial) {
  euler_t attitude;
  if (plant == NULL || initial == NULL) {
    return;
  }
  attitude.roll = initial->roll_rad;
  attitude.pitch = initial->pitch_rad;
  attitude.yaw = initial->yaw_rad;
  plant->body.attitude_body_to_ned = quat_from_euler(attitude);
  plant->body.position_ned_m.x = 0.0f;
  plant->body.position_ned_m.y = 0.0f;
  plant->body.position_ned_m.z = -initial->altitude_m;
  if (initial->has_velocity_ned) {
    plant->body.velocity_ned_mps.x = initial->vel_n_mps;
    plant->body.velocity_ned_mps.y = initial->vel_e_mps;
    plant->body.velocity_ned_mps.z = initial->vel_d_mps;
  } else {
    vec3_t forward_body = {initial->airspeed_mps, 0.0f, 0.0f};
    plant->body.velocity_ned_mps = quat_rotate_vec3(plant->body.attitude_body_to_ned, forward_body);
  }
  plant->body.omega_body_rps.x = initial->p_rps;
  plant->body.omega_body_rps.y = initial->q_rps;
  plant->body.omega_body_rps.z = initial->r_rps;
  plant->last_airspeed_mps = initial->airspeed_mps;
}

static void ned_to_geo(real_t lat0_deg,
                       real_t lon0_deg,
                       vec3_t position_ned_m,
                       real_t *lat_deg,
                       real_t *lon_deg,
                       real_t *altitude_m) {
  const real_t earth_radius_m = 6378137.0f;
  real_t lat0_rad = lat0_deg * (BAYEK_PI / 180.0f);
  real_t cos_lat0 = (real_t)cosf(lat0_rad);
  if (fabsf(cos_lat0) < 1.0e-6f) {
    cos_lat0 = cos_lat0 < 0.0f ? -1.0e-6f : 1.0e-6f;
  }
  *lat_deg = lat0_deg + (position_ned_m.x / earth_radius_m) * (180.0f / BAYEK_PI);
  *lon_deg = lon0_deg + (position_ned_m.y / (earth_radius_m * cos_lat0)) * (180.0f / BAYEK_PI);
  *altitude_m = -position_ned_m.z;
}

static int run_cruise6dof(const sitl_config_t *cfg, int steps, FILE *csv) {
  sim_fixedwing_params_t params;
  sim_fixedwing_state_t plant;
  fsw_input_t input;
  fsw_output_t output;
  sitl_initial_conditions_t initial;
  char initial_error[160];
  rc_input_t rc;
  int i;
  double start_wall_s;

  bayek_fsw_init(altair_vehicle_interface());
  sim_fixedwing_default_params(&params);
  sim_fixedwing_init_default(&plant);
  sitl_initial_conditions_default(&initial);
  if (cfg->initial_path != NULL) {
    if (!sitl_initial_conditions_load(cfg->initial_path, &initial, initial_error, sizeof(initial_error))) {
      fprintf(stderr, "failed to load initial conditions: %s\n", initial_error);
      return 1;
    }
  }
  apply_initial_conditions(&plant, &initial);
  rc = initial.rc;
  start_wall_s = wall_time_s();

  if (fprintf(csv,
              "step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,"
              "lat_deg,lon_deg,pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,"
              "roll_rad,pitch_rad,yaw_rad,quat_w,quat_x,quat_y,quat_z,p_rps,q_rps,r_rps,"
              "airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2,"
              "force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm\n") < 0) {
    fprintf(stderr, "failed to write output\n");
    return 1;
  }

  for (i = 0; i < steps; ++i) {
    euler_t euler;
    real_t lat_deg;
    real_t lon_deg;
    real_t altitude_m;
    sim_fixedwing_make_fsw_input(&plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
    ned_to_geo(initial.lat_deg, initial.lon_deg, plant.body.position_ned_m, &lat_deg, &lon_deg, &altitude_m);
    input.gps.lat_deg = lat_deg;
    input.gps.lon_deg = lon_deg;
    input.gps.alt_m = altitude_m;
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
    ned_to_geo(initial.lat_deg, initial.lon_deg, plant.body.position_ned_m, &lat_deg, &lon_deg, &altitude_m);
    if (fprintf(csv,
                "%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.8f,%.8f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.8f,%.8f,%.8f,%.8f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
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
                (double)lat_deg,
                (double)lon_deg,
                (double)plant.body.position_ned_m.x,
                (double)plant.body.position_ned_m.y,
                (double)plant.body.position_ned_m.z,
                (double)plant.body.velocity_ned_mps.x,
                (double)plant.body.velocity_ned_mps.y,
                (double)plant.body.velocity_ned_mps.z,
                (double)euler.roll,
                (double)euler.pitch,
                (double)euler.yaw,
                (double)plant.body.attitude_body_to_ned.w,
                (double)plant.body.attitude_body_to_ned.x,
                (double)plant.body.attitude_body_to_ned.y,
                (double)plant.body.attitude_body_to_ned.z,
                (double)plant.body.omega_body_rps.x,
                (double)plant.body.omega_body_rps.y,
                (double)plant.body.omega_body_rps.z,
                (double)plant.last_airspeed_mps,
                (double)altitude_m,
                (double)plant.body.specific_force_body_mps2.x,
                (double)plant.body.specific_force_body_mps2.y,
                (double)plant.body.specific_force_body_mps2.z,
                (double)plant.last_force_body_n.x,
                (double)plant.last_force_body_n.y,
                (double)plant.last_force_body_n.z,
                (double)plant.last_moment_body_nm.x,
                (double)plant.last_moment_body_nm.y,
                (double)plant.last_moment_body_nm.z) < 0) {
      fprintf(stderr, "failed to write output\n");
      return 1;
    }
    pace_realtime_step(cfg, start_wall_s, i + 1);
  }
  return 0;
}

int main(int argc, char **argv) {
  sitl_config_t cfg;
  int steps;
  clock_t start;
  clock_t end;
  double start_wall_s;
  double end_wall_s;
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
  start_wall_s = wall_time_s();
  if (strcmp(cfg.scenario, "cruise6dof") == 0) {
    run_result = run_cruise6dof(&cfg, steps, csv);
  } else {
    run_result = run_smoke(&cfg, steps, csv);
  }
  end = clock();
  end_wall_s = wall_time_s();
  if (run_result != 0) {
    if (csv != stdout) {
      (void)fclose(csv);
    }
    return run_result;
  }
  fprintf(stderr,
          "sitl_steps=%d cpu_elapsed_s=%.6f wall_elapsed_s=%.6f realtime=%d\n",
          steps,
          (double)(end - start) / (double)CLOCKS_PER_SEC,
          end_wall_s - start_wall_s,
          cfg.realtime);
  if (!checked_close(csv, cfg.output_path != NULL ? cfg.output_path : "stdout")) {
    return 1;
  }
  return 0;
}
