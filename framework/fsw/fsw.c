#include "fsw.h"

#include "altair_mixer.h"
#include "altair_params.h"
#include "control_utils.h"
#include "math_utils.h"

typedef struct {
  const vehicle_params_t *params;
  pid_t roll_pid;
  pid_t pitch_pid;
  pid_t yaw_rate_pid;
  state_estimate_t estimate;
} fsw_state_t;

static fsw_state_t g_fsw;

static fsw_mode_t select_mode(const fsw_input_t *in) {
  if (!in->rc.arm_switch) {
    return FSW_MODE_DISARMED;
  }
  if (!in->gps.fix_valid || in->dt_s <= 0.0f || in->dt_s > 0.1f) {
    return FSW_MODE_FAILSAFE;
  }
  return in->rc.mode_switch ? FSW_MODE_STABILIZE : FSW_MODE_MANUAL;
}

static void update_estimate(const fsw_input_t *in, state_estimate_t *estimate) {
  estimate->angular_rate_rps = in->imu.gyro_rps;
  estimate->position_m.x = 0.0f;
  estimate->position_m.y = 0.0f;
  estimate->position_m.z = in->baro.altitude_m;
  estimate->velocity_mps = in->gps.vel_mps;
  estimate->airspeed_mps = in->airspeed.true_airspeed_mps;

  euler_t delta = {
    in->imu.gyro_rps.x * in->dt_s,
    in->imu.gyro_rps.y * in->dt_s,
    in->imu.gyro_rps.z * in->dt_s
  };
  quat_t dq = quat_from_euler(delta);
  estimate->attitude = quat_normalize(quat_multiply(estimate->attitude, dq));
  estimate->euler = euler_from_quat(estimate->attitude);
}

void fsw_init(const vehicle_params_t *params) {
  g_fsw.params = params ? params : altair_default_params();
  pid_init(&g_fsw.roll_pid, 2.0f, 0.1f, 0.02f, -1.0f, 1.0f);
  pid_init(&g_fsw.pitch_pid, 2.0f, 0.1f, 0.02f, -1.0f, 1.0f);
  pid_init(&g_fsw.yaw_rate_pid, 1.0f, 0.05f, 0.0f, -1.0f, 1.0f);
  fsw_reset();
}

void fsw_reset(void) {
  if (!g_fsw.params) {
    g_fsw.params = altair_default_params();
  }
  pid_reset(&g_fsw.roll_pid);
  pid_reset(&g_fsw.pitch_pid);
  pid_reset(&g_fsw.yaw_rate_pid);
  g_fsw.estimate.attitude = quat_identity();
  g_fsw.estimate.euler.roll = 0.0f;
  g_fsw.estimate.euler.pitch = 0.0f;
  g_fsw.estimate.euler.yaw = 0.0f;
  g_fsw.estimate.angular_rate_rps.x = 0.0f;
  g_fsw.estimate.angular_rate_rps.y = 0.0f;
  g_fsw.estimate.angular_rate_rps.z = 0.0f;
  g_fsw.estimate.position_m.x = 0.0f;
  g_fsw.estimate.position_m.y = 0.0f;
  g_fsw.estimate.position_m.z = 0.0f;
  g_fsw.estimate.velocity_mps.x = 0.0f;
  g_fsw.estimate.velocity_mps.y = 0.0f;
  g_fsw.estimate.velocity_mps.z = 0.0f;
  g_fsw.estimate.airspeed_mps = 0.0f;
}

void fsw_step(const fsw_input_t *in, fsw_output_t *out) {
  if (!g_fsw.params) {
    fsw_init(0);
  }
  update_estimate(in, &g_fsw.estimate);

  out->estimate = g_fsw.estimate;
  out->mode = select_mode(in);

  if (out->mode == FSW_MODE_DISARMED || out->mode == FSW_MODE_FAILSAFE) {
    out->actuators = altair_safe_actuators(g_fsw.params);
    return;
  }

  if (out->mode == FSW_MODE_MANUAL) {
    out->actuators = altair_mix_manual(&in->rc);
    return;
  }

  real_t desired_roll = clamp_real(in->rc.roll, -1.0f, 1.0f) * g_fsw.params->max_roll_rad;
  real_t desired_pitch = clamp_real(in->rc.pitch, -1.0f, 1.0f) * g_fsw.params->max_pitch_rad;
  real_t desired_yaw_rate = clamp_real(in->rc.yaw, -1.0f, 1.0f) * g_fsw.params->max_yaw_rate_rps;

  real_t roll_cmd = pid_step(&g_fsw.roll_pid, desired_roll, g_fsw.estimate.euler.roll, in->dt_s);
  real_t pitch_cmd = pid_step(&g_fsw.pitch_pid, desired_pitch, g_fsw.estimate.euler.pitch, in->dt_s);
  real_t yaw_cmd = pid_step(&g_fsw.yaw_rate_pid, desired_yaw_rate, in->imu.gyro_rps.z, in->dt_s);

  out->actuators = altair_mix_control(in->rc.throttle, roll_cmd, pitch_cmd, yaw_cmd);
}
