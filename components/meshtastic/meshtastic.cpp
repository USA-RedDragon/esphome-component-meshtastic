#include "meshtastic.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace meshtastic {

static const char *const TAG = "meshtastic";

void Meshtastic::setup() {
  if (this->node_num_ == 0) {
    uint8_t mac[6];
    get_mac_address_raw(mac);
    this->node_num_ = ((uint32_t) mac[2] << 24) | ((uint32_t) mac[3] << 16) | ((uint32_t) mac[4] << 8) | mac[5];
  }
  if (this->short_name_.empty())
    this->short_name_ = str_snprintf("%04x", 4, this->node_num_ & 0xFFFF);
  if (this->long_name_.empty())
    this->long_name_ = "Meshtastic " + this->short_name_;
}

void Meshtastic::dump_config() {
  ESP_LOGCONFIG(TAG, "Meshtastic:");
  ESP_LOGCONFIG(TAG, "  Node: !%08x \"%s\" (%s)", this->node_num_, this->long_name_.c_str(),
                this->short_name_.c_str());
#ifdef USE_SX126X
  if (this->sx126x_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Radio: SX126x");
#endif
#ifdef USE_SX127X
  if (this->sx127x_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Radio: SX127x");
#endif
}

#ifdef USE_SX126X
void Meshtastic::set_radio(sx126x::SX126x *radio) {
  this->sx126x_ = radio;
  radio->register_listener(this);
}
#endif
#ifdef USE_SX127X
void Meshtastic::set_radio(sx127x::SX127x *radio) {
  this->sx127x_ = radio;
  radio->register_listener(this);
}
#endif

#if defined(USE_SX126X) || defined(USE_SX127X)
void Meshtastic::on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) {
  PacketHeader h;
  if (!parse_header(packet, h)) {
    ESP_LOGW(TAG, "Dropping runt packet: %zu bytes", packet.size());
    return;
  }
  ESP_LOGD(TAG, "RX %zuB rssi=%.0f snr=%.1f from=!%08x to=!%08x id=0x%08x ch=0x%02x hop=%u/%u%s%s", packet.size(),
           rssi, snr, h.from, h.to, h.id, h.channel, h.hop_limit, h.hop_start, h.want_ack ? " ack" : "",
           h.via_mqtt ? " mqtt" : "");
}
#endif

}  // namespace meshtastic
}  // namespace esphome
