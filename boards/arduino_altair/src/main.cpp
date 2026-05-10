#ifdef ARDUINO
#include <Arduino.h>
#endif

extern "C" {
#include "altair_hal.h"
#include "altair_vehicle.h"
#include "fsw.h"
}

void setup() {
  altair_hal_init();
  bayek_fsw_init(altair_vehicle_interface());
}

void loop() {
  fsw_input_t input;
  fsw_output_t output;
  altair_hal_read_inputs(&input);
  bayek_fsw_step(&input, &output);
  altair_hal_write_actuators(&output.actuators);
  altair_hal_send_telemetry(&output);
#ifdef ARDUINO
  delay(10);
#endif
}
