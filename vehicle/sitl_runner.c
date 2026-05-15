#include "altair_vehicle.h"
#include "altair_sim_params.h"
#include "altair_sim_model.h"
#include "altair_fsw.h"
#include "math_utils.h"
#include "sim_plant.h"
#include "sitl_case.h"
#include "sitl_conditions.h"
#include "sitl_initial_conditions.h"
#include "sitl_trim.h"

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

typedef struct
{
    const char *scenario;
    const char *profile;
    double duration_s;
    double dt_s;
    uint32_t seed;
    const char *output_path;
    const char *initial_path;
    const char *case_path;
    const char *condition_path;
    const sitl_case_t *case_file;
    const sitl_conditions_t *conditions;
    int frame_mode;
    int realtime;
    int qgc_enabled;
    const char *qgc_host;
    const char *qgc_port;
    uint32_t mavlink_system_id;
    uint32_t mavlink_source_port;
} sitl_config_t;

typedef struct
{
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    uint8_t seq;
} qgc_link_t;

static void qgc_close(qgc_link_t *link);

static void print_usage(FILE *stream)
{
    fprintf(
        stream,
        "usage: sitl_runner [--scenario smoke|cruise6dof] [--duration seconds] [--dt seconds]\n"
        "                   [--seed uint] [--output path] [--initial path] [--case path]\n"
        "                   [--conditions path]\n"
        "                   [--frame-mode ned|ecef]\n"
        "                   [--profile cruise|takeoff|turn|descent|failsafe|mission] [--realtime]\n"
        "                   [--mavlink] [--mavlink-host host] [--mavlink-port port]\n"
        "                   [--mavlink-system-id id] [--mavlink-source-port port]\n"
        "                   [--qgc] [--qgc-host host] [--qgc-port port]\n");
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

static int parse_args(int argc, char **argv, sitl_config_t *cfg)
{
    int i;

    cfg->scenario = "smoke";
    cfg->profile = "cruise";
    cfg->duration_s = 10.0;
    cfg->dt_s = 0.01;
    cfg->seed = 1U;
    cfg->output_path = NULL;
    cfg->initial_path = NULL;
    cfg->case_path = NULL;
    cfg->condition_path = NULL;
    cfg->case_file = NULL;
    cfg->conditions = NULL;
    cfg->frame_mode = SIM6DOF_FRAME_ECEF;
    cfg->realtime = 0;
    cfg->qgc_enabled = 0;
    cfg->qgc_host = "127.0.0.1";
    cfg->qgc_port = "14550";
    cfg->mavlink_system_id = 1U;
    cfg->mavlink_source_port = 0U;

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
        else if (strcmp(argv[i], "--profile") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "--profile requires a value\n");
                return -1;
            }
            cfg->profile = argv[i];
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
        else if (strcmp(argv[i], "--seed") == 0)
        {
            if (++i >= argc || !parse_uint_arg(argv[i], "seed", &cfg->seed))
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
        else if (strcmp(argv[i], "--initial") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "--initial requires a value\n");
                return -1;
            }
            cfg->initial_path = argv[i];
        }
        else if (strcmp(argv[i], "--case") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "--case requires a value\n");
                return -1;
            }
            cfg->case_path = argv[i];
        }
        else if (strcmp(argv[i], "--conditions") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "--conditions requires a value\n");
                return -1;
            }
            cfg->condition_path = argv[i];
        }
        else if (strcmp(argv[i], "--frame-mode") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "--frame-mode requires a value\n");
                return -1;
            }
            if (strcmp(argv[i], "ned") == 0)
            {
                cfg->frame_mode = SIM6DOF_FRAME_NED;
            }
            else if (strcmp(argv[i], "ecef") == 0)
            {
                cfg->frame_mode = SIM6DOF_FRAME_ECEF;
            }
            else
            {
                fprintf(stderr, "invalid --frame-mode: %s\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "--realtime") == 0)
        {
            cfg->realtime = 1;
        }
        else if (strcmp(argv[i], "--qgc") == 0 || strcmp(argv[i], "--mavlink") == 0)
        {
            cfg->qgc_enabled = 1;
            cfg->realtime = 1;
        }
        else if (strcmp(argv[i], "--qgc-host") == 0 || strcmp(argv[i], "--mavlink-host") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "%s requires a value\n", argv[i - 1]);
                return -1;
            }
            cfg->qgc_host = argv[i];
        }
        else if (strcmp(argv[i], "--qgc-port") == 0 || strcmp(argv[i], "--mavlink-port") == 0)
        {
            if (++i >= argc)
            {
                fprintf(stderr, "%s requires a value\n", argv[i - 1]);
                return -1;
            }
            cfg->qgc_port = argv[i];
        }
        else if (strcmp(argv[i], "--mavlink-system-id") == 0)
        {
            if (++i >= argc ||
                !parse_uint_arg(argv[i], "mavlink system id", &cfg->mavlink_system_id) ||
                cfg->mavlink_system_id == 0U || cfg->mavlink_system_id > 255U)
            {
                return -1;
            }
        }
        else if (strcmp(argv[i], "--mavlink-source-port") == 0)
        {
            if (++i >= argc ||
                !parse_uint_arg(argv[i], "mavlink source port", &cfg->mavlink_source_port) ||
                cfg->mavlink_source_port > 65535U)
            {
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    (void)cfg->seed;
    return 0;
}

static void apply_case_run_config(sitl_config_t *cfg, const sitl_case_t *case_file)
{
    if (cfg == NULL || case_file == NULL)
    {
        return;
    }
    if (case_file->run.has_scenario)
    {
        cfg->scenario = case_file->run.scenario;
    }
    if (case_file->run.has_profile)
    {
        cfg->profile = case_file->run.profile;
    }
    if (case_file->run.has_duration_s)
    {
        cfg->duration_s = case_file->run.duration_s;
    }
    if (case_file->run.has_dt_s)
    {
        cfg->dt_s = case_file->run.dt_s;
    }
    if (case_file->run.has_seed)
    {
        cfg->seed = case_file->run.seed;
    }
    if (case_file->run.has_frame_mode)
    {
        cfg->frame_mode = case_file->run.frame_mode;
    }
    if (case_file->run.has_condition_file && cfg->condition_path == NULL)
    {
        cfg->condition_path = case_file->run.condition_file;
    }
    cfg->case_file = case_file;
}

static int validate_config(const sitl_config_t *cfg)
{
    if (strcmp(cfg->scenario, "smoke") != 0 && strcmp(cfg->scenario, "cruise6dof") != 0)
    {
        fprintf(stderr, "unknown scenario: %s\n", cfg->scenario);
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
    if (cfg->initial_path != NULL && strcmp(cfg->scenario, "cruise6dof") != 0)
    {
        fprintf(stderr, "--initial is only supported with --scenario cruise6dof\n");
        return -1;
    }
    if (cfg->case_path != NULL && strcmp(cfg->scenario, "cruise6dof") != 0)
    {
        fprintf(stderr, "--case is only supported with --scenario cruise6dof\n");
        return -1;
    }
    if (cfg->condition_path != NULL && strcmp(cfg->scenario, "cruise6dof") != 0)
    {
        fprintf(stderr, "--conditions is only supported with --scenario cruise6dof\n");
        return -1;
    }
    if (strcmp(cfg->profile, "cruise") != 0 && strcmp(cfg->profile, "takeoff") != 0 &&
        strcmp(cfg->profile, "turn") != 0 && strcmp(cfg->profile, "descent") != 0 &&
        strcmp(cfg->profile, "failsafe") != 0 && strcmp(cfg->profile, "mission") != 0)
    {
        fprintf(stderr, "unknown profile: %s\n", cfg->profile);
        return -1;
    }
    if (strcmp(cfg->scenario, "cruise6dof") != 0 && strcmp(cfg->profile, "cruise") != 0)
    {
        fprintf(stderr, "--profile is only supported with --scenario cruise6dof\n");
        return -1;
    }
    if (cfg->qgc_enabled && strcmp(cfg->scenario, "cruise6dof") != 0)
    {
        fprintf(stderr, "--mavlink/--qgc is only supported with --scenario cruise6dof\n");
        return -1;
    }
    return 0;
}

static uint16_t mavlink_crc_accumulate(uint8_t data, uint16_t crc)
{
    uint8_t tmp = data ^ (uint8_t)(crc & 0xffU);
    tmp ^= (uint8_t)(tmp << 4U);
    return (uint16_t)((crc >> 8U) ^ ((uint16_t)tmp << 8U) ^ ((uint16_t)tmp << 3U) ^
                      ((uint16_t)tmp >> 4U));
}

static void mavlink_put_u16(uint8_t *payload, size_t offset, uint16_t value)
{
    payload[offset] = (uint8_t)(value & 0xffU);
    payload[offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
}

static void mavlink_put_u32(uint8_t *payload, size_t offset, uint32_t value)
{
    payload[offset] = (uint8_t)(value & 0xffU);
    payload[offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
    payload[offset + 2U] = (uint8_t)((value >> 16U) & 0xffU);
    payload[offset + 3U] = (uint8_t)((value >> 24U) & 0xffU);
}

static void mavlink_put_i16(uint8_t *payload, size_t offset, int16_t value)
{
    mavlink_put_u16(payload, offset, (uint16_t)value);
}

static void mavlink_put_i32(uint8_t *payload, size_t offset, int32_t value)
{
    mavlink_put_u32(payload, offset, (uint32_t)value);
}

static void mavlink_put_float(uint8_t *payload, size_t offset, float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    mavlink_put_u32(payload, offset, raw);
}

static int qgc_open(const sitl_config_t *cfg, qgc_link_t *link)
{
    struct sockaddr_in addr;
    struct sockaddr_in source_addr;
    char *end = NULL;
    unsigned long port;

    link->fd = -1;
    link->addr_len = 0;
    link->seq = 0U;
    if (!cfg->qgc_enabled)
    {
        return 1;
    }

    errno = 0;
    port = strtoul(cfg->qgc_port, &end, 10);
    if (errno != 0 || end == cfg->qgc_port || *end != '\0' || port == 0UL || port > 65535UL)
    {
        fprintf(stderr, "invalid QGC UDP port: %s\n", cfg->qgc_port);
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, cfg->qgc_host, &addr.sin_addr) != 1)
    {
        fprintf(stderr, "invalid QGC IPv4 address: %s\n", cfg->qgc_host);
        return 0;
    }

    link->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (link->fd < 0)
    {
        fprintf(stderr, "failed to open QGC UDP socket\n");
        return 0;
    }
    if (cfg->mavlink_source_port > 0U)
    {
        memset(&source_addr, 0, sizeof(source_addr));
        source_addr.sin_family = AF_INET;
        source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        source_addr.sin_port = htons((uint16_t)cfg->mavlink_source_port);
        if (bind(link->fd, (const struct sockaddr *)&source_addr, sizeof(source_addr)) != 0)
        {
            fprintf(stderr,
                    "failed to bind MAVLink source port %u\n",
                    (unsigned)cfg->mavlink_source_port);
            qgc_close(link);
            return 0;
        }
    }
    memcpy(&link->addr, &addr, sizeof(addr));
    link->addr_len = (socklen_t)sizeof(addr);
    return 1;
}

static void qgc_close(qgc_link_t *link)
{
    if (link->fd >= 0)
    {
        (void)close(link->fd);
        link->fd = -1;
    }
}

static void qgc_send_packet(qgc_link_t *link,
                            const sitl_config_t *cfg,
                            uint8_t msg_id,
                            const uint8_t *payload,
                            uint8_t payload_len,
                            uint8_t crc_extra)
{
    uint8_t frame[6U + 255U + 2U];
    uint16_t checksum = 0xffffU;
    size_t i;

    if (link->fd < 0)
    {
        return;
    }
    frame[0] = 0xfeU;
    frame[1] = payload_len;
    frame[2] = link->seq++;
    frame[3] = (uint8_t)cfg->mavlink_system_id;
    frame[4] = 1U;
    frame[5] = msg_id;
    memcpy(&frame[6], payload, payload_len);

    for (i = 1U; i < 6U + payload_len; ++i)
    {
        checksum = mavlink_crc_accumulate(frame[i], checksum);
    }
    checksum = mavlink_crc_accumulate(crc_extra, checksum);
    frame[6U + payload_len] = (uint8_t)(checksum & 0xffU);
    frame[7U + payload_len] = (uint8_t)((checksum >> 8U) & 0xffU);

    (void)sendto(
        link->fd, frame, 8U + payload_len, 0, (const struct sockaddr *)&link->addr, link->addr_len);
}

static int32_t scaled_i32(real_t value, real_t scale)
{
    return (int32_t)lrintf(value * scale);
}

static int16_t scaled_i16(real_t value, real_t scale)
{
    return (int16_t)lrintf(value * scale);
}

static uint16_t heading_cdeg(real_t yaw_rad)
{
    real_t yaw_deg = yaw_rad * (180.0f / BAYEK_PI);
    uint16_t heading;
    while (yaw_deg < 0.0f)
    {
        yaw_deg += 360.0f;
    }
    while (yaw_deg >= 360.0f)
    {
        yaw_deg -= 360.0f;
    }
    heading = (uint16_t)lrintf(yaw_deg * 100.0f);
    return heading >= 36000U ? 0U : heading;
}

static void qgc_send_heartbeat(qgc_link_t *link, const sitl_config_t *cfg)
{
    uint8_t payload[9] = {0};
    mavlink_put_u32(payload, 0, 0U);
    payload[4] = 1U;
    payload[5] = 0U;
    payload[6] = 0U;
    payload[7] = 4U;
    payload[8] = 3U;
    qgc_send_packet(link, cfg, 0U, payload, sizeof(payload), 50U);
}

static void qgc_send_attitude(qgc_link_t *link,
                              const sitl_config_t *cfg,
                              uint32_t time_boot_ms,
                              const sim_fixedwing_state_t *plant,
                              euler_t euler)
{
    uint8_t payload[28] = {0};
    mavlink_put_u32(payload, 0, time_boot_ms);
    mavlink_put_float(payload, 4, euler.roll);
    mavlink_put_float(payload, 8, euler.pitch);
    mavlink_put_float(payload, 12, euler.yaw);
    mavlink_put_float(payload, 16, plant->body.omega_body_rps.x);
    mavlink_put_float(payload, 20, plant->body.omega_body_rps.y);
    mavlink_put_float(payload, 24, plant->body.omega_body_rps.z);
    qgc_send_packet(link, cfg, 30U, payload, sizeof(payload), 39U);
}

static void qgc_send_global_position(qgc_link_t *link,
                                     const sitl_config_t *cfg,
                                     uint32_t time_boot_ms,
                                     real_t lat_deg,
                                     real_t lon_deg,
                                     real_t altitude_m,
                                     const sim_fixedwing_state_t *plant,
                                     euler_t euler)
{
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
    qgc_send_packet(link, cfg, 33U, payload, sizeof(payload), 104U);
}

static void qgc_send_vfr_hud(qgc_link_t *link,
                             const sitl_config_t *cfg,
                             real_t airspeed_mps,
                             real_t groundspeed_mps,
                             real_t altitude_m,
                             real_t climb_mps,
                             euler_t euler)
{
    uint8_t payload[20] = {0};
    mavlink_put_float(payload, 0, airspeed_mps);
    mavlink_put_float(payload, 4, groundspeed_mps);
    mavlink_put_i16(payload, 8, (int16_t)lrintf((real_t)heading_cdeg(euler.yaw) / 100.0f));
    mavlink_put_u16(payload, 10, 0U);
    mavlink_put_float(payload, 12, altitude_m);
    mavlink_put_float(payload, 16, climb_mps);
    qgc_send_packet(link, cfg, 74U, payload, sizeof(payload), 20U);
}

static void qgc_send_state(qgc_link_t *link,
                           const sitl_config_t *cfg,
                           int step,
                           const sim_fixedwing_state_t *plant,
                           real_t lat_deg,
                           real_t lon_deg,
                           real_t altitude_m,
                           euler_t euler)
{
    uint32_t time_boot_ms;
    real_t groundspeed_mps;

    if (link->fd < 0)
    {
        return;
    }
    time_boot_ms = (uint32_t)lrint((double)step * cfg->dt_s * 1000.0);
    groundspeed_mps =
        (real_t)sqrtf(plant->body.velocity_ned_mps.x * plant->body.velocity_ned_mps.x +
                      plant->body.velocity_ned_mps.y * plant->body.velocity_ned_mps.y);
    if (step == 0 || fmod((double)step * cfg->dt_s, 1.0) < cfg->dt_s)
    {
        qgc_send_heartbeat(link, cfg);
    }
    qgc_send_attitude(link, cfg, time_boot_ms, plant, euler);
    qgc_send_global_position(link, cfg, time_boot_ms, lat_deg, lon_deg, altitude_m, plant, euler);
    qgc_send_vfr_hud(link,
                     cfg,
                     plant->last_airspeed_mps,
                     groundspeed_mps,
                     altitude_m,
                     -plant->body.velocity_ned_mps.z,
                     euler);
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

static double wall_time_s(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0)
    {
        return 0.0;
    }
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void sleep_until_wall_s(double target_s)
{
    double remaining_s = target_s - wall_time_s();
    struct timeval timeout;
    long usec;
    if (remaining_s <= 0.0)
    {
        return;
    }
    if (remaining_s > 1.0)
    {
        remaining_s = 1.0;
    }
    usec = (long)(remaining_s * 1000000.0);
    timeout.tv_sec = usec / 1000000L;
    timeout.tv_usec = usec % 1000000L;
    (void)select(0, NULL, NULL, NULL, &timeout);
}

static void pace_realtime_step(const sitl_config_t *cfg, double start_wall_s, int completed_steps)
{
    if (!cfg->realtime)
    {
        return;
    }
    sleep_until_wall_s(start_wall_s + (double)completed_steps * cfg->dt_s);
}

static int run_smoke(const sitl_config_t *cfg, int steps, FILE *csv)
{
    sim_plant_t plant;
    fsw_input_t input;
    fsw_output_t output;
    altair_fsw_t fsw;
    rc_input_t rc = {0.55f, 0.10f, 0.02f, 0.0f, 1U, 1U};
    int i;
    double start_wall_s;

    altair_fsw_init(&fsw, altair_vehicle_interface());
    sim_plant_init(&plant);
    start_wall_s = wall_time_s();

    if (fprintf(csv, "step,time_s,mode,motor,aileron,elevator,rudder,airspeed_mps,altitude_m\n") <
        0)
    {
        fprintf(stderr, "failed to write output\n");
        if (csv != stdout)
        {
            (void)fclose(csv);
        }
        return 1;
    }
    for (i = 0; i < steps; ++i)
    {
        sim_make_fsw_input(
            &plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
        altair_fsw_step(&fsw, &input, &output);
        if (!sim_output_is_bounded(&output))
        {
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
                    (double)plant.altitude_m) < 0)
        {
            fprintf(stderr, "failed to write output\n");
            return 1;
        }
        pace_realtime_step(cfg, start_wall_s, i + 1);
    }
    return 0;
}

static void apply_initial_conditions(sim_fixedwing_state_t *plant,
                                     const sim6dof_params_t *params,
                                     const sitl_initial_conditions_t *initial)
{
    euler_t attitude;
    if (plant == NULL || params == NULL || initial == NULL)
    {
        return;
    }
    sim6dof_set_origin(&plant->body,
                       initial->lat_deg,
                       initial->lon_deg,
                       initial->altitude_m,
                       params->earth_radius_m);
    attitude.roll = initial->roll_rad;
    attitude.pitch = initial->pitch_rad;
    attitude.yaw = initial->yaw_rad;
    plant->body.attitude_body_to_ned = quat_from_euler(attitude);
    plant->body.position_ned_m.x = 0.0f;
    plant->body.position_ned_m.y = 0.0f;
    plant->body.position_ned_m.z = 0.0f;
    if (initial->has_velocity_ned)
    {
        plant->body.velocity_ned_mps.x = initial->vel_n_mps;
        plant->body.velocity_ned_mps.y = initial->vel_e_mps;
        plant->body.velocity_ned_mps.z = initial->vel_d_mps;
    }
    else
    {
        vec3_t forward_body = {initial->airspeed_mps, 0.0f, 0.0f};
        plant->body.velocity_ned_mps =
            quat_rotate_vec3(plant->body.attitude_body_to_ned, forward_body);
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
                                 real_t *altitude_m)
{
    sim6dof_ecef_to_geodetic(state->position_ecef_m, earth_radius_m, lat_deg, lon_deg, altitude_m);
}

static rc_input_t cruise6dof_profile_rc(const sitl_config_t *cfg,
                                        const sitl_initial_conditions_t *initial,
                                        int step,
                                        int steps)
{
    rc_input_t rc = initial->rc;
    real_t progress = steps > 1 ? (real_t)step / (real_t)(steps - 1) : 0.0f;

    if (strcmp(cfg->profile, "takeoff") == 0)
    {
        rc.throttle = 0.82f;
        rc.roll = 0.0f;
        rc.pitch = progress < 0.70f ? 0.08f : 0.03f;
        rc.yaw = 0.0f;
    }
    else if (strcmp(cfg->profile, "turn") == 0)
    {
        rc.throttle = initial->rc.throttle;
        rc.pitch = initial->rc.pitch;
        rc.yaw = 0.0f;
        if (progress < 0.20f)
        {
            rc.roll = 0.0f;
        }
        else if (progress < 0.75f)
        {
            rc.roll = 0.30f;
        }
        else
        {
            rc.roll = -0.10f;
        }
    }
    else if (strcmp(cfg->profile, "descent") == 0)
    {
        rc.throttle = progress < 0.25f ? initial->rc.throttle : 0.38f;
        rc.roll = 0.0f;
        rc.pitch = progress < 0.25f ? initial->rc.pitch : -0.10f;
        rc.yaw = 0.0f;
    }
    else if (strcmp(cfg->profile, "failsafe") == 0)
    {
        if (progress >= 0.50f)
        {
            rc.throttle = 0.0f;
            rc.roll = 0.0f;
            rc.pitch = 0.0f;
            rc.yaw = 0.0f;
            rc.arm_switch = 1U;
            rc.mode_switch = 1U;
        }
    }
    else if (strcmp(cfg->profile, "mission") == 0)
    {
        rc.throttle = 0.0f;
        rc.roll = 0.0f;
        rc.pitch = 0.0f;
        rc.yaw = 0.0f;
        rc.arm_switch = 1U;
        rc.mode_switch = 2U;
    }
    return rc;
}

static bayek_mission_plan_t make_cruise6dof_mission(const sitl_initial_conditions_t *initial)
{
    bayek_mission_plan_t mission = {0};
    const real_t throttle = 0.62f;

    mission.waypoint_count = 3U;
    mission.waypoints[0].lat_deg = initial->lat_deg;
    mission.waypoints[0].lon_deg = initial->lon_deg;
    mission.waypoints[0].alt_m = initial->altitude_m;
    mission.waypoints[0].throttle = throttle;
    mission.waypoints[0].acceptance_radius_m = 100.0f;
    mission.waypoints[1].lat_deg = initial->lat_deg + 0.00030f;
    mission.waypoints[1].lon_deg = initial->lon_deg;
    mission.waypoints[1].alt_m = initial->altitude_m;
    mission.waypoints[1].throttle = throttle;
    mission.waypoints[1].acceptance_radius_m = 20.0f;
    mission.waypoints[2].lat_deg = initial->lat_deg + 0.00100f;
    mission.waypoints[2].lon_deg = initial->lon_deg;
    mission.waypoints[2].alt_m = initial->altitude_m + 80.0f;
    mission.waypoints[2].throttle = throttle;
    mission.waypoints[2].acceptance_radius_m = 25.0f;
    return mission;
}

static int run_cruise6dof(const sitl_config_t *cfg, int steps, FILE *csv)
{
    sim_fixedwing_params_t params;
    altair_sim_param_store_t sim_param_store;
    sim_fixedwing_state_t plant;
    fsw_input_t input;
    fsw_output_t output;
    altair_fsw_t fsw;
    sitl_initial_conditions_t initial;
    vehicle_params_t vehicle_params;
    altair_vehicle_param_store_t vehicle_param_store;
    bayek_mission_plan_t mission = {0};
    uint8_t mission_enabled = 0U;
    sitl_trim_config_t trim_config;
    sitl_trim_status_t trim_status;
    qgc_link_t qgc;
    char initial_error[160];
    char model_error[160];
    char condition_error[200];
    char trim_error[200];
    int i;
    double start_wall_s;

    vehicle_params = *altair_default_params();
    sitl_trim_config_default(&trim_config);
    sitl_trim_status_default(&trim_status);
    if (cfg->case_file != NULL && cfg->case_file->has_vehicle_params)
    {
        vehicle_params = cfg->case_file->vehicle_params;
    }
    altair_vehicle_param_store_init(&vehicle_param_store, &vehicle_params);
    altair_fsw_init(&fsw, altair_vehicle_interface_with_params(&vehicle_param_store.active));
    altair_fixedwing_sim_params(&params);
    if (cfg->case_file != NULL && cfg->case_file->has_sim_params)
    {
        params = cfg->case_file->sim_params;
    }
    params.core.frame_mode = cfg->frame_mode;
    altair_sim_param_store_init(&sim_param_store, &params);
    if (!altair_fixedwing_sim_params_are_valid(
            &sim_param_store.active, &vehicle_param_store.active, model_error, sizeof(model_error)))
    {
        fprintf(stderr, "invalid fixed-wing sim model: %s\n", model_error);
        return 1;
    }
    sim_fixedwing_init_default(&plant);
    sitl_initial_conditions_default(&initial);
    if (cfg->initial_path != NULL)
    {
        if (!sitl_initial_conditions_load(
                cfg->initial_path, &initial, initial_error, sizeof(initial_error)))
        {
            fprintf(stderr, "failed to load initial conditions: %s\n", initial_error);
            return 1;
        }
    }
    if (cfg->case_file != NULL && cfg->case_file->has_initial)
    {
        initial = cfg->case_file->initial;
    }
    apply_initial_conditions(&plant, &sim_param_store.active.core, &initial);
    if (!qgc_open(cfg, &qgc))
    {
        return 1;
    }
    if (cfg->case_file != NULL && cfg->case_file->has_mission)
    {
        mission = cfg->case_file->mission;
        mission_enabled = cfg->case_file->mission_enabled;
        if (cfg->case_file->mission_enabled && cfg->case_file->mission.waypoint_count > 0U)
        {
            altair_fsw_set_mission(&fsw, &mission);
        }
        else
        {
            altair_fsw_clear_mission(&fsw);
        }
    }
    else if (strcmp(cfg->profile, "mission") == 0)
    {
        mission_enabled = 1U;
        mission = make_cruise6dof_mission(&initial);
        altair_fsw_set_mission(&fsw, &mission);
    }
    else
    {
        altair_fsw_clear_mission(&fsw);
    }
    start_wall_s = wall_time_s();

    if (fprintf(
            csv,
            "step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,"
            "gps_fix_valid,lat_deg,lon_deg,pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,"
            "roll_rad,pitch_rad,yaw_rad,quat_w,quat_x,quat_y,quat_z,p_rps,q_rps,r_rps,"
            "airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2,"
            "force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm,"
            "pos_ecef_x_m,pos_ecef_y_m,pos_ecef_z_m,vel_ecef_x_mps,vel_ecef_y_mps,vel_ecef_z_"
            "mps,trim_active,trim_achieved,trim_failed,trim_iteration_count,trim_residual_norm,"
            "mission_loaded,mission_active_wp,mission_wp_count,mission_distance_m\n") < 0)
    {
        fprintf(stderr, "failed to write output\n");
        qgc_close(&qgc);
        return 1;
    }

    for (i = 0; i < steps; ++i)
    {
        euler_t euler;
        real_t lat_deg;
        real_t lon_deg;
        real_t altitude_m;
        bayek_mission_status_t mission_status;
        rc_input_t rc = cruise6dof_profile_rc(cfg, &initial, i, steps);
        uint32_t gps_fix_valid = 1U;
        sim_fixedwing_make_fsw_input(
            &plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
        truth_geo_from_state(&plant.body,
                             sim_param_store.active.core.earth_radius_m,
                             &lat_deg,
                             &lon_deg,
                             &altitude_m);
        input.gps.lat_deg = lat_deg;
        input.gps.lon_deg = lon_deg;
        input.gps.alt_m = altitude_m;
        input.baro.altitude_m = altitude_m;
        if (strcmp(cfg->profile, "failsafe") == 0 && i * 2 >= steps)
        {
            gps_fix_valid = 0U;
        }
        input.gps.fix_valid = gps_fix_valid;
        if (cfg->conditions != NULL)
        {
            sitl_condition_context_t condition_ctx;
            memset(&condition_ctx, 0, sizeof(condition_ctx));
            condition_ctx.t_s = (double)i * cfg->dt_s;
            condition_ctx.step = (uint32_t)i;
            condition_ctx.rc = &rc;
            condition_ctx.input = &input;
            altair_vehicle_param_store_begin(&vehicle_param_store);
            altair_sim_param_store_begin(&sim_param_store);
            condition_ctx.vehicle_params = &vehicle_param_store.staged;
            condition_ctx.sim_params = &sim_param_store.staged;
            condition_ctx.plant = &plant;
            condition_ctx.trim = &trim_config;
            condition_ctx.mission_enabled = &mission_enabled;
            condition_ctx.mission = &mission;
            if (!sitl_conditions_eval(
                    cfg->conditions, &condition_ctx, condition_error, sizeof(condition_error)))
            {
                fprintf(
                    stderr, "failed to evaluate conditions at step %d: %s\n", i, condition_error);
                qgc_close(&qgc);
                return 1;
            }
            rc = input.rc;
            gps_fix_valid = input.gps.fix_valid;
            if (condition_ctx.plant_ecef_dirty)
            {
                sim6dof_sync_ned_from_ecef(&plant.body, sim_param_store.active.core.earth_radius_m);
            }
            if (condition_ctx.plant_ned_dirty)
            {
                sim6dof_sync_ecef_from_ned(&plant.body, sim_param_store.active.core.earth_radius_m);
            }
            if (condition_ctx.vehicle_params_dirty || condition_ctx.sim_params_dirty)
            {
                if (!altair_fixedwing_sim_params_are_valid(&sim_param_store.staged,
                                                           &vehicle_param_store.staged,
                                                           model_error,
                                                           sizeof(model_error)))
                {
                    fprintf(stderr,
                            "invalid fixed-wing sim model after conditions at step %d: %s\n",
                            i,
                            model_error);
                    qgc_close(&qgc);
                    return 1;
                }
                if (condition_ctx.vehicle_params_dirty &&
                    !altair_vehicle_param_store_commit(
                        &vehicle_param_store, model_error, sizeof(model_error)))
                {
                    fprintf(stderr,
                            "invalid vehicle params after conditions at step %d: %s\n",
                            i,
                            model_error);
                    qgc_close(&qgc);
                    return 1;
                }
                else if (!condition_ctx.vehicle_params_dirty)
                {
                    vehicle_param_store.staged_valid = 0U;
                }
                if (condition_ctx.sim_params_dirty &&
                    !altair_sim_param_store_commit(&sim_param_store,
                                                   &vehicle_param_store.active,
                                                   model_error,
                                                   sizeof(model_error)))
                {
                    fprintf(stderr,
                            "invalid sim params after conditions at step %d: %s\n",
                            i,
                            model_error);
                    qgc_close(&qgc);
                    return 1;
                }
                else if (!condition_ctx.sim_params_dirty)
                {
                    sim_param_store.staged_valid = 0U;
                }
            }
            if (condition_ctx.mission_dirty)
            {
                if (mission_enabled && mission.waypoint_count > 0U)
                {
                    altair_fsw_set_mission(&fsw, &mission);
                }
                else
                {
                    altair_fsw_clear_mission(&fsw);
                }
            }
        }
        if (trim_config.enabled && !trim_status.achieved && !trim_status.failed)
        {
            if (!sitl_trim_fixedwing_level(&plant,
                                           &sim_param_store.active,
                                           &vehicle_param_store.active,
                                           &trim_config,
                                           &trim_status,
                                           trim_error,
                                           sizeof(trim_error)))
            {
                fprintf(stderr, "trim failed at step %d: %s\n", i, trim_error);
                if (trim_config.fail_on_error)
                {
                    qgc_close(&qgc);
                    return 1;
                }
            }
            if (trim_status.achieved)
            {
                initial.rc.throttle = trim_status.actuators.motor;
                initial.rc.roll = trim_status.actuators.aileron;
                initial.rc.pitch =
                    vehicle_param_store.active.max_pitch_rad > 0.0f
                        ? trim_status.pitch_rad / vehicle_param_store.active.max_pitch_rad
                        : initial.rc.pitch;
                initial.rc.yaw = trim_status.actuators.rudder;
                rc = initial.rc;
                sim_fixedwing_make_fsw_input(
                    &plant, &rc, (real_t)cfg->dt_s, (uint32_t)(i * cfg->dt_s * 1000000.0), &input);
                truth_geo_from_state(&plant.body,
                                     sim_param_store.active.core.earth_radius_m,
                                     &lat_deg,
                                     &lon_deg,
                                     &altitude_m);
                input.gps.lat_deg = lat_deg;
                input.gps.lon_deg = lon_deg;
                input.gps.alt_m = altitude_m;
                input.baro.altitude_m = altitude_m;
                input.gps.fix_valid = gps_fix_valid;
            }
        }
        altair_fsw_step(&fsw, &input, &output);
        altair_fsw_get_mission_status(&fsw, &mission_status);
        if (!sim_output_is_bounded(&output))
        {
            fprintf(stderr, "unbounded_output at step %d\n", i);
            qgc_close(&qgc);
            return 2;
        }
        if (!sim_fixedwing_step(
                &plant, &sim_param_store.active, &output.actuators, (real_t)cfg->dt_s))
        {
            fprintf(stderr, "invalid_sim_state at step %d\n", i);
            qgc_close(&qgc);
            return 2;
        }
        euler = euler_from_quat(plant.body.attitude_body_to_ned);
        truth_geo_from_state(&plant.body,
                             sim_param_store.active.core.earth_radius_m,
                             &lat_deg,
                             &lon_deg,
                             &altitude_m);
        qgc_send_state(&qgc, cfg, i, &plant, lat_deg, lon_deg, altitude_m, euler);
        if (fprintf(csv,
                    "%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                    "%u,%.8f,%.8f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                    "%.6f,%.6f,%.6f,%.8f,%.8f,%.8f,%.8f,%.6f,%.6f,%.6f,"
                    "%.6f,%.6f,%.6f,%.6f,%.6f,"
                    "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                    "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                    "%u,%u,%u,%u,%.6f,%u,%u,%u,%.6f\n",
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
                    (double)plant.body.velocity_ecef_mps.z,
                    (unsigned)trim_status.active,
                    (unsigned)trim_status.achieved,
                    (unsigned)trim_status.failed,
                    (unsigned)trim_status.iteration_count,
                    (double)trim_status.residual_norm,
                    (unsigned)mission_status.loaded,
                    (unsigned)mission_status.active_waypoint_index,
                    (unsigned)mission_status.waypoint_count,
                    (double)mission_status.horizontal_distance_m) < 0)
        {
            fprintf(stderr, "failed to write output\n");
            qgc_close(&qgc);
            return 1;
        }
        pace_realtime_step(cfg, start_wall_s, i + 1);
    }
    qgc_close(&qgc);
    return 0;
}

int main(int argc, char **argv)
{
    sitl_config_t cfg;
    int steps;
    clock_t start;
    clock_t end;
    double start_wall_s;
    double end_wall_s;
    FILE *csv = stdout;
    int parse_result;
    int run_result;
    sitl_case_t case_file;
    sitl_conditions_t conditions;
    char case_error[200];
    char condition_error[200];

    parse_result = parse_args(argc, argv, &cfg);
    if (parse_result > 0)
    {
        return 0;
    }
    if (parse_result < 0)
    {
        print_usage(stderr);
        return 1;
    }
    if (cfg.case_path != NULL)
    {
        if (!sitl_case_load(cfg.case_path, &case_file, case_error, sizeof(case_error)))
        {
            fprintf(stderr, "failed to load case file: %s\n", case_error);
            return 1;
        }
        apply_case_run_config(&cfg, &case_file);
    }
    if (validate_config(&cfg) < 0)
    {
        print_usage(stderr);
        return 1;
    }
    if (cfg.condition_path != NULL)
    {
        if (!sitl_conditions_load(
                cfg.condition_path, &conditions, condition_error, sizeof(condition_error)))
        {
            fprintf(stderr, "failed to load condition file: %s\n", condition_error);
            return 1;
        }
        cfg.conditions = &conditions;
    }

    steps = (int)(cfg.duration_s / cfg.dt_s);
    if (steps <= 0)
    {
        fprintf(stderr, "duration and dt produce no simulation steps\n");
        return 1;
    }
    if (cfg.output_path != NULL)
    {
        csv = fopen(cfg.output_path, "w");
        if (csv == NULL)
        {
            fprintf(stderr, "failed to open output %s\n", cfg.output_path);
            return 1;
        }
    }

    start = clock();
    start_wall_s = wall_time_s();
    if (strcmp(cfg.scenario, "cruise6dof") == 0)
    {
        run_result = run_cruise6dof(&cfg, steps, csv);
    }
    else
    {
        run_result = run_smoke(&cfg, steps, csv);
    }
    end = clock();
    end_wall_s = wall_time_s();
    if (run_result != 0)
    {
        if (csv != stdout)
        {
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
    if (!checked_close(csv, cfg.output_path != NULL ? cfg.output_path : "stdout"))
    {
        return 1;
    }
    return 0;
}
