#ifndef ALTAIR_FSW_H
#define ALTAIR_FSW_H

#include "common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void fsw_init(const vehicle_params_t *params);
void fsw_reset(void);
void fsw_step(const fsw_input_t *in, fsw_output_t *out);

#ifdef __cplusplus
}
#endif

#endif
