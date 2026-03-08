#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "channel.h"
#include "nodedb.h"
#include "protocol.h"
#include "router.h"
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
  void set_node_info_interval(uint32_t interval_ms) { this->node_info_interval_ = interval_ms; }
  void set_hw_model(uint32_t hw_model) { this->hw_model_ = hw_model; }
  void set_node_db_size(uint32_t size) { this->nodedb_.set_max_nodes(size); }
  void add_channel(const std::string &name, const std::vector<uint8_t> &key, bool uplink, bool downlink);

#ifdef USE_SX126X
  void set_radio(sx126x::SX126x *radio);
#endif
#ifdef USE_SX127X
  void set_radio(sx127x::SX127x *radio);
#endif

  using OnPacketTrigger = Trigger<uint32_t, uint32_t, uint32_t, std::vector<uint8_t>, float, float>;
  using OnTextTrigger = Trigger<uint32_t, uint32_t, uint8_t, std::string, float, float>;
  using OnNodeInfoTrigger = Trigger<uint32_t, std::string, std::string, uint32_t, uint32_t>;
  using OnPositionTrigger = Trigger<uint32_t, double, double, int32_t, uint32_t, float, float>;
  using OnTelemetryTrigger = Trigger<uint32_t, uint32_t, float, float, float, uint32_t>;
  void add_on_packet_trigger(OnPacketTrigger *t) { this->on_packet_triggers_.push_back(t); }
  void add_on_text_trigger(OnTextTrigger *t) { this->on_text_triggers_.push_back(t); }
  void add_on_nodeinfo_trigger(OnNodeInfoTrigger *t) { this->on_nodeinfo_triggers_.push_back(t); }
  void add_on_position_trigger(OnPositionTrigger *t) { this->on_position_triggers_.push_back(t); }
  void add_on_telemetry_trigger(OnTelemetryTrigger *t) { this->on_telemetry_triggers_.push_back(t); }

  void send_text(const std::string &text, uint32_t dest, const std::string &channel, bool want_ack);

  void handle_rx(const std::vector<uint8_t> &packet, float rssi, float snr);

#if defined(USE_SX126X) || defined(USE_SX127X)
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;
#endif

 protected:
  void maybe_relay_(const std::vector<uint8_t> &packet, const PacketHeader &h, float snr);
  void transmit_(const std::vector<uint8_t> &packet);
  void send_data_(uint32_t portnum, const uint8_t *payload, size_t payload_len, uint32_t dest, size_t channel_idx,
                  bool want_ack);
  void broadcast_node_info_();

  std::string long_name_;
  std::string short_name_;
  uint32_t node_num_{0};
  uint32_t role_{0};  // meshtastic_Config_DeviceConfig_Role_CLIENT
  uint8_t hop_limit_{3};
  uint32_t node_info_interval_{10800000};  // 3 hours
  uint32_t hw_model_{39};  // meshtastic_HardwareModel_DIY_V1
  std::vector<Channel> channels_;
  PacketDedup dedup_;
  NodeDb nodedb_;
  std::vector<OnPacketTrigger *> on_packet_triggers_;
  std::vector<OnTextTrigger *> on_text_triggers_;
  std::vector<OnNodeInfoTrigger *> on_nodeinfo_triggers_;
  std::vector<OnPositionTrigger *> on_position_triggers_;
  std::vector<OnTelemetryTrigger *> on_telemetry_triggers_;

#ifdef USE_SX126X
  sx126x::SX126x *sx126x_{nullptr};
#endif
#ifdef USE_SX127X
  sx127x::SX127x *sx127x_{nullptr};
#endif
};

}  // namespace meshtastic
}  // namespace esphome
