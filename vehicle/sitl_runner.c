#include "altair_vehicle.h"
#include "altair_sim_model.h"
#include "fsw.h"
#include "math_utils.h"
#include "sim_plant.h"
#include "sitl_initial_conditions.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  const char *scenario;
  const char *profile;
  double duration_s;
  double dt_s;
  uint32_t seed;
  const char *output_path;
  const char *initial_path;
  int frame_mode;
  int realtime;
  int qgc_enabled;
  const char *qgc_host;
  const char *qgc_port;
} sitl_config_t;

typedef struct {
  int fd;
  struct sockaddr_storage addr;
  socklen_t addr_len;
  uint8_t seq;
} qgc_link_t;

static void print_usage(FILE *stream) {
  fprintf(stream,
          "usage: sitl_runner [--scenario smoke|cruise6dof] [--duration seconds] [--dt seconds]\n"
          "                   [--seed uint] [--output path] [--initial path]\n"
          "                   [--frame-mode ned|ecef]\n"
          "                   [--profile cruise|takeoff|turn|descent|failsafe] [--realtime]\n"
          "                   [--qgc] [--qgc-host host] [--qgc-port port]\n");
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
  cfg->profile = "cruise";
  cfg->duration_s = 10.0;
  cfg->dt_s = 0.01;
  cfg->seed = 1U;
  cfg->output_path = NULL;
  cfg->initial_path = NULL;
  cfg->frame_mode = SIM6DOF_FRAME_ECEF;
  cfg->realtime = 0;
  cfg->qgc_enabled = 0;
  cfg->qgc_host = "127.0.0.1";
  cfg->qgc_port = "14550";

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
    } else if (strcmp(argv[i], "--profile") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--profile requires a value\n");
        return -1;
      }
      cfg->profile = argv[i];
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
    } else if (strcmp(argv[i], "--frame-mode") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--frame-mode requires a value\n");
        return -1;
      }
      if (strcmp(argv[i], "ned") == 0) {
        cfg->frame_mode = SIM6DOF_FRAME_NED;
      } else if (strcmp(argv[i], "ecef") == 0) {
        cfg->frame_mode = SIM6DOF_FRAME_ECEF;
      } else {
        fprintf(stderr, "invalid --frame-mode: %s\n", argv[i]);
        return -1;
      }
    } else if (strcmp(argv[i], "--realtime") == 0) {
      cfg->realtime = 1;
    } else if (strcmp(argv[i], "--qgc") == 0) {
      cfg->qgc_enabled = 1;
      cfg->realtime = 1;
    } else if (strcmp(argv[i], "--qgc-host") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--qgc-host requires a value\n");
        return -1;
      }
      cfg->qgc_host = argv[i];
    } else if (strcmp(argv[i], "--qgc-port") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "--qgc-port requires a value\n");
        return -1;
      }
      cfg->qgc_port = argv[i];
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
  if (strcmp(cfg->profile, "cruise") != 0 && strcmp(cfg->profile, "takeoff") != 0 &&
      strcmp(cfg->profile, "turn") != 0 && strcmp(cfg->profile, "descent") != 0 &&
      strcmp(cfg->profile, "failsafe") != 0) {
    fprintf(stderr, "unknown profile: %s\n", cfg->profile);
    return -1;
  }
  if (strcmp(cfg->scenario, "cruise6dof") != 0 && strcmp(cfg->profile, "cruise") != 0) {
    fprintf(stderr, "--profile is only supported with --scenario cruise6dof\n");
    return -1;
  }
  if (cfg->qgc_enabled && strcmp(cfg->scenario, "cruise6dof") != 0) {
    fprintf(stderr, "--qgc is only supported with --scenario cruise6dof\n");
    return -1;
  }
  (void)cfg->seed;
  return 0;
}

static uint16_t mavlink_crc_accumulate(uint8_t data, uint16_t crc) {
  uint8_t tmp = data ^ (uint8_t)(crc & 0xffU);
  tmp ^= (uint8_t)(tmp << 4U);
  return (uint16_t)((crc >> 8U) ^ ((uint16_t)tmp << 8U) ^ ((uint16_t)tmp << 3U) ^ ((uint16_t)tmp >> 4U));
}

