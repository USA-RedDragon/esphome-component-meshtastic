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

// POSITION_APP. (from, latitude deg, longitude deg, altitude m, time epoch, rssi, snr)
class PositionTrigger : public Trigger<uint32_t, double, double, int32_t, uint32_t, float, float> {
 public:
  explicit PositionTrigger(Meshtastic *parent) { parent->add_on_position_trigger(this); }
};

// TELEMETRY_APP device-metrics variant. (from, battery %, voltage, channel util %, air util tx %, uptime s)
class TelemetryTrigger : public Trigger<uint32_t, uint32_t, float, float, float, uint32_t> {
 public:
  explicit TelemetryTrigger(Meshtastic *parent) { parent->add_on_telemetry_trigger(this); }
};

}  // namespace meshtastic
}  // namespace esphome
