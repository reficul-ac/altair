#ifndef ALTAIR_SIM_PLANT_H
#define ALTAIR_SIM_PLANT_H

#include "common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  euler_t attitude;
  vec3_t rates_rps;
  real_t altitude_m;
  real_t airspeed_mps;
  actuator_cmd_t actuator_state;
} sim_plant_t;

void sim_plant_init(sim_plant_t *plant);
void sim_plant_step(sim_plant_t *plant, const actuator_cmd_t *cmd, real_t dt_s);
void sim_make_fsw_input(const sim_plant_t *plant, const rc_input_t *rc, real_t dt_s, uint32_t timestamp_us, fsw_input_t *in);
int sim_output_is_bounded(const fsw_output_t *out);

#ifdef __cplusplus
}
#endif

#endif
