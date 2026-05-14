#ifndef ALTAIR_HAL_H
#define ALTAIR_HAL_H

#include "common_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void altair_hal_init(void);
    uint32_t altair_hal_time_us(void);
    void altair_hal_read_inputs(fsw_input_t *input);
    void altair_hal_write_actuators(const actuator_cmd_t *cmd);
    void altair_hal_send_telemetry(const fsw_output_t *output);

#ifdef __cplusplus
}
#endif

#endif
