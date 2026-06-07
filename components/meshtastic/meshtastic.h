#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "channel.h"
#include "nodedb.h"
#include "protocol.h"
#include "router.h"
#include <map>
#include <string>
#include <vector>

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#if __has_include("esphome/components/sx126x/sx126x.h")
#include "esphome/components/sx126x/sx126x.h"
#define USE_SX126X 1
#endif
#if __has_include("esphome/components/sx127x/sx127x.h")
#include "esphome/components/sx127x/sx127x.h"
#define USE_SX127X 1
#endif

#if __has_include("esphome/components/socket/socket.h")
#include "esphome/components/socket/socket.h"
#include <memory>
#define USE_MESH_UDP 1
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
  void set_private_key(const std::vector<uint8_t> &key);
  void set_role(uint32_t role) { this->role_ = role; }
  void set_hop_limit(uint8_t hop_limit) { this->hop_limit_ = hop_limit; }
  void set_node_info_interval(uint32_t interval_ms) { this->node_info_interval_ = interval_ms; }
  void set_neighbor_info_interval(uint32_t interval_ms) { this->neighbor_info_interval_ = interval_ms; }
  void set_hw_model(uint32_t hw_model) { this->hw_model_ = hw_model; }
  void set_node_db_size(uint32_t size) { this->nodedb_.set_max_nodes(size); }
  void set_request_unknown_node_info(bool enable) { this->request_unknown_node_info_ = enable; }
  void set_persist_node_db(bool enable) { this->persist_node_db_ = enable; }
#ifdef USE_MESH_UDP
  void set_udp(const std::string &address, uint16_t port) {
    this->udp_address_ = address;
    this->udp_port_ = port;
    this->udp_enabled_ = true;
  }
#endif
  void add_channel(const std::string &name, const std::vector<uint8_t> &key, bool uplink, bool downlink);

#ifdef USE_SX126X
  void set_radio(sx126x::SX126x *radio);
#endif
#ifdef USE_SX127X
  void set_radio(sx127x::SX127x *radio);
#endif

