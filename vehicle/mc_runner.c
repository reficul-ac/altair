#include "altair_vehicle.h"
#include "fsw.h"
#include "sim_plant.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint32_t seed;
  int runs;
  int steps;
  real_t throttle_bias_span;
} mc_config_t;

static uint32_t lcg_next(uint32_t *state) {
  *state = (*state * 1664525U) + 1013904223U;
  return *state;
}

static real_t rand_unit(uint32_t *state) {
  return (real_t)((lcg_next(state) >> 8) & 0x00ffffffU) / 16777215.0f;
}

int main(int argc, char **argv) {
  mc_config_t cfg = {1U, 10, 500, 0.08f};
  int run;
  if (argc > 1) {
    cfg.seed = (uint32_t)strtoul(argv[1], 0, 10);
  }
  if (argc > 2) {
    cfg.runs = atoi(argv[2]);
  }

  printf("run,seed,throttle_bias,final_airspeed_mps,final_altitude_m,max_abs_roll_rad\n");
  for (run = 0; run < cfg.runs; ++run) {
    uint32_t rng = cfg.seed + (uint32_t)run;
    real_t throttle_bias = (rand_unit(&rng) * 2.0f - 1.0f) * cfg.throttle_bias_span;
    real_t max_abs_roll = 0.0f;
    int step;
    sim_plant_t plant;
    fsw_input_t input;
    fsw_output_t output;
    rc_input_t rc = {0.52f + throttle_bias, 0.05f, 0.0f, 0.0f, 1U, 1U};

    bayek_fsw_init(altair_vehicle_interface());
    sim_plant_init(&plant);
    for (step = 0; step < cfg.steps; ++step) {
      real_t abs_roll;
      sim_make_fsw_input(&plant, &rc, 0.01f, (uint32_t)(step * 10000U), &input);
      bayek_fsw_step(&input, &output);
      if (!sim_output_is_bounded(&output)) {
        return 3;
      }
      sim_plant_step(&plant, &output.actuators, 0.01f);
      abs_roll = plant.attitude.roll < 0.0f ? -plant.attitude.roll : plant.attitude.roll;
      if (abs_roll > max_abs_roll) {
        max_abs_roll = abs_roll;
      }
    }
    printf("%d,%u,%.6f,%.6f,%.6f,%.6f\n",
           run,
           cfg.seed + (uint32_t)run,
           (double)throttle_bias,
           (double)plant.airspeed_mps,
           (double)plant.altitude_m,
           (double)max_abs_roll);
  }
  return 0;
}
