#include "altair_vehicle.h"

#include "altair_mixer.h"
#include "altair_params.h"

static const bayek_vehicle_interface_t k_altair_vehicle = {
  0,
  altair_mix_manual,
  altair_mix_control,
  altair_safe_actuators
};

const bayek_vehicle_interface_t *altair_vehicle_interface(void) {
  static bayek_vehicle_interface_t vehicle;
  vehicle = k_altair_vehicle;
  vehicle.params = altair_default_params();
  return &vehicle;
}
