#include "sim_plant.h"

#include "math_utils.h"

void sim_plant_init(sim_plant_t *plant) {
  plant->attitude.roll = 0.0f;
  plant->attitude.pitch = 0.0f;
  plant->attitude.yaw = 0.0f;
  plant->rates_rps.x = 0.0f;
  plant->rates_rps.y = 0.0f;
  plant->rates_rps.z = 0.0f;
  plant->altitude_m = 100.0f;
  plant->airspeed_mps = 15.0f;
  plant->actuator_state.motor = 0.0f;
  plant->actuator_state.aileron = 0.0f;
  plant->actuator_state.elevator = 0.0f;
  plant->actuator_state.rudder = 0.0f;
}

void sim_plant_step(sim_plant_t *plant, const actuator_cmd_t *cmd, real_t dt_s) {
  real_t lag = clamp_real(dt_s * 12.0f, 0.0f, 1.0f);
  plant->actuator_state.motor = lerp_real(plant->actuator_state.motor, cmd->motor, lag);
  plant->actuator_state.aileron = lerp_real(plant->actuator_state.aileron, cmd->aileron, lag);
  plant->actuator_state.elevator = lerp_real(plant->actuator_state.elevator, cmd->elevator, lag);
  plant->actuator_state.rudder = lerp_real(plant->actuator_state.rudder, cmd->rudder, lag);

  plant->rates_rps.x = 1.8f * plant->actuator_state.aileron - 0.6f * plant->attitude.roll;
  plant->rates_rps.y = 1.2f * plant->actuator_state.elevator - 0.5f * plant->attitude.pitch;
  plant->rates_rps.z = 0.8f * plant->actuator_state.rudder;

  plant->attitude.roll = wrap_pi(plant->attitude.roll + plant->rates_rps.x * dt_s);
  plant->attitude.pitch = clamp_real(plant->attitude.pitch + plant->rates_rps.y * dt_s, -1.0f, 1.0f);
  plant->attitude.yaw = wrap_pi(plant->attitude.yaw + plant->rates_rps.z * dt_s);
  plant->airspeed_mps = clamp_real(plant->airspeed_mps + (plant->actuator_state.motor - 0.45f) * 3.0f * dt_s, 5.0f, 40.0f);
  plant->altitude_m += (plant->airspeed_mps * plant->attitude.pitch - 0.2f) * dt_s;
}

void sim_make_fsw_input(const sim_plant_t *plant, const rc_input_t *rc, real_t dt_s, uint32_t timestamp_us, fsw_input_t *in) {
  in->dt_s = dt_s;
  in->rc = *rc;
  in->imu.gyro_rps = plant->rates_rps;
  in->imu.accel_mps2.x = 0.0f;
  in->imu.accel_mps2.y = 0.0f;
  in->imu.accel_mps2.z = -9.80665f;
  in->imu.timestamp_us = timestamp_us;
  in->gps.lat_deg = 0.0f;
  in->gps.lon_deg = 0.0f;
  in->gps.alt_m = plant->altitude_m;
  in->gps.vel_mps.x = plant->airspeed_mps;
  in->gps.vel_mps.y = 0.0f;
  in->gps.vel_mps.z = plant->airspeed_mps * plant->attitude.pitch;
  in->gps.fix_valid = 1U;
  in->gps.timestamp_us = timestamp_us;
  in->baro.pressure_pa = 101325.0f;
  in->baro.altitude_m = plant->altitude_m;
  in->baro.timestamp_us = timestamp_us;
  in->airspeed.true_airspeed_mps = plant->airspeed_mps;
  in->airspeed.timestamp_us = timestamp_us;
}

int sim_output_is_bounded(const fsw_output_t *out) {
  return real_is_finite(out->actuators.motor) &&
         real_is_finite(out->actuators.aileron) &&
         real_is_finite(out->actuators.elevator) &&
         real_is_finite(out->actuators.rudder) &&
         out->actuators.motor >= 0.0f && out->actuators.motor <= 1.0f &&
         out->actuators.aileron >= -1.0f && out->actuators.aileron <= 1.0f &&
         out->actuators.elevator >= -1.0f && out->actuators.elevator <= 1.0f &&
         out->actuators.rudder >= -1.0f && out->actuators.rudder <= 1.0f;
}
