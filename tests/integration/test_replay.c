#include "altair_vehicle.h"
#include "fsw.h"

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

static void make_input(int i, fsw_input_t *in)
{
    in->dt_s = 0.01f;
    in->rc.throttle = 0.5f;
    in->rc.roll = (i % 7) * 0.02f;
    in->rc.pitch = -0.05f;
    in->rc.yaw = 0.01f;
    in->rc.arm_switch = 1U;
    in->rc.mode_switch = 1U;
    in->imu.gyro_rps.x = 0.001f * (real_t)i;
    in->imu.gyro_rps.y = -0.001f * (real_t)i;
    in->imu.gyro_rps.z = 0.0005f * (real_t)i;
    in->imu.accel_mps2.x = 0.0f;
    in->imu.accel_mps2.y = 0.0f;
    in->imu.accel_mps2.z = -9.80665f;
    in->gps.vel_mps.x = 15.0f;
    in->gps.vel_mps.y = 0.0f;
    in->gps.vel_mps.z = 0.0f;
    in->gps.fix_valid = 1U;
    in->baro.altitude_m = 100.0f;
    in->airspeed.true_airspeed_mps = 15.0f;
}

int main(void)
{
    fsw_output_t a[64];
    fsw_output_t b[64];
    fsw_input_t in;
    int i;
    bayek_fsw_init(altair_vehicle_interface());
    for (i = 0; i < 64; ++i)
    {
        make_input(i, &in);
        bayek_fsw_step(&in, &a[i]);
    }
    bayek_fsw_reset();
    for (i = 0; i < 64; ++i)
    {
        make_input(i, &in);
        bayek_fsw_step(&in, &b[i]);
        CHECK(fabsf(a[i].actuators.motor - b[i].actuators.motor) < 1.0e-7f);
        CHECK(fabsf(a[i].actuators.aileron - b[i].actuators.aileron) < 1.0e-7f);
        CHECK(fabsf(a[i].actuators.elevator - b[i].actuators.elevator) < 1.0e-7f);
        CHECK(fabsf(a[i].actuators.rudder - b[i].actuators.rudder) < 1.0e-7f);
    }
    return 0;
}
