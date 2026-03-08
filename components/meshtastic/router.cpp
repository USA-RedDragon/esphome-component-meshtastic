#include "router.h"

#include "esphome/core/helpers.h"
#include "mesh.pb.h"

namespace esphome {
namespace meshtastic {

bool role_rebroadcasts(uint32_t role) {
  switch (role) {
    case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:
    case meshtastic_Config_DeviceConfig_Role_SENSOR:
    case meshtastic_Config_DeviceConfig_Role_TRACKER:
      return false;
    default:
      return true;
  }
}

uint32_t rebroadcast_delay_ms(uint32_t role, float snr) {
  uint32_t base, window;
  switch (role) {
    case meshtastic_Config_DeviceConfig_Role_ROUTER:
      base = 0;
      window = 100;
      break;
    case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
      base = 300;
      window = 200;
      break;
    default:
      base = 100;
      window = 300;
      break;
  }
  if (snr > 5.0f)
    base += 100;
  return base + (random_uint32() % window);
}

}  // namespace meshtastic
}  // namespace esphome
