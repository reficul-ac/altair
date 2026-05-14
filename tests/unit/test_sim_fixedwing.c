#include "sim_fixedwing.h"

#include "math_utils.h"

#include <math.h>
#include <stdio.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            printf("check failed: %s:%d\n", __FILE__, __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static sim_fixedwing_state_t
run_for(real_t throttle, real_t aileron, real_t elevator, real_t seconds, int frame_mode)
{
    sim_fixedwing_params_t params;
    sim_fixedwing_state_t state;
    actuator_cmd_t cmd;
    int steps = (int)(seconds / 0.01f);
    int i;

    sim_fixedwing_default_params(&params);
    params.core.frame_mode = frame_mode;
    sim_fixedwing_init_default(&state);
    cmd.motor = throttle;
    cmd.aileron = aileron;
    cmd.elevator = elevator;
    cmd.rudder = 0.0f;
    for (i = 0; i < steps; ++i)
    {
        if (!sim_fixedwing_step(&state, &params, &cmd, 0.01f))
        {
            state.last_airspeed_mps = NAN;
            return state;
        }
    }
    return state;
}

static int near_real(real_t a, real_t b, real_t tol)
{
    return fabsf(a - b) <= tol;
}

static int near_vec3(vec3_t a, vec3_t b, real_t tol)
{
    return near_real(a.x, b.x, tol) && near_real(a.y, b.y, tol) && near_real(a.z, b.z, tol);
}

