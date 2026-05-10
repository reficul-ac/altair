#include "altair_vehicle.h"
#include "fsw.h"
#include "sim_plant.h"

#include <stdio.h>
#include <time.h>

int main(void) {
  const real_t dt_s = 0.01f;
  const int steps = 1000;
  sim_plant_t plant;
  fsw_input_t input;
  fsw_output_t output;
  rc_input_t rc = {0.55f, 0.10f, 0.02f, 0.0f, 1U, 1U};
  int i;
  clock_t start;
  clock_t end;

  bayek_fsw_init(altair_vehicle_interface());
  sim_plant_init(&plant);

  printf("step,time_s,mode,motor,aileron,elevator,rudder,airspeed_mps,altitude_m\n");
  start = clock();
  for (i = 0; i < steps; ++i) {
    sim_make_fsw_input(&plant, &rc, dt_s, (uint32_t)(i * 10000U), &input);
    bayek_fsw_step(&input, &output);
    if (!sim_output_is_bounded(&output)) {
      return 2;
    }
    sim_plant_step(&plant, &output.actuators, dt_s);
    printf("%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
           i,
           (double)(i * dt_s),
           (int)output.mode,
           (double)output.actuators.motor,
           (double)output.actuators.aileron,
           (double)output.actuators.elevator,
           (double)output.actuators.rudder,
           (double)plant.airspeed_mps,
           (double)plant.altitude_m);
  }
  end = clock();
  fprintf(stderr, "sitl_steps=%d elapsed_s=%.6f\n", steps, (double)(end - start) / (double)CLOCKS_PER_SEC);
  return 0;
}
