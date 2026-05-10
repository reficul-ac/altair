#ifdef ARDUINO
#include <Arduino.h>
#endif

extern "C" {
#include "altair_hal.h"
#include "altair_params.h"
#include "fsw.h"
}

void setup() {
  altair_hal_init();
  fsw_init(altair_default_params());
}

void loop() {
  fsw_input_t input;
  fsw_output_t output;
  altair_hal_read_inputs(&input);
  fsw_step(&input, &output);
  altair_hal_write_actuators(&output.actuators);
  altair_hal_send_telemetry(&output);
#ifdef ARDUINO
  delay(10);
#endif
}
