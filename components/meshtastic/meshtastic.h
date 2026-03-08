#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
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
  void dump_config() override;

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
#ifdef USE_SX126X
  sx126x::SX126x *sx126x_{nullptr};
#endif
#ifdef USE_SX127X
  sx127x::SX127x *sx127x_{nullptr};
#endif
};

}  // namespace meshtastic
}  // namespace esphome
