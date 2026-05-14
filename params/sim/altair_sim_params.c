#include "altair_sim_params.h"

#include <stddef.h>

void altair_default_fixedwing_sim_params(sim_fixedwing_params_t *params)
{
    if (params == NULL)
    {
        return;
    }

    sim_fixedwing_default_params(params);

    params->core.mass_kg = 2.5f;
    params->core.inertia_kgm2.x = 0.08f;
    params->core.inertia_kgm2.y = 0.12f;
    params->core.inertia_kgm2.z = 0.18f;
    params->core.gravity_mps2 = 9.80665f;
    params->core.air_density_kgpm3 = 1.225f;
    params->core.actuator_lag_hz = 12.0f;
    params->core.frame_mode = SIM6DOF_FRAME_ECEF;
    params->core.earth_model = SIM6DOF_EARTH_SPHERICAL;
    params->core.earth_radius_m = 6378137.0f;

    params->wing_area_m2 = 0.45f;
    params->wing_span_m = 1.8f;
    params->mean_chord_m = 0.25f;
    params->max_thrust_n = 18.0f;
    params->drag_cd0 = 0.035f;
    params->drag_cd_alpha = 0.80f;
    params->lift_cl0 = 0.18f;
    params->lift_cl_alpha = 4.2f;
    params->lift_cl_elevator = 0.35f;
    params->stall_alpha_rad = 0.28f;
    params->roll_aileron_nm = 1.6f;
    params->pitch_elevator_nm = 1.0f;
    params->yaw_rudder_nm = 0.6f;
    params->rate_damping_nms.x = 0.35f;
    params->rate_damping_nms.y = 0.45f;
    params->rate_damping_nms.z = 0.30f;
}
