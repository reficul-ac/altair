#ifdef ARDUINO
#include <Arduino.h>
#endif

extern "C"
{
#include "altair_hal.h"
#include "altair_vehicle.h"
#include "altair_fsw.h"
}

static altair_fsw_t fsw;

void setup()
{
    altair_hal_init();
    altair_fsw_init(&fsw, altair_vehicle_interface());
}

void loop()
{
    fsw_input_t input;
    fsw_output_t output;
    altair_hal_read_inputs(&input);
    altair_fsw_step(&fsw, &input, &output);
    altair_hal_write_actuators(&output.actuators);
    altair_hal_send_telemetry(&output);
#ifdef ARDUINO
    delay(10);
#endif
}