static void mavlink_put_u16(uint8_t *payload, size_t offset, uint16_t value) {
  payload[offset] = (uint8_t)(value & 0xffU);
  payload[offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
}

static void mavlink_put_u32(uint8_t *payload, size_t offset, uint32_t value) {
  payload[offset] = (uint8_t)(value & 0xffU);
  payload[offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
  payload[offset + 2U] = (uint8_t)((value >> 16U) & 0xffU);
  payload[offset + 3U] = (uint8_t)((value >> 24U) & 0xffU);
}

static void mavlink_put_i16(uint8_t *payload, size_t offset, int16_t value) {
  mavlink_put_u16(payload, offset, (uint16_t)value);
}

static void mavlink_put_i32(uint8_t *payload, size_t offset, int32_t value) {
  mavlink_put_u32(payload, offset, (uint32_t)value);
}

static void mavlink_put_float(uint8_t *payload, size_t offset, float value) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
  mavlink_put_u32(payload, offset, raw);
}

static int qgc_open(const sitl_config_t *cfg, qgc_link_t *link) {
  struct sockaddr_in addr;
  char *end = NULL;
  unsigned long port;

  link->fd = -1;
  link->addr_len = 0;
  link->seq = 0U;
  if (!cfg->qgc_enabled) {
    return 1;
  }

  errno = 0;
  port = strtoul(cfg->qgc_port, &end, 10);
  if (errno != 0 || end == cfg->qgc_port || *end != '\0' || port == 0UL || port > 65535UL) {
    fprintf(stderr, "invalid QGC UDP port: %s\n", cfg->qgc_port);
    return 0;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, cfg->qgc_host, &addr.sin_addr) != 1) {
    fprintf(stderr, "invalid QGC IPv4 address: %s\n", cfg->qgc_host);
    return 0;
  }

  link->fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (link->fd < 0) {
    fprintf(stderr, "failed to open QGC UDP socket\n");
    return 0;
  }
  memcpy(&link->addr, &addr, sizeof(addr));
  link->addr_len = (socklen_t)sizeof(addr);
  return 1;
}

static void qgc_close(qgc_link_t *link) {
  if (link->fd >= 0) {
    (void)close(link->fd);
    link->fd = -1;
  }
}

static void qgc_send_packet(qgc_link_t *link, uint8_t msg_id, const uint8_t *payload, uint8_t payload_len, uint8_t crc_extra) {
  uint8_t frame[6U + 255U + 2U];
  uint16_t checksum = 0xffffU;
  size_t i;

  if (link->fd < 0) {
    return;
  }
  frame[0] = 0xfeU;
  frame[1] = payload_len;
  frame[2] = link->seq++;
  frame[3] = 1U;
  frame[4] = 1U;
  frame[5] = msg_id;
  memcpy(&frame[6], payload, payload_len);

  for (i = 1U; i < 6U + payload_len; ++i) {
    checksum = mavlink_crc_accumulate(frame[i], checksum);
  }
  checksum = mavlink_crc_accumulate(crc_extra, checksum);
  frame[6U + payload_len] = (uint8_t)(checksum & 0xffU);
  frame[7U + payload_len] = (uint8_t)((checksum >> 8U) & 0xffU);

  (void)sendto(link->fd, frame, 8U + payload_len, 0, (const struct sockaddr *)&link->addr, link->addr_len);
}

static int32_t scaled_i32(real_t value, real_t scale) {
  return (int32_t)lrintf(value * scale);
}

static int16_t scaled_i16(real_t value, real_t scale) {
  return (int16_t)lrintf(value * scale);
}