int main(void)
{
    sim_fixedwing_params_t params;
    sim_fixedwing_state_t initial;
    sim_fixedwing_state_t force_probe;
    sim_fixedwing_state_t cruise;
    sim_fixedwing_state_t low_throttle;
    sim_fixedwing_state_t high_throttle;
    sim_fixedwing_state_t roll;
    sim_fixedwing_state_t pitch;
    actuator_cmd_t cmd = {0.55f, 0.0f, 0.0f, 0.0f};
    actuator_cmd_t probe_cmd;
    rc_input_t rc = {0.52f, -0.25f, 0.10f, 0.33f, 1U, 2U};
    fsw_input_t input;
    int i;

    sim_fixedwing_default_params(&params);
    sim_fixedwing_init_default(&initial);
    CHECK(near_real(-initial.body.position_ned_m.z, 120.0f, 0.001f));
    CHECK(near_real(initial.body.velocity_ned_mps.x, 18.0f, 0.001f));
    CHECK(near_real(initial.body.actuator_state.motor, 0.55f, 0.001f));
    CHECK(near_real(initial.last_airspeed_mps, 18.0f, 0.001f));
    CHECK(near_real(initial.last_force_body_n.x, 0.0f, 0.001f));
    CHECK(near_real(
        initial.last_force_body_n.z, -params.core.mass_kg * params.core.gravity_mps2, 0.001f));

    force_probe = initial;
    force_probe.body.actuator_state.motor = 0.85f;
    CHECK(sim_fixedwing_step(&force_probe, &params, &force_probe.body.actuator_state, 0.01f));
    CHECK(force_probe.last_force_body_n.x > 0.0f);

    force_probe = initial;
    force_probe.body.actuator_state.elevator = 0.50f;
    CHECK(sim_fixedwing_step(&force_probe, &params, &force_probe.body.actuator_state, 0.01f));
    CHECK(force_probe.last_force_body_n.z < initial.last_force_body_n.z);
    CHECK(force_probe.last_moment_body_nm.y > 0.0f);

    force_probe = initial;
    force_probe.body.actuator_state.aileron = 0.50f;
    CHECK(sim_fixedwing_step(&force_probe, &params, &force_probe.body.actuator_state, 0.01f));
    CHECK(force_probe.last_moment_body_nm.x > 0.0f);

    force_probe = initial;
    force_probe.body.actuator_state.rudder = 0.50f;
    CHECK(sim_fixedwing_step(&force_probe, &params, &force_probe.body.actuator_state, 0.01f));
    CHECK(force_probe.last_force_body_n.y > 0.0f);
    CHECK(force_probe.last_moment_body_nm.z > 0.0f);

    initial.body.time_s = 3.5f;
    initial.body.omega_body_rps.x = 0.1f;
    initial.body.omega_body_rps.y = -0.2f;
    initial.body.omega_body_rps.z = 0.3f;
    initial.body.specific_force_body_mps2.x = 1.0f;
    initial.body.specific_force_body_mps2.y = 2.0f;
    initial.body.specific_force_body_mps2.z = 3.0f;
    initial.body.velocity_ned_mps.x = 17.0f;
    initial.body.velocity_ned_mps.y = -1.0f;
    initial.body.velocity_ned_mps.z = 0.5f;
    initial.body.position_ned_m.z = -123.0f;
    initial.last_airspeed_mps = 17.25f;
    sim_fixedwing_make_fsw_input(&initial, &rc, 0.02f, 123456U, &input);
    CHECK(near_real(input.dt_s, 0.02f, 0.0001f));
    CHECK(near_real(input.rc.throttle, rc.throttle, 0.0001f));
    CHECK(near_real(input.rc.roll, rc.roll, 0.0001f));
    CHECK(near_real(input.rc.pitch, rc.pitch, 0.0001f));
    CHECK(near_real(input.rc.yaw, rc.yaw, 0.0001f));
    CHECK(input.rc.arm_switch == rc.arm_switch && input.rc.mode_switch == rc.mode_switch);
    CHECK(near_vec3(input.imu.gyro_rps, initial.body.omega_body_rps, 0.0001f));
    CHECK(near_vec3(input.imu.accel_mps2, initial.body.specific_force_body_mps2, 0.0001f));
    CHECK(input.imu.timestamp_us == 123456U);
    CHECK(near_vec3(input.gps.vel_mps, initial.body.velocity_ned_mps, 0.0001f));
    CHECK(near_real(input.gps.lat_deg, 0.0f, 0.0001f));
    CHECK(near_real(input.gps.lon_deg, 0.0f, 0.0001f));
    CHECK(near_real(input.gps.alt_m, 123.0f, 0.0001f));
    CHECK(input.gps.fix_valid == 1U);
    CHECK(input.gps.timestamp_us == 123456U);
    CHECK(near_real(input.baro.altitude_m, 123.0f, 0.0001f));
    CHECK(input.baro.timestamp_us == 123456U);
    CHECK(near_real(input.airspeed.true_airspeed_mps, 17.25f, 0.0001f));
    CHECK(input.airspeed.timestamp_us == 123456U);

    sim_fixedwing_init_default(&cruise);
    for (i = 0; i < 1000; ++i)
    {
        CHECK(sim_fixedwing_step(&cruise, &params, &cmd, 0.01f));
    }
    CHECK(cruise.last_airspeed_mps > 8.0f && cruise.last_airspeed_mps < 45.0f);
    CHECK(-cruise.body.position_ned_m.z > 20.0f && -cruise.body.position_ned_m.z < 300.0f);

    low_throttle = run_for(0.25f, 0.0f, 0.0f, 3.0f, SIM6DOF_FRAME_ECEF);
    high_throttle = run_for(0.85f, 0.0f, 0.0f, 3.0f, SIM6DOF_FRAME_ECEF);
    CHECK(high_throttle.last_airspeed_mps > low_throttle.last_airspeed_mps + 1.0f);

    roll = run_for(0.55f, 0.5f, 0.0f, 1.0f, SIM6DOF_FRAME_ECEF);
    CHECK(fabsf(roll.body.omega_body_rps.x) > 0.5f);

    pitch = run_for(0.65f, 0.0f, 0.4f, 1.0f, SIM6DOF_FRAME_ECEF);
    CHECK(pitch.body.omega_body_rps.y > 0.3f || -pitch.body.position_ned_m.z > 121.0f);
    CHECK(sim_fixedwing_state_is_valid(&pitch));

    low_throttle = run_for(0.55f, 0.0f, 0.0f, 2.0f, SIM6DOF_FRAME_NED);
    high_throttle = run_for(0.55f, 0.0f, 0.0f, 2.0f, SIM6DOF_FRAME_ECEF);
    CHECK(fabsf(low_throttle.last_airspeed_mps - high_throttle.last_airspeed_mps) < 0.5f);
    CHECK(fabsf(low_throttle.body.position_ned_m.x - high_throttle.body.position_ned_m.x) < 2.0f);

    probe_cmd = cmd;
    force_probe = initial;
    force_probe.last_force_body_n.x = NAN;
    CHECK(!sim_fixedwing_state_is_valid(&force_probe));
    force_probe = initial;
    force_probe.last_moment_body_nm.z = NAN;
    CHECK(!sim_fixedwing_state_is_valid(&force_probe));
    force_probe = initial;
    force_probe.last_airspeed_mps = -0.01f;
    CHECK(!sim_fixedwing_state_is_valid(&force_probe));
    force_probe.last_airspeed_mps = 120.0f;
    CHECK(!sim_fixedwing_state_is_valid(&force_probe));
    CHECK(sim_fixedwing_step(&initial, &params, &probe_cmd, 0.01f));
    return 0;
}
