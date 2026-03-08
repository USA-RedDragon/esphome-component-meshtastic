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

// TEXT_MESSAGE_APP. (from, to, channel name, text, rssi, snr)
class TextTrigger : public Trigger<uint32_t, uint32_t, std::string, std::string, float, float> {
 public:
  explicit TextTrigger(Meshtastic *parent) { parent->add_on_text_trigger(this); }
};

// NODEINFO_APP. (from, channel, long_name, short_name, hw_model name, role name, rssi, snr)
class NodeInfoTrigger
    : public Trigger<uint32_t, std::string, std::string, std::string, std::string, std::string, float, float> {
 public:
  explicit NodeInfoTrigger(Meshtastic *parent) { parent->add_on_nodeinfo_trigger(this); }
};

// POSITION_APP. (from, channel, latitude deg, longitude deg, altitude m, precision_bits, time epoch, rssi, snr)
// precision_bits < 32 means the sender coarsened the location (anonymization); 0 = unset.
class PositionTrigger
    : public Trigger<uint32_t, std::string, double, double, int32_t, uint32_t, uint32_t, float, float> {
 public:
  explicit PositionTrigger(Meshtastic *parent) { parent->add_on_position_trigger(this); }
};

// TELEMETRY_APP device-metrics. (from, channel, battery %, voltage, channel util %, air util tx %, uptime s, rssi, snr)
class TelemetryTrigger
    : public Trigger<uint32_t, std::string, uint32_t, float, float, float, uint32_t, float, float> {
 public:
  explicit TelemetryTrigger(Meshtastic *parent) { parent->add_on_telemetry_trigger(this); }
};

// meshtastic.send_text action. dest defaults to broadcast; channel is a channel name (empty = primary).
template<typename... Ts> class SendTextAction : public Action<Ts...> {
 public:
  explicit SendTextAction(Meshtastic *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, text)
  TEMPLATABLE_VALUE(uint32_t, dest)
  TEMPLATABLE_VALUE(std::string, channel)
  TEMPLATABLE_VALUE(bool, want_ack)

  void play(Ts... x) override {
    this->parent_->send_text(this->text_.value(x...), this->dest_.value(x...), this->channel_.value(x...),
                             this->want_ack_.value(x...));
  }

 protected:
  Meshtastic *parent_;
};

}  // namespace meshtastic
}  // namespace esphome