#ifdef USE_SENSOR
  void set_nodes_online_sensor(sensor::Sensor *s) { this->nodes_online_sensor_ = s; }
  void set_nodes_known_sensor(sensor::Sensor *s) { this->nodes_known_sensor_ = s; }
  void set_neighbors_sensor(sensor::Sensor *s) { this->neighbors_sensor_ = s; }
  void set_last_rx_age_sensor(sensor::Sensor *s) { this->last_rx_age_sensor_ = s; }
  void set_rx_packets_sensor(sensor::Sensor *s) { this->rx_packets_sensor_ = s; }
  void set_tx_packets_sensor(sensor::Sensor *s) { this->tx_packets_sensor_ = s; }
  void set_relayed_packets_sensor(sensor::Sensor *s) { this->relayed_packets_sensor_ = s; }
  void set_dropped_duplicate_sensor(sensor::Sensor *s) { this->dropped_duplicate_sensor_ = s; }
  void set_no_key_sensor(sensor::Sensor *s) { this->no_key_sensor_ = s; }
  void set_decode_failed_sensor(sensor::Sensor *s) { this->decode_failed_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_node_id_text_sensor(text_sensor::TextSensor *s) { this->node_id_text_sensor_ = s; }
#endif

  using OnPacketTrigger = Trigger<uint32_t, uint32_t, uint32_t, std::vector<uint8_t>, float, float>;
  using OnTextTrigger = Trigger<uint32_t, uint32_t, std::string, std::string, float, float>;
  using OnNodeInfoTrigger = Trigger<uint32_t, std::string, meshtastic_User, float, float>;
  using OnPositionTrigger = Trigger<uint32_t, std::string, meshtastic_Position, float, float>;
  using OnTelemetryTrigger = Trigger<uint32_t, std::string, meshtastic_DeviceMetrics, float, float>;
  using OnEnvironmentTrigger = Trigger<uint32_t, std::string, meshtastic_EnvironmentMetrics, float, float>;
  using OnAirQualityTrigger = Trigger<uint32_t, std::string, meshtastic_AirQualityMetrics, float, float>;
  using OnPowerTrigger = Trigger<uint32_t, std::string, meshtastic_PowerMetrics, float, float>;
  using OnLocalStatsTrigger = Trigger<uint32_t, std::string, meshtastic_LocalStats, float, float>;
  using OnHealthTrigger = Trigger<uint32_t, std::string, meshtastic_HealthMetrics, float, float>;
  using OnTraceRouteResponseTrigger = Trigger<uint32_t, std::string, meshtastic_RouteDiscovery, float, float>;
  using OnNeighborInfoTrigger = Trigger<uint32_t, std::string, meshtastic_NeighborInfo, float, float>;
  using OnWaypointTrigger = Trigger<uint32_t, std::string, meshtastic_Waypoint, float, float>;
  using OnDetectionTrigger = Trigger<uint32_t, std::string, std::string, float, float>;
  using OnReplyTrigger = Trigger<uint32_t, std::string, std::string, float, float>;
  using OnRangeTestTrigger = Trigger<uint32_t, std::string, std::string, float, float>;
  using OnKeyVerificationTrigger = Trigger<uint32_t, std::string, meshtastic_KeyVerification, float, float>;
  void add_on_packet_trigger(OnPacketTrigger *t) { this->on_packet_triggers_.push_back(t); }
  void add_on_text_trigger(OnTextTrigger *t) { this->on_text_triggers_.push_back(t); }
  void add_on_nodeinfo_trigger(OnNodeInfoTrigger *t) { this->on_nodeinfo_triggers_.push_back(t); }
  void add_on_position_trigger(OnPositionTrigger *t) { this->on_position_triggers_.push_back(t); }
  void add_on_telemetry_trigger(OnTelemetryTrigger *t) { this->on_telemetry_triggers_.push_back(t); }
  void add_on_environment_trigger(OnEnvironmentTrigger *t) { this->on_environment_triggers_.push_back(t); }
  void add_on_air_quality_trigger(OnAirQualityTrigger *t) { this->on_air_quality_triggers_.push_back(t); }
  void add_on_power_trigger(OnPowerTrigger *t) { this->on_power_triggers_.push_back(t); }
  void add_on_local_stats_trigger(OnLocalStatsTrigger *t) { this->on_local_stats_triggers_.push_back(t); }
  void add_on_health_trigger(OnHealthTrigger *t) { this->on_health_triggers_.push_back(t); }
  void add_on_traceroute_response_trigger(OnTraceRouteResponseTrigger *t) {
    this->on_traceroute_response_triggers_.push_back(t);
  }
  void add_on_neighbor_info_trigger(OnNeighborInfoTrigger *t) { this->on_neighbor_info_triggers_.push_back(t); }
  void add_on_waypoint_trigger(OnWaypointTrigger *t) { this->on_waypoint_triggers_.push_back(t); }
  void add_on_detection_trigger(OnDetectionTrigger *t) { this->on_detection_triggers_.push_back(t); }
  void add_on_reply_trigger(OnReplyTrigger *t) { this->on_reply_triggers_.push_back(t); }
  void add_on_range_test_trigger(OnRangeTestTrigger *t) { this->on_range_test_triggers_.push_back(t); }
  void add_on_key_verification_trigger(OnKeyVerificationTrigger *t) {
    this->on_key_verification_triggers_.push_back(t);
  }

  void send_text(const std::string &text, uint32_t dest, const std::string &channel, bool want_ack);
  void send_detection(const std::string &text, const std::string &channel, bool want_ack);

  void send_position(double latitude, double longitude, int32_t altitude, uint32_t precision_bits,
                     const std::string &channel, bool want_ack);
  void send_device_metrics(const meshtastic_DeviceMetrics &metrics, const std::string &channel, bool want_ack);
  void send_environment_metrics(const meshtastic_EnvironmentMetrics &metrics, const std::string &channel,
                                bool want_ack);
  void send_node_info();
  void send_neighbor_info();
  void send_traceroute(uint32_t dest, const std::string &channel, bool want_ack);

  void handle_rx(const std::vector<uint8_t> &packet, float rssi, float snr);

#ifdef USE_MESH_UDP
  void loop() override;
#endif

#if defined(USE_SX126X) || defined(USE_SX127X)
  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;
#endif

 protected:
  void maybe_relay_(const std::vector<uint8_t> &packet, const PacketHeader &h, float snr);
  void relay_traceroute_(const PacketHeader &h, const meshtastic_Data &data_in, size_t channel_idx, float snr);
  void transmit_(const std::vector<uint8_t> &packet);
  void send_data_(uint32_t portnum, const uint8_t *payload, size_t payload_len, uint32_t dest, size_t channel_idx,
                  bool want_ack, uint32_t request_id = 0, bool want_response = false);
  void send_ack_(uint32_t to, uint32_t request_id, size_t channel_idx);
  void send_routing_error_(uint32_t to, uint32_t request_id, size_t channel_idx, uint32_t error);
  void track_want_ack_(uint32_t id, const std::vector<uint8_t> &packet);
  void clear_outstanding_(uint32_t id);
  void service_retransmits_();
  int find_channel_index_(const std::string &name);
  void send_telemetry_(const meshtastic_Telemetry &tel, size_t channel_idx, bool want_ack);
  void init_keypair_();
  void dispatch_decoded_(const meshtastic_Data &data, const PacketHeader &h, const std::string &channel_name,
                         float rssi, float snr);
  bool pkc_decode_(const PacketHeader &h, const uint8_t *cipher, size_t cipher_len, const uint8_t peer_public_key[32],
                   meshtastic_Data *out);
  void send_dm_(uint32_t dest, uint32_t portnum, const uint8_t *payload, size_t payload_len, bool want_ack,
                uint32_t request_id);
  void send_our_node_info_(uint32_t dest, size_t channel_idx, bool want_response);
  void request_node_info_(uint32_t dest);
  void maybe_request_node_info_(uint32_t node);  // rate-limited; only when request_unknown_node_info_ is set
  void load_nodedb_();
  void save_nodedb_();
#ifdef USE_MESH_UDP
  void udp_setup_();
  void udp_broadcast_(const std::vector<uint8_t> &frame);
#endif
  void queue_pending_dm_(uint32_t dest, const std::string &text, bool want_ack);
  void queue_pending_rx_(uint32_t from, const std::vector<uint8_t> &packet, float rssi, float snr);
  void flush_pending_dms_(uint32_t learned_node);
  void expire_pending_dms_();

  std::string long_name_;
  std::string short_name_;
  uint32_t node_num_{0};
  uint32_t role_{0};  // meshtastic_Config_DeviceConfig_Role_CLIENT
  uint8_t hop_limit_{3};
  uint32_t node_info_interval_{10800000};  // 3 hours
  uint32_t neighbor_info_interval_{0};     // 0 = disabled
  uint32_t hw_model_{39};                  // meshtastic_HardwareModel_DIY_V1
  uint8_t private_key_[32]{};
  uint8_t public_key_[32]{};
  bool has_keypair_{false};
  bool private_key_configured_{false};
  ESPPreferenceObject key_pref_;
  bool nodedb_dirty_{false};
  meshtastic_Position self_position_{};
  bool has_self_position_{false};
  meshtastic_DeviceMetrics self_metrics_{};
  bool has_self_metrics_{false};
  // One queue for both directions: a TX DM awaiting the destination's public key, or an RX DM we could
  // not decrypt yet awaiting the sender's public key
  struct PendingDm {
    uint32_t peer;
    uint32_t queued_at;
    bool is_rx;
    std::string text;             // TX
    bool want_ack;                // TX
    std::vector<uint8_t> packet;  // RX: full raw packet, re-decoded on retry
    float rssi;                   // RX
    float snr;                    // RX
  };
  std::vector<PendingDm> pending_dms_;
  // Our want_ack sends awaiting confirmation
  struct OutstandingTx {
    uint32_t id;
    std::vector<uint8_t> packet;
    uint8_t attempts;
    uint32_t next_ms;
  };
  std::vector<OutstandingTx> outstanding_tx_;
  std::vector<Channel> channels_;
  PacketDedup dedup_;
  NodeDb nodedb_;
  bool request_unknown_node_info_{false};
  bool persist_node_db_{true};
#ifdef USE_MESH_UDP
  std::unique_ptr<socket::Socket> udp_socket_;
  std::string udp_address_;
  uint16_t udp_port_{4403};
  bool udp_enabled_{false};
  uint32_t udp_last_setup_attempt_ms_{0};
#endif
  std::map<uint32_t, uint32_t> nodeinfo_requested_at_;
  uint32_t last_nodeinfo_request_ms_{0};
  std::vector<OnPacketTrigger *> on_packet_triggers_;
  std::vector<OnTextTrigger *> on_text_triggers_;
  std::vector<OnNodeInfoTrigger *> on_nodeinfo_triggers_;
  std::vector<OnPositionTrigger *> on_position_triggers_;
  std::vector<OnTelemetryTrigger *> on_telemetry_triggers_;
  std::vector<OnEnvironmentTrigger *> on_environment_triggers_;
  std::vector<OnAirQualityTrigger *> on_air_quality_triggers_;
  std::vector<OnPowerTrigger *> on_power_triggers_;
  std::vector<OnLocalStatsTrigger *> on_local_stats_triggers_;
  std::vector<OnHealthTrigger *> on_health_triggers_;
  std::vector<OnTraceRouteResponseTrigger *> on_traceroute_response_triggers_;
  std::vector<OnNeighborInfoTrigger *> on_neighbor_info_triggers_;
  std::vector<OnWaypointTrigger *> on_waypoint_triggers_;
  std::vector<OnDetectionTrigger *> on_detection_triggers_;
  std::vector<OnReplyTrigger *> on_reply_triggers_;
  std::vector<OnRangeTestTrigger *> on_range_test_triggers_;
  std::vector<OnKeyVerificationTrigger *> on_key_verification_triggers_;

  // Lifetime diagnostic counters (surfaced via the sensor platform).
  uint32_t rx_packets_{0};
  uint32_t tx_packets_{0};
  uint32_t relayed_packets_{0};
  uint32_t dropped_duplicate_{0};
  uint32_t no_key_packets_{0};
  uint32_t decode_failed_{0};
  uint32_t last_rx_ms_{0};
  bool had_rx_{false};

#ifdef USE_SENSOR
  void publish_stats_();
  sensor::Sensor *nodes_online_sensor_{nullptr};
  sensor::Sensor *nodes_known_sensor_{nullptr};
  sensor::Sensor *neighbors_sensor_{nullptr};
  sensor::Sensor *last_rx_age_sensor_{nullptr};
  sensor::Sensor *rx_packets_sensor_{nullptr};
  sensor::Sensor *tx_packets_sensor_{nullptr};
  sensor::Sensor *relayed_packets_sensor_{nullptr};
  sensor::Sensor *dropped_duplicate_sensor_{nullptr};
  sensor::Sensor *no_key_sensor_{nullptr};
  sensor::Sensor *decode_failed_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *node_id_text_sensor_{nullptr};
#endif

#ifdef USE_SX126X
  sx126x::SX126x *sx126x_{nullptr};
#endif
#ifdef USE_SX127X
  sx127x::SX127x *sx127x_{nullptr};
#endif
};

}  // namespace meshtastic
}  // namespace esphome
