#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/packet_transport/packet_transport.h"
#include "../meshtastic.h"

#include <span>
#include <string>
#include <vector>

namespace esphome {
namespace meshtastic {

// ESPHome packet_transport over the Meshtastic mesh: opaque frames ride a Data{PRIVATE_APP} packet on a
// dedicated PSK channel, so two ESPHome nodes can share sensor state without any IP network.
class MeshtasticTransport : public packet_transport::PacketTransport, public Parented<Meshtastic> {
 public:
  void setup() override;
  void dump_config() override;
  void set_channel(const std::string &channel) { this->channel_ = channel; }

 protected:
  void send_packet(const std::vector<uint8_t> &buf) const override;
  size_t get_max_packet_size() override { return meshtastic_Constants_DATA_PAYLOAD_LEN; }

  std::string channel_;
};

}  // namespace meshtastic
}  // namespace esphome
