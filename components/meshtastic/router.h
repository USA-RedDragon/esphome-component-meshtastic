#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace meshtastic {

// Whether a device role rebroadcasts foreign packets
// `role` is a meshtastic_Config_DeviceConfig_Role value
bool role_rebroadcasts(uint32_t role);

// Randomized, role-tiered rebroadcast backoff in ms
uint32_t rebroadcast_delay_ms(uint32_t role, float snr);

}  // namespace meshtastic
}  // namespace esphome
