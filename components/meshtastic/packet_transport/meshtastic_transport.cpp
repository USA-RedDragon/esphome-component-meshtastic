#include "meshtastic_transport.h"
#include "esphome/core/log.h"

namespace esphome {
namespace meshtastic {

static const char *const TAG = "meshtastic.transport";

void MeshtasticTransport::setup() {
  packet_transport::PacketTransport::setup();
  this->parent_->register_transport(this->channel_, [this](const uint8_t *data, size_t len) {
    this->process_(std::span<const uint8_t>(data, len));
  });
}

void MeshtasticTransport::dump_config() {
  ESP_LOGCONFIG(TAG, "Meshtastic packet_transport on channel \"%s\"", this->channel_.c_str());
}

void MeshtasticTransport::send_packet(const std::vector<uint8_t> &buf) const {
  this->parent_->send_transport_packet(this->channel_, buf);
}

}  // namespace meshtastic
}  // namespace esphome
