#include "altair_vehicle.h"
#include "fsw.h"
#include "sim_plant.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

int main(void) {
  sim_plant_t plant;
  fsw_input_t input;
  fsw_output_t output;
  rc_input_t rc = {0.55f, 0.1f, 0.0f, 0.0f, 1U, 1U};
  int i;
  bayek_fsw_init(altair_vehicle_interface());
  sim_plant_init(&plant);
  for (i = 0; i < 300; ++i) {
    sim_make_fsw_input(&plant, &rc, 0.01f, (uint32_t)(i * 10000U), &input);
    bayek_fsw_step(&input, &output);
    CHECK(sim_output_is_bounded(&output));
    sim_plant_step(&plant, &output.actuators, 0.01f);
  }
  CHECK(plant.airspeed_mps >= 5.0f && plant.airspeed_mps <= 40.0f);
  return 0;
}
