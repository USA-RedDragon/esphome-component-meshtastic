#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "meshtastic.h"

namespace esphome {
namespace meshtastic {

// Fires for every decoded packet. (from, to, portnum, payload, rssi, snr)
class PacketTrigger : public Trigger<uint32_t, uint32_t, uint32_t, std::vector<uint8_t>, float, float> {
 public:
  explicit PacketTrigger(Meshtastic *parent) { parent->add_on_packet_trigger(this); }
};

// TEXT_MESSAGE_APP. (from, to, channel index, text, rssi, snr)
class TextTrigger : public Trigger<uint32_t, uint32_t, uint8_t, std::string, float, float> {
 public:
  explicit TextTrigger(Meshtastic *parent) { parent->add_on_text_trigger(this); }
};

// NODEINFO_APP. (from, long_name, short_name, hw_model, role)
class NodeInfoTrigger : public Trigger<uint32_t, std::string, std::string, uint32_t, uint32_t> {
 public:
  explicit NodeInfoTrigger(Meshtastic *parent) { parent->add_on_nodeinfo_trigger(this); }
};

}  // namespace meshtastic
}  // namespace esphome
