#include "meshtastic.h"

namespace esphome {
namespace meshtastic {

static const char *const TAG = "meshtastic";

void Meshtastic::dump_config() {
  ESP_LOGCONFIG(TAG, "Meshtastic:");
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
  ESP_LOGD(TAG, "Received packet: %zu bytes, RSSI %.1f dBm, SNR %.1f dB", packet.size(), rssi, snr);
}
#endif

}  // namespace meshtastic
}  // namespace esphome