static uint16_t heading_cdeg(real_t yaw_rad) {
  real_t yaw_deg = yaw_rad * (180.0f / BAYEK_PI);
  uint16_t heading;
  while (yaw_deg < 0.0f) {
    yaw_deg += 360.0f;
  }
  while (yaw_deg >= 360.0f) {
    yaw_deg -= 360.0f;
  }
  heading = (uint16_t)lrintf(yaw_deg * 100.0f);
  return heading >= 36000U ? 0U : heading;
}

static void qgc_send_heartbeat(qgc_link_t *link) {
  uint8_t payload[9] = {0};
  mavlink_put_u32(payload, 0, 0U);
  payload[4] = 1U;
  payload[5] = 0U;
  payload[6] = 0U;
  payload[7] = 4U;
  payload[8] = 3U;
  qgc_send_packet(link, 0U, payload, sizeof(payload), 50U);
}

static void qgc_send_attitude(qgc_link_t *link, uint32_t time_boot_ms, const sim_fixedwing_state_t *plant, euler_t euler) {
  uint8_t payload[28] = {0};
  mavlink_put_u32(payload, 0, time_boot_ms);
  mavlink_put_float(payload, 4, euler.roll);
  mavlink_put_float(payload, 8, euler.pitch);
  mavlink_put_float(payload, 12, euler.yaw);
  mavlink_put_float(payload, 16, plant->body.omega_body_rps.x);
  mavlink_put_float(payload, 20, plant->body.omega_body_rps.y);
  mavlink_put_float(payload, 24, plant->body.omega_body_rps.z);
  qgc_send_packet(link, 30U, payload, sizeof(payload), 39U);
}

static void qgc_send_global_position(qgc_link_t *link,
                                     uint32_t time_boot_ms,
                                     real_t lat_deg,
                                     real_t lon_deg,
                                     real_t altitude_m,
                                     const sim_fixedwing_state_t *plant,
                                     euler_t euler) {
  uint8_t payload[28] = {0};
  mavlink_put_u32(payload, 0, time_boot_ms);
  mavlink_put_i32(payload, 4, scaled_i32(lat_deg, 10000000.0f));
  mavlink_put_i32(payload, 8, scaled_i32(lon_deg, 10000000.0f));
  mavlink_put_i32(payload, 12, scaled_i32(altitude_m, 1000.0f));
  mavlink_put_i32(payload, 16, scaled_i32(altitude_m, 1000.0f));
  mavlink_put_i16(payload, 20, scaled_i16(plant->body.velocity_ned_mps.x, 100.0f));
  mavlink_put_i16(payload, 22, scaled_i16(plant->body.velocity_ned_mps.y, 100.0f));
  mavlink_put_i16(payload, 24, scaled_i16(plant->body.velocity_ned_mps.z, 100.0f));
  mavlink_put_u16(payload, 26, heading_cdeg(euler.yaw));
  qgc_send_packet(link, 33U, payload, sizeof(payload), 104U);
}

static void qgc_send_vfr_hud(qgc_link_t *link,
                             real_t airspeed_mps,
                             real_t groundspeed_mps,
                             real_t altitude_m,
                             real_t climb_mps,
                             euler_t euler) {
  uint8_t payload[20] = {0};
  mavlink_put_float(payload, 0, airspeed_mps);
  mavlink_put_float(payload, 4, groundspeed_mps);
  mavlink_put_i16(payload, 8, (int16_t)lrintf((real_t)heading_cdeg(euler.yaw) / 100.0f));
  mavlink_put_u16(payload, 10, 0U);
  mavlink_put_float(payload, 12, altitude_m);
  mavlink_put_float(payload, 16, climb_mps);
  qgc_send_packet(link, 74U, payload, sizeof(payload), 20U);
}

