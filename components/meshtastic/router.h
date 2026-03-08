#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace meshtastic {

class PacketDedup {
 public:
  // Records (from,id)@now_ms; returns true if it was already seen within the TTL window.
  bool is_duplicate(uint32_t from, uint32_t id, uint32_t now_ms);

 protected:
  static constexpr size_t SIZE = 32;
  static constexpr uint32_t TTL_MS = 5 * 60 * 1000;
  struct Entry {
    uint32_t from;
    uint32_t id;
    uint32_t ts;
    bool valid;
  };
  Entry entries_[SIZE]{};
  size_t next_{0};
};

// Whether a device role rebroadcasts foreign packets
// `role` is a meshtastic_Config_DeviceConfig_Role value
bool role_rebroadcasts(uint32_t role);

// Randomized, role-tiered rebroadcast backoff in ms
uint32_t rebroadcast_delay_ms(uint32_t role, float snr);

}  // namespace meshtastic
}  // namespace esphome
