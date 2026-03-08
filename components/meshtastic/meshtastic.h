#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "channel.h"
#include "protocol.h"
#include <string>
#include <vector>

#if __has_include("esphome/components/sx126x/sx126x.h")
#include "esphome/components/sx126x/sx126x.h"
#define USE_SX126X 1
#endif
#if __has_include("esphome/components/sx127x/sx127x.h")
#include "esphome/components/sx127x/sx127x.h"
#define USE_SX127X 1
#endif

namespace esphome {
namespace meshtastic {

class Meshtastic : public Component
#ifdef USE_SX126X
                   , public sx126x::SX126xListener
#endif
#ifdef USE_SX127X
                   , public sx127x::SX127xListener
#endif
{
 public:
  void setup() override;
  void dump_config() override;

  void set_long_name(const std::string &name) { this->long_name_ = name; }
  void set_short_name(const std::string &name) { this->short_name_ = name; }
  void set_node_num(uint32_t num) { this->node_num_ = num; }
  void set_role(uint32_t role) { this->role_ = role; }
  void set_hop_limit(uint8_t hop_limit) { this->hop_limit_ = hop_limit; }
  void add_channel(const std::string &name, const std::vector<uint8_t> &key, bool uplink, bool downlink);

#ifdef USE_SX126X
  void set_radio(sx126x::SX126x *radio);
#endif
#ifdef USE_SX127X
  void set_radio(sx127x::SX127x *radio);
#endif

#if defined(USE_SX126X) || defined(USE_SX127X)
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;
#endif

 protected:
  std::string long_name_;
  std::string short_name_;
  uint32_t node_num_{0};
  uint32_t role_{0};  // meshtastic_Config_DeviceConfig_Role_CLIENT
  uint8_t hop_limit_{3};
  std::vector<Channel> channels_;

#ifdef USE_SX126X
  sx126x::SX126x *sx126x_{nullptr};
#endif
#ifdef USE_SX127X
  sx127x::SX127x *sx127x_{nullptr};
#endif
};

}  // namespace meshtastic
}  // namespace esphome