static void qgc_send_state(qgc_link_t *link,
                           const sitl_config_t *cfg,
                           int step,
                           const sim_fixedwing_state_t *plant,
                           real_t lat_deg,
                           real_t lon_deg,
                           real_t altitude_m,
                           euler_t euler) {
  uint32_t time_boot_ms;
  real_t groundspeed_mps;

  if (link->fd < 0) {
    return;
  }
  time_boot_ms = (uint32_t)lrint((double)step * cfg->dt_s * 1000.0);
  groundspeed_mps = (real_t)sqrtf(plant->body.velocity_ned_mps.x * plant->body.velocity_ned_mps.x +
                                  plant->body.velocity_ned_mps.y * plant->body.velocity_ned_mps.y);
  if (step == 0 || fmod((double)step * cfg->dt_s, 1.0) < cfg->dt_s) {
    qgc_send_heartbeat(link);
  }
  qgc_send_attitude(link, time_boot_ms, plant, euler);
  qgc_send_global_position(link, time_boot_ms, lat_deg, lon_deg, altitude_m, plant, euler);
  qgc_send_vfr_hud(link, plant->last_airspeed_mps, groundspeed_mps, altitude_m, -plant->body.velocity_ned_mps.z, euler);
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

static void apply_initial_conditions(sim_fixedwing_state_t *plant,
                                     const sim6dof_params_t *params,
                                     const sitl_initial_conditions_t *initial) {
  euler_t attitude;
  if (plant == NULL || params == NULL || initial == NULL) {
    return;
  }
  sim6dof_set_origin(&plant->body, initial->lat_deg, initial->lon_deg, initial->altitude_m, params->earth_radius_m);
  attitude.roll = initial->roll_rad;
  attitude.pitch = initial->pitch_rad;
  attitude.yaw = initial->yaw_rad;
  plant->body.attitude_body_to_ned = quat_from_euler(attitude);
  plant->body.position_ned_m.x = 0.0f;
  plant->body.position_ned_m.y = 0.0f;
  plant->body.position_ned_m.z = 0.0f;
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
  sim6dof_sync_ecef_from_ned(&plant->body, params->earth_radius_m);
}

static void truth_geo_from_state(const sim6dof_state_t *state,
                                 real_t earth_radius_m,
                                 real_t *lat_deg,
                                 real_t *lon_deg,
                                 real_t *altitude_m) {
  sim6dof_ecef_to_geodetic(state->position_ecef_m, earth_radius_m, lat_deg, lon_deg, altitude_m);
}

static rc_input_t cruise6dof_profile_rc(const sitl_config_t *cfg, const sitl_initial_conditions_t *initial, int step, int steps) {
  rc_input_t rc = initial->rc;
  real_t progress = steps > 1 ? (real_t)step / (real_t)(steps - 1) : 0.0f;

  if (strcmp(cfg->profile, "takeoff") == 0) {
    rc.throttle = 0.82f;
    rc.roll = 0.0f;
    rc.pitch = progress < 0.70f ? 0.08f : 0.03f;
    rc.yaw = 0.0f;
  } else if (strcmp(cfg->profile, "turn") == 0) {
    rc.throttle = initial->rc.throttle;
    rc.pitch = initial->rc.pitch;
    rc.yaw = 0.0f;
    if (progress < 0.20f) {
      rc.roll = 0.0f;
    } else if (progress < 0.75f) {
      rc.roll = 0.30f;
    } else {
      rc.roll = -0.10f;
    }
  } else if (strcmp(cfg->profile, "descent") == 0) {
    rc.throttle = progress < 0.25f ? initial->rc.throttle : 0.38f;
    rc.roll = 0.0f;
    rc.pitch = progress < 0.25f ? initial->rc.pitch : -0.10f;
    rc.yaw = 0.0f;
  } else if (strcmp(cfg->profile, "failsafe") == 0) {
    if (progress >= 0.50f) {
      rc.throttle = 0.0f;
      rc.roll = 0.0f;
      rc.pitch = 0.0f;
      rc.yaw = 0.0f;
      rc.arm_switch = 1U;
      rc.mode_switch = 1U;
    }
  }
  return rc;
}

static int run_cruise6dof(const sitl_config_t *cfg, int steps, FILE *csv) {
  sim_fixedwing_params_t params;
  sim_fixedwing_state_t plant;
  fsw_input_t input;
  fsw_output_t output;
  sitl_initial_conditions_t initial;
  qgc_link_t qgc;
  char initial_error[160];
  char model_error[160];
  int i;
  double start_wall_s;

  bayek_fsw_init(altair_vehicle_interface());
  altair_fixedwing_sim_params(&params);
  params.core.frame_mode = cfg->frame_mode;
  if (!altair_fixedwing_sim_params_are_valid(&params, altair_default_params(), model_error, sizeof(model_error))) {
    fprintf(stderr, "invalid fixed-wing sim model: %s\n", model_error);
    return 1;
  }
  sim_fixedwing_init_default(&plant);
  sitl_initial_conditions_default(&initial);
  if (cfg->initial_path != NULL) {
    if (!sitl_initial_conditions_load(cfg->initial_path, &initial, initial_error, sizeof(initial_error))) {
      fprintf(stderr, "failed to load initial conditions: %s\n", initial_error);
      return 1;
    }
  }
  apply_initial_conditions(&plant, &params.core, &initial);
  if (!qgc_open(cfg, &qgc)) {
    return 1;
  }
  start_wall_s = wall_time_s();

  if (fprintf(csv,
              "step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,"
              "gps_fix_valid,lat_deg,lon_deg,pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,"
              "roll_rad,pitch_rad,yaw_rad,quat_w,quat_x,quat_y,quat_z,p_rps,q_rps,r_rps,"
              "airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2,"
              "force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm,"
              "pos_ecef_x_m,pos_ecef_y_m,pos_ecef_z_m,vel_ecef_x_mps,vel_ecef_y_mps,vel_ecef_z_mps\n") < 0) {
    fprintf(stderr, "failed to write output\n");
    qgc_close(&qgc);
    return 1;
  }

  for (i = 0; i < steps; ++i) {
    euler_t euler;
    real_t lat_deg;
    real_t lon_deg;
    real_t altitude_m;
    rc_input_t rc = cruise6dof_profile_rc(cfg, &initial, i, steps);
    uint32_t gps_fix_valid = 1U;
    sim_fixedwing_make_fsw_input(&plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
    truth_geo_from_state(&plant.body, params.core.earth_radius_m, &lat_deg, &lon_deg, &altitude_m);
    input.gps.lat_deg = lat_deg;
    input.gps.lon_deg = lon_deg;
    input.gps.alt_m = altitude_m;
    input.baro.altitude_m = altitude_m;
    if (strcmp(cfg->profile, "failsafe") == 0 && i * 2 >= steps) {
      gps_fix_valid = 0U;
    }
    input.gps.fix_valid = gps_fix_valid;
    bayek_fsw_step(&input, &output);
    if (!sim_output_is_bounded(&output)) {
      fprintf(stderr, "unbounded_output at step %d\n", i);
      qgc_close(&qgc);
      return 2;
    }
    if (!sim_fixedwing_step(&plant, &params, &output.actuators, (real_t)cfg->dt_s)) {
      fprintf(stderr, "invalid_sim_state at step %d\n", i);
      qgc_close(&qgc);
      return 2;
    }
    euler = euler_from_quat(plant.body.attitude_body_to_ned);
    truth_geo_from_state(&plant.body, params.core.earth_radius_m, &lat_deg, &lon_deg, &altitude_m);
    qgc_send_state(&qgc, cfg, i, &plant, lat_deg, lon_deg, altitude_m, euler);
    if (fprintf(csv,
                "%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%u,%.8f,%.8f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.8f,%.8f,%.8f,%.8f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
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
                gps_fix_valid,
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
                (double)plant.last_moment_body_nm.z,
                (double)plant.body.position_ecef_m.x,
                (double)plant.body.position_ecef_m.y,
                (double)plant.body.position_ecef_m.z,
                (double)plant.body.velocity_ecef_mps.x,
                (double)plant.body.velocity_ecef_mps.y,
                (double)plant.body.velocity_ecef_mps.z) < 0) {
      fprintf(stderr, "failed to write output\n");
      qgc_close(&qgc);
      return 1;
    }
    pace_realtime_step(cfg, start_wall_s, i + 1);
  }
  qgc_close(&qgc);
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
