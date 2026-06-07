#include "meshtastic.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include "crypto.h"
#include "enum_names.h"
#include "mesh.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace meshtastic {

static const char *const TAG = "meshtastic";

static const uint32_t NODE_ONLINE_WINDOW_MS = 2 * 60 * 60 * 1000;

void Meshtastic::add_channel(const std::string &name, const std::vector<uint8_t> &key, bool uplink, bool downlink) {
  Channel ch;
  ch.name = name;
  ch.key = key;
  ch.uplink = uplink;
  ch.downlink = downlink;
  ch.hash = channel_hash(name, key);
  this->channels_.push_back(ch);
}

void Meshtastic::set_private_key(const std::vector<uint8_t> &key) {
  if (key.size() != 32)
    return;
  memcpy(this->private_key_, key.data(), 32);
  this->private_key_configured_ = true;
}

void Meshtastic::init_keypair_() {
  if (!this->private_key_configured_) {
    struct PkcKey {
      uint8_t key[32];
    } kp{};
    this->key_pref_ = global_preferences->make_preference<PkcKey>(fnv1_hash("meshtastic_pkc") ^ this->node_num_);
    if (this->key_pref_.load(&kp)) {
      memcpy(this->private_key_, kp.key, 32);
    } else {
      random_bytes(this->private_key_, 32);
      memcpy(kp.key, this->private_key_, 32);
      this->key_pref_.save(&kp);
      ESP_LOGW(TAG, "Generated and persisted a new PKC keypair");
    }
  }
  // Public key = clamp(private) * basepoint(9).
  static const uint8_t basepoint[32] = {9};
  this->has_keypair_ = x25519_shared(this->private_key_, basepoint, this->public_key_);
}

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

  this->init_keypair_();

  if (this->node_info_interval_ > 0) {
    this->defer([this]() { this->send_node_info(); });
    this->set_interval(this->node_info_interval_, [this]() { this->send_node_info(); });
  }

  this->set_interval(15000, [this]() { this->expire_pending_dms_(); });
  this->set_interval(500, [this]() { this->service_retransmits_(); });

  if (this->neighbor_info_interval_ > 0)
    this->set_interval(this->neighbor_info_interval_, [this]() { this->send_neighbor_info(); });

#ifdef USE_TEXT_SENSOR
  if (this->node_id_text_sensor_ != nullptr)
    this->node_id_text_sensor_->publish_state(str_snprintf("!%08x", 9, this->node_num_));
#endif
#ifdef USE_SENSOR
  this->set_interval(30000, [this]() { this->publish_stats_(); });
#endif
}

#ifdef USE_SENSOR
void Meshtastic::publish_stats_() {
  const uint32_t now = millis();
  if (this->nodes_online_sensor_ != nullptr)
    this->nodes_online_sensor_->publish_state(this->nodedb_.count_active(now, NODE_ONLINE_WINDOW_MS));
  if (this->nodes_known_sensor_ != nullptr)
    this->nodes_known_sensor_->publish_state(this->nodedb_.size());
  if (this->neighbors_sensor_ != nullptr)
    this->neighbors_sensor_->publish_state(this->nodedb_.count_neighbors());
  if (this->last_rx_age_sensor_ != nullptr && this->had_rx_)
    this->last_rx_age_sensor_->publish_state((now - this->last_rx_ms_) / 1000.0f);
  if (this->rx_packets_sensor_ != nullptr)
    this->rx_packets_sensor_->publish_state(this->rx_packets_);
  if (this->tx_packets_sensor_ != nullptr)
    this->tx_packets_sensor_->publish_state(this->tx_packets_);
  if (this->relayed_packets_sensor_ != nullptr)
    this->relayed_packets_sensor_->publish_state(this->relayed_packets_);
  if (this->dropped_duplicate_sensor_ != nullptr)
    this->dropped_duplicate_sensor_->publish_state(this->dropped_duplicate_);
  if (this->no_key_sensor_ != nullptr)
    this->no_key_sensor_->publish_state(this->no_key_packets_);
  if (this->decode_failed_sensor_ != nullptr)
    this->decode_failed_sensor_->publish_state(this->decode_failed_);
}
#endif

void Meshtastic::dump_config() {
  ESP_LOGCONFIG(TAG, "Meshtastic:");
  ESP_LOGCONFIG(TAG, "  Node: !%08x \"%s\" (%s)", this->node_num_, this->long_name_.c_str(),
                this->short_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Role: %s  Hop limit: %u", role_name(this->role_), this->hop_limit_);
  if (role_is_deprecated(this->role_))
    ESP_LOGW(TAG, "Configured role '%s' is deprecated", role_name(this->role_));
  if (hardware_model_is_deprecated(this->hw_model_))
    ESP_LOGW(TAG, "Configured hw_model '%s' is deprecated", hardware_model_name(this->hw_model_));
  for (const auto &ch : this->channels_) {
    ESP_LOGCONFIG(TAG, "  Channel \"%s\": hash=0x%02x %s%s%s", ch.name.c_str(), ch.hash,
                  ch.has_crypto() ? "encrypted" : "cleartext", ch.uplink ? " uplink" : "",
                  ch.downlink ? " downlink" : "");
  }
  if (this->channels_.empty())
    ESP_LOGW(TAG, "No channels configured; received packets cannot be decoded");
  ESP_LOGCONFIG(TAG, "  PKC (direct messages): %s",
                this->has_keypair_ ? (this->private_key_configured_ ? "ready (configured key)" : "ready (generated key)")
                                   : "key derivation FAILED");
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
  this->handle_rx(packet, rssi, snr);
}
#endif

void Meshtastic::dispatch_decoded_(const meshtastic_Data &data, const PacketHeader &h, const std::string &channel_name,
                                   float rssi, float snr) {
  if (!this->on_packet_triggers_.empty()) {
    std::vector<uint8_t> payload(data.payload.bytes, data.payload.bytes + data.payload.size);
    for (auto *t : this->on_packet_triggers_)
      t->trigger(h.from, h.to, (uint32_t) data.portnum, payload, rssi, snr);
  }

  bool node_is_new = false;
  meshtastic_NodeInfoLite *node = nullptr;
  if (h.from != 0 && h.from != this->node_num_ && this->nodedb_.enabled()) {
    node = this->nodedb_.get_or_create(h.from, &node_is_new);
    node->num = h.from;
    node->snr = snr;
    node->last_heard = millis();
    node->has_hops_away = true;
    node->hops_away = (h.hop_start >= h.hop_limit) ? (h.hop_start - h.hop_limit) : 0;
  }

  if (this->request_unknown_node_info_ && node != nullptr && !node->has_user && h.from != this->node_num_ &&
      data.portnum != meshtastic_PortNum_NODEINFO_APP)
    this->maybe_request_node_info_(h.from);

  if (data.portnum == meshtastic_PortNum_NODEINFO_APP && h.from != 0 && h.from != this->node_num_) {
    meshtastic_User user = meshtastic_User_init_zero;
    pb_istream_t us = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&us, meshtastic_User_fields, &user)) {
      if (node != nullptr) {
        node->has_user = true;
        memcpy(node->user.long_name, user.long_name, sizeof(node->user.long_name));
        memcpy(node->user.short_name, user.short_name, sizeof(node->user.short_name));
        node->user.hw_model = user.hw_model;
        node->user.role = user.role;
        node->user.is_licensed = user.is_licensed;
        node->user.public_key.size = user.public_key.size;
        memcpy(node->user.public_key.bytes, user.public_key.bytes, user.public_key.size);
      }
      ESP_LOGD(TAG, "  node !%08x \"%s\" (%s) %s", h.from, user.long_name, user.short_name,
               node == nullptr ? "(db off)" : (node_is_new ? "NEW" : "updated"));
      for (auto *t : this->on_nodeinfo_triggers_)
        t->trigger(h.from, channel_name, user, rssi, snr);
      if (user.public_key.size == 32)
        this->flush_pending_dms_(h.from);
      if (data.want_response && h.to == this->node_num_) {
        const int ridx = this->find_channel_index_(channel_name);
        ESP_LOGD(TAG, "  NodeInfo requested by !%08x; replying", h.from);
        this->send_our_node_info_(h.from, ridx < 0 ? 0 : ridx, false);
      }
    }
  } else if (node != nullptr && node_is_new) {
    ESP_LOGD(TAG, "  node !%08x heard (awaiting NodeInfo) [%u known]", h.from, (unsigned) this->nodedb_.size());
  }

  if (data.portnum == meshtastic_PortNum_POSITION_APP && h.from != 0 && h.from != this->node_num_) {
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pb_istream_t ps = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&ps, meshtastic_Position_fields, &pos)) {
      if (node != nullptr) {
        node->has_position = true;
        node->position.latitude_i = pos.latitude_i;
        node->position.longitude_i = pos.longitude_i;
        node->position.altitude = pos.altitude;
        node->position.time = pos.time;
        node->position.location_source = pos.location_source;
      }
      const double lat = pos.latitude_i * 1e-7;
      const double lon = pos.longitude_i * 1e-7;
      ESP_LOGD(TAG, "  position !%08x %.6f, %.6f alt=%dm prec=%ubits", h.from, lat, lon, (int) pos.altitude,
               pos.precision_bits);
      for (auto *t : this->on_position_triggers_)
        t->trigger(h.from, channel_name, pos, rssi, snr);
      if (data.want_response && h.to == this->node_num_) {
        int ridx = this->find_channel_index_(channel_name);
        if (ridx < 0)
          ridx = 0;
        if (this->has_self_position_) {
          uint8_t rbuf[meshtastic_Position_size];
          pb_ostream_t ros = pb_ostream_from_buffer(rbuf, sizeof(rbuf));
          if (pb_encode(&ros, meshtastic_Position_fields, &this->self_position_)) {
            ESP_LOGD(TAG, "  position requested by !%08x; replying", h.from);
            this->send_data_(meshtastic_PortNum_POSITION_APP, rbuf, ros.bytes_written, h.from, ridx, false, h.id);
          }
        } else {
          this->send_routing_error_(h.from, h.id, ridx, meshtastic_Routing_Error_NO_RESPONSE);
        }
      }
    }
  }

  if (data.portnum == meshtastic_PortNum_TELEMETRY_APP && h.from != 0 && h.from != this->node_num_) {
    meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
    pb_istream_t ts = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&ts, meshtastic_Telemetry_fields, &tel)) {
      switch (tel.which_variant) {
        case meshtastic_Telemetry_device_metrics_tag: {
          const meshtastic_DeviceMetrics &dm = tel.variant.device_metrics;
          if (node != nullptr) {
            node->has_device_metrics = true;
            node->device_metrics = dm;
          }
          ESP_LOGD(TAG, "  telemetry !%08x batt=%u%% %.2fV chUtil=%.1f%% airTx=%.1f%%", h.from, dm.battery_level,
                   dm.voltage, dm.channel_utilization, dm.air_util_tx);
          for (auto *t : this->on_telemetry_triggers_)
            t->trigger(h.from, channel_name, dm, rssi, snr);
          break;
        }
        case meshtastic_Telemetry_environment_metrics_tag: {
          const meshtastic_EnvironmentMetrics &em = tel.variant.environment_metrics;
          ESP_LOGD(TAG, "  env telemetry !%08x temp=%.1f hum=%.0f%% press=%.1f", h.from,
                   em.has_temperature ? em.temperature : NAN, em.has_relative_humidity ? em.relative_humidity : NAN,
                   em.has_barometric_pressure ? em.barometric_pressure : NAN);
          for (auto *t : this->on_environment_triggers_)
            t->trigger(h.from, channel_name, em, rssi, snr);
          break;
        }
        case meshtastic_Telemetry_air_quality_metrics_tag: {
          ESP_LOGD(TAG, "  air-quality telemetry !%08x", h.from);
          for (auto *t : this->on_air_quality_triggers_)
            t->trigger(h.from, channel_name, tel.variant.air_quality_metrics, rssi, snr);
          break;
        }
        case meshtastic_Telemetry_power_metrics_tag: {
          ESP_LOGD(TAG, "  power telemetry !%08x", h.from);
          for (auto *t : this->on_power_triggers_)
            t->trigger(h.from, channel_name, tel.variant.power_metrics, rssi, snr);
          break;
        }
        case meshtastic_Telemetry_local_stats_tag: {
          ESP_LOGD(TAG, "  local-stats telemetry !%08x", h.from);
          for (auto *t : this->on_local_stats_triggers_)
            t->trigger(h.from, channel_name, tel.variant.local_stats, rssi, snr);
          break;
        }
        case meshtastic_Telemetry_health_metrics_tag: {
          ESP_LOGD(TAG, "  health telemetry !%08x", h.from);
          for (auto *t : this->on_health_triggers_)
            t->trigger(h.from, channel_name, tel.variant.health_metrics, rssi, snr);
          break;
        }
        default:
          ESP_LOGD(TAG, "  telemetry !%08x variant=%d (unhandled)", h.from, (int) tel.which_variant);
          break;
      }
      if (data.want_response && h.to == this->node_num_) {
        int ridx = this->find_channel_index_(channel_name);
        if (ridx < 0)
          ridx = 0;
        if (this->has_self_metrics_) {
          meshtastic_Telemetry rt = meshtastic_Telemetry_init_zero;
          rt.which_variant = meshtastic_Telemetry_device_metrics_tag;
          rt.variant.device_metrics = this->self_metrics_;
          uint8_t rbuf[meshtastic_Telemetry_size];
          pb_ostream_t ros = pb_ostream_from_buffer(rbuf, sizeof(rbuf));
          if (pb_encode(&ros, meshtastic_Telemetry_fields, &rt)) {
            ESP_LOGD(TAG, "  telemetry requested by !%08x; replying", h.from);
            this->send_data_(meshtastic_PortNum_TELEMETRY_APP, rbuf, ros.bytes_written, h.from, ridx, false, h.id);
          }
        } else {
          this->send_routing_error_(h.from, h.id, ridx, meshtastic_Routing_Error_NO_RESPONSE);
        }
      }
    }
  }

  // Routing: an ack (error NONE) or nak for one of our want_ack sends, correlated by request_id
  if (data.portnum == meshtastic_PortNum_ROUTING_APP) {
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    pb_istream_t rstream = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&rstream, meshtastic_Routing_fields, &routing) &&
        routing.which_variant == meshtastic_Routing_error_reason_tag) {
      if (routing.error_reason == meshtastic_Routing_Error_NONE)
        ESP_LOGD(TAG, "  ACK from !%08x for id=0x%08x", h.from, data.request_id);
      else
        ESP_LOGD(TAG, "  NAK from !%08x for id=0x%08x error=%d", h.from, data.request_id,
                 (int) routing.error_reason);
      this->clear_outstanding_(data.request_id);
    }
  }

  // Traceroute addressed to us: append our RX SNR (dB*4, int8) for the final hop and reply
  if (data.portnum == meshtastic_PortNum_TRACEROUTE_APP && data.want_response && h.to == this->node_num_ &&
      h.from != 0 && h.from != this->node_num_) {
    meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
    pb_istream_t rs = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&rs, meshtastic_RouteDiscovery_fields, &rd)) {
      if (rd.snr_towards_count < (pb_size_t) (sizeof(rd.snr_towards) / sizeof(rd.snr_towards[0]))) {
        int v = (int) lroundf(snr * 4.0f);
        v = v > 127 ? 127 : (v < -128 ? -128 : v);
        rd.snr_towards[rd.snr_towards_count++] = (int8_t) v;
      }
      int ridx = this->find_channel_index_(channel_name);
      if (ridx < 0)
        ridx = 0;
      uint8_t rbuf[meshtastic_RouteDiscovery_size];
      pb_ostream_t ros = pb_ostream_from_buffer(rbuf, sizeof(rbuf));
      if (pb_encode(&ros, meshtastic_RouteDiscovery_fields, &rd)) {
        ESP_LOGD(TAG, "  traceroute requested by !%08x; replying", h.from);
        this->send_data_(meshtastic_PortNum_TRACEROUTE_APP, rbuf, ros.bytes_written, h.from, ridx, false, h.id);
      }
    }
  }

  // Traceroute reply to a traceroute we initiated (meshtastic.traceroute): log the discovered path.
  if (data.portnum == meshtastic_PortNum_TRACEROUTE_APP && !data.want_response && h.to == this->node_num_) {
    meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
    pb_istream_t rs = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&rs, meshtastic_RouteDiscovery_fields, &rd)) {
      ESP_LOGI(TAG, "Traceroute to !%08x: %u hops out, %u back", h.from, (unsigned) rd.route_count,
               (unsigned) rd.route_back_count);
      for (pb_size_t i = 0; i < rd.route_count; i++)
        ESP_LOGI(TAG, "  out[%u] !%08x snr=%.2f", (unsigned) i, rd.route[i],
                 i < rd.snr_towards_count ? rd.snr_towards[i] / 4.0f : NAN);
      for (pb_size_t i = 0; i < rd.route_back_count; i++)
        ESP_LOGI(TAG, "  back[%u] !%08x snr=%.2f", (unsigned) i, rd.route_back[i],
                 i < rd.snr_back_count ? rd.snr_back[i] / 4.0f : NAN);
      for (auto *t : this->on_traceroute_response_triggers_)
        t->trigger(h.from, channel_name, rd, rssi, snr);
    }
  }

  // Remote admin is not supported: never act on AdminMessage; deny directed requests, never crash (P7).
  if (data.portnum == meshtastic_PortNum_ADMIN_APP && h.from != 0 && h.from != this->node_num_) {
    ESP_LOGW(TAG, "  admin request from !%08x denied (remote admin not supported)", h.from);
    if (data.want_response && h.to == this->node_num_) {
      int ridx = this->find_channel_index_(channel_name);
      if (ridx < 0)
        ridx = 0;
      this->send_routing_error_(h.from, h.id, ridx, meshtastic_Routing_Error_NOT_AUTHORIZED);
    }
  }

  if (data.portnum == meshtastic_PortNum_NEIGHBORINFO_APP && h.from != 0 && h.from != this->node_num_) {
    meshtastic_NeighborInfo ni = meshtastic_NeighborInfo_init_zero;
    pb_istream_t ns = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&ns, meshtastic_NeighborInfo_fields, &ni)) {
      ESP_LOGD(TAG, "  neighborinfo !%08x (%u neighbors)", h.from, (unsigned) ni.neighbors_count);
      for (auto *t : this->on_neighbor_info_triggers_)
        t->trigger(h.from, channel_name, ni, rssi, snr);
    }
  }

  if (data.portnum == meshtastic_PortNum_WAYPOINT_APP && h.from != 0 && h.from != this->node_num_) {
    meshtastic_Waypoint wp = meshtastic_Waypoint_init_zero;
    pb_istream_t ws = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&ws, meshtastic_Waypoint_fields, &wp)) {
      ESP_LOGD(TAG, "  waypoint !%08x id=%u \"%s\"", h.from, wp.id, wp.name);
      for (auto *t : this->on_waypoint_triggers_)
        t->trigger(h.from, channel_name, wp, rssi, snr);
    }
  }

  if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
    std::string text((const char *) data.payload.bytes, data.payload.size);
    ESP_LOGD(TAG, "  text: %s", text.c_str());
    for (auto *t : this->on_text_triggers_)
      t->trigger(h.from, h.to, channel_name, text, rssi, snr);
  }

  // Compressed text (unishox2) is rare on-air (mainline firmware no longer auto-compresses). We don't carry
  // the unishox2 codec, so surface it rather than silently dropping
  if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    ESP_LOGW(TAG, "  compressed text from !%08x not decoded (unishox2 unsupported)", h.from);
}

void Meshtastic::handle_rx(const std::vector<uint8_t> &packet, float rssi, float snr) {
  PacketHeader h;
  if (!parse_header(packet, h)) {
    ESP_LOGW(TAG, "Dropping runt packet: %zu bytes", packet.size());
    return;
  }
  ESP_LOGD(TAG, "RX %zuB rssi=%.0f snr=%.1f from=!%08x to=!%08x id=0x%08x ch=0x%02x hop=%u/%u%s%s", packet.size(),
           rssi, snr, h.from, h.to, h.id, h.channel, h.hop_limit, h.hop_start, h.want_ack ? " ack" : "",
           h.via_mqtt ? " mqtt" : "");
  this->rx_packets_++;
  this->last_rx_ms_ = millis();
  this->had_rx_ = true;

  if (this->dedup_.is_duplicate(h.from, h.id, millis())) {
    ESP_LOGV(TAG, "  duplicate, ignoring");
    this->dropped_duplicate_++;
    if (h.from == this->node_num_)
      this->clear_outstanding_(h.id);
    return;
  }

  const uint8_t *cipher = packet.data() + MESHTASTIC_HEADER_LEN;
  const size_t cipher_len = packet.size() - MESHTASTIC_HEADER_LEN;

  // PKC direct message: channel hash 0, addressed to us (not broadcast). Decrypt with the sender's
  // public key (learned from a prior NodeInfo). If we don't have it yet, buffer the packet and request the
  // key
  if (h.channel == 0 && h.to == this->node_num_ && h.from != 0 && h.from != this->node_num_ && this->has_keypair_ &&
      cipher_len > 12) {
    meshtastic_NodeInfoLite *peer = this->nodedb_.find(h.from);
    if (peer != nullptr && peer->has_user && peer->user.public_key.size == 32) {
      meshtastic_Data data = meshtastic_Data_init_zero;
      if (this->pkc_decode_(h, cipher, cipher_len, peer->user.public_key.bytes, &data)) {
        ESP_LOGD(TAG, "  PKC DM from !%08x: portnum=%d payload=%uB", h.from, (int) data.portnum,
                 (unsigned) data.payload.size);
        this->dispatch_decoded_(data, h, "", rssi, snr);
        return;
      }
      ESP_LOGD(TAG, "  PKC DM from !%08x failed to decrypt", h.from);
      this->decode_failed_++;
      return;
    } else {
      ESP_LOGD(TAG, "  PKC DM from !%08x but its public key is unknown; buffering + requesting NodeInfo", h.from);
      this->queue_pending_rx_(h.from, packet, rssi, snr);
      this->request_node_info_(h.from);
      return;
    }
  }

  // Try each channel whose hash matches; decode the first that yields a valid Data.
  meshtastic_Data data = meshtastic_Data_init_zero;
  bool decoded = false;
  size_t dec_ci = 0;
  bool hash_matched = false;
  for (size_t ci = 0; ci < this->channels_.size(); ci++) {
    Channel &ch = this->channels_[ci];
    if (ch.hash != h.channel)
      continue;
    hash_matched = true;
    std::vector<uint8_t> plain(cipher_len);
    if (!ch.crypt(h.from, h.id, cipher, cipher_len, plain.data()))
      continue;
    meshtastic_Data d = meshtastic_Data_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(plain.data(), plain.size());
    if (!pb_decode(&stream, meshtastic_Data_fields, &d))
      continue;
    ESP_LOGD(TAG, "  decoded on \"%s\": portnum=%d payload=%uB", ch.name.c_str(), (int) d.portnum,
             (unsigned) d.payload.size);
    this->dispatch_decoded_(d, h, ch.name, rssi, snr);
    if (h.want_ack && h.to == this->node_num_ && h.from != 0 && h.from != this->node_num_)
      this->send_ack_(h.from, h.id, ci);
    data = d;
    decoded = true;
    dec_ci = ci;
    break;
  }
  if (!decoded) {
    if (hash_matched) {
      ESP_LOGD(TAG, "  channel hash 0x%02x matched but decode failed", h.channel);
      this->decode_failed_++;
    } else {
      ESP_LOGV(TAG, "  no key for channel hash 0x%02x", h.channel);
      this->no_key_packets_++;
    }
  }

  if (decoded && data.portnum == meshtastic_PortNum_TRACEROUTE_APP) {
    this->relay_traceroute_(h, data, dec_ci, snr);
  } else {
    this->maybe_relay_(packet, h, snr);
  }
}

void Meshtastic::maybe_relay_(const std::vector<uint8_t> &packet, const PacketHeader &h, float snr) {
  if (h.from == this->node_num_)  // our own packet
    return;
  if (h.to == this->node_num_)  // we are the destination
    return;
  if (h.hop_limit == 0)  // expired
    return;
  if (!role_rebroadcasts(this->role_))
    return;

  std::vector<uint8_t> tx = packet;
  PacketHeader rh = h;
  rh.hop_limit = h.hop_limit - 1;
  rh.next_hop = 0;
  rh.relay_node = (uint8_t) (this->node_num_ & 0xFF);
  serialize_header(rh, tx.data());

  const uint32_t delay = rebroadcast_delay_ms(this->role_, snr);
  ESP_LOGD(TAG, "  relaying in %ums (hop %u->%u)", delay, h.hop_limit, rh.hop_limit);
  this->relayed_packets_++;
  this->set_timeout(delay, [this, tx]() { this->transmit_(tx); });
}

void Meshtastic::relay_traceroute_(const PacketHeader &h, const meshtastic_Data &data_in, size_t channel_idx,
                                   float snr) {
  if (h.from == this->node_num_ || h.to == this->node_num_ || h.hop_limit == 0 ||
      !role_rebroadcasts(this->role_) || channel_idx >= this->channels_.size())
    return;

  // Insert ourselves into the RouteDiscovery. Firmware: request_id==0 => travelling towards the destination
  // (route/snr_towards); non-zero => the response travelling back (route_back/snr_back).
  meshtastic_Data data = data_in;
  bool appended = false;
  meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
  pb_istream_t rs = pb_istream_from_buffer(data_in.payload.bytes, data_in.payload.size);
  if (pb_decode(&rs, meshtastic_RouteDiscovery_fields, &rd)) {
    int v = (int) lroundf(snr * 4.0f);
    v = v > 127 ? 127 : (v < -128 ? -128 : v);
    const pb_size_t cap = (pb_size_t) (sizeof(rd.route) / sizeof(rd.route[0]));
    if (data_in.request_id == 0) {
      if (rd.route_count < cap)
        rd.route[rd.route_count++] = this->node_num_;
      if (rd.snr_towards_count < cap)
        rd.snr_towards[rd.snr_towards_count++] = (int8_t) v;
    } else {
      if (rd.route_back_count < cap)
        rd.route_back[rd.route_back_count++] = this->node_num_;
      if (rd.snr_back_count < cap)
        rd.snr_back[rd.snr_back_count++] = (int8_t) v;
    }
    uint8_t rdbuf[meshtastic_RouteDiscovery_size];
    pb_ostream_t ros = pb_ostream_from_buffer(rdbuf, sizeof(rdbuf));
    if (pb_encode(&ros, meshtastic_RouteDiscovery_fields, &rd) && ros.bytes_written <= sizeof(data.payload.bytes)) {
      memcpy(data.payload.bytes, rdbuf, ros.bytes_written);
      data.payload.size = ros.bytes_written;
      appended = true;
    }
  }

  // Re-encode the Data and re-encrypt on the original channel (nonce keeps the original from/id).
  uint8_t databuf[256];
  pb_ostream_t dos = pb_ostream_from_buffer(databuf, sizeof(databuf));
  if (!pb_encode(&dos, meshtastic_Data_fields, &data))
    return;
  Channel &ch = this->channels_[channel_idx];
  std::vector<uint8_t> tx(MESHTASTIC_HEADER_LEN + dos.bytes_written);
  if (!ch.crypt(h.from, h.id, databuf, dos.bytes_written, tx.data() + MESHTASTIC_HEADER_LEN))
    return;
  PacketHeader rh = h;
  rh.hop_limit = h.hop_limit - 1;
  rh.next_hop = 0;
  rh.relay_node = (uint8_t) (this->node_num_ & 0xFF);
  serialize_header(rh, tx.data());

  const uint32_t delay = rebroadcast_delay_ms(this->role_, snr);
  ESP_LOGD(TAG, "  relaying traceroute%s in %ums (hop %u->%u)", appended ? " (+self)" : "", delay, h.hop_limit,
           rh.hop_limit);
  this->relayed_packets_++;
  this->set_timeout(delay, [this, tx]() { this->transmit_(tx); });
}

void Meshtastic::transmit_(const std::vector<uint8_t> &packet) {
  this->tx_packets_++;
#ifdef USE_SX126X
  if (this->sx126x_ != nullptr) {
    this->sx126x_->transmit_packet(packet);
    return;
  }
#endif
#ifdef USE_SX127X
  if (this->sx127x_ != nullptr) {
    this->sx127x_->transmit_packet(packet);
    return;
  }
#endif
}

void Meshtastic::send_data_(uint32_t portnum, const uint8_t *payload, size_t payload_len, uint32_t dest,
                            size_t channel_idx, bool want_ack, uint32_t request_id, bool want_response) {
  if (channel_idx >= this->channels_.size())
    return;
  Channel &ch = this->channels_[channel_idx];

  meshtastic_Data data = meshtastic_Data_init_zero;
  data.portnum = (meshtastic_PortNum) portnum;
  data.request_id = request_id;
  data.want_response = want_response;
  if (payload_len > sizeof(data.payload.bytes))
    return;
  data.payload.size = payload_len;
  memcpy(data.payload.bytes, payload, payload_len);

  uint8_t databuf[256];
  pb_ostream_t os = pb_ostream_from_buffer(databuf, sizeof(databuf));
  if (!pb_encode(&os, meshtastic_Data_fields, &data)) {
    ESP_LOGW(TAG, "Data encode failed");
    return;
  }

  uint32_t id = random_uint32();
  if (id == 0)
    id = 1;

  std::vector<uint8_t> packet(MESHTASTIC_HEADER_LEN + os.bytes_written);
  if (!ch.crypt(this->node_num_, id, databuf, os.bytes_written, packet.data() + MESHTASTIC_HEADER_LEN))
    return;

  PacketHeader h{};
  h.to = dest;
  h.from = this->node_num_;
  h.id = id;
  h.hop_limit = this->hop_limit_;
  h.want_ack = want_ack;
  h.hop_start = this->hop_limit_;
  h.channel = ch.hash;
  serialize_header(h, packet.data());

  this->dedup_.is_duplicate(this->node_num_, id, millis());  // remember our own packet
  ESP_LOGD(TAG, "TX portnum=%u to=!%08x id=0x%08x %uB", portnum, dest, id, (unsigned) packet.size());
  this->transmit_(packet);
  if (want_ack)
    this->track_want_ack_(id, packet);
}

void Meshtastic::send_ack_(uint32_t to, uint32_t request_id, size_t channel_idx) {
  this->send_routing_error_(to, request_id, channel_idx, meshtastic_Routing_Error_NONE);
}

void Meshtastic::send_routing_error_(uint32_t to, uint32_t request_id, size_t channel_idx, uint32_t error) {
  meshtastic_Routing routing = meshtastic_Routing_init_zero;
  routing.which_variant = meshtastic_Routing_error_reason_tag;
  routing.error_reason = (meshtastic_Routing_Error) error;

  uint8_t buf[meshtastic_Routing_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_Routing_fields, &routing)) {
    ESP_LOGW(TAG, "Routing encode failed");
    return;
  }
  ESP_LOGD(TAG, "Routing to=!%08x for id=0x%08x error=%d", to, request_id, error);
  this->send_data_(meshtastic_PortNum_ROUTING_APP, buf, os.bytes_written, to, channel_idx, false, request_id);
}

// Reliable-delivery tuning. RETX_BASE_MS is approximate: exact time-on-air-based backoff needs the radio
// modem getters that ESPHome's sx126x/sx127x don't expose yet (same blocker as channel/air utilization).
static const uint32_t RETX_BASE_MS = 5000;
static const uint8_t MAX_RETX = 3;
static const size_t MAX_OUTSTANDING = 8;

void Meshtastic::track_want_ack_(uint32_t id, const std::vector<uint8_t> &packet) {
  if (this->outstanding_tx_.size() >= MAX_OUTSTANDING)
    this->outstanding_tx_.erase(this->outstanding_tx_.begin());  // drop oldest
  OutstandingTx o;
  o.id = id;
  o.packet = packet;
  o.attempts = 0;
  o.next_ms = millis() + RETX_BASE_MS;
  this->outstanding_tx_.push_back(std::move(o));
}

void Meshtastic::clear_outstanding_(uint32_t id) {
  for (size_t i = 0; i < this->outstanding_tx_.size(); i++) {
    if (this->outstanding_tx_[i].id == id) {
      ESP_LOGD(TAG, "want_ack id=0x%08x confirmed", id);
      this->outstanding_tx_.erase(this->outstanding_tx_.begin() + i);
      return;
    }
  }
}

void Meshtastic::service_retransmits_() {
  const uint32_t now = millis();
  for (size_t i = 0; i < this->outstanding_tx_.size();) {
    OutstandingTx &o = this->outstanding_tx_[i];
    if (now < o.next_ms) {
      i++;
      continue;
    }
    if (o.attempts >= MAX_RETX) {
      ESP_LOGW(TAG, "want_ack id=0x%08x: no ack after %u retransmits, giving up", o.id, o.attempts);
      this->outstanding_tx_.erase(this->outstanding_tx_.begin() + i);
      continue;
    }
    o.attempts++;
    ESP_LOGD(TAG, "want_ack id=0x%08x: retransmit %u/%u", o.id, o.attempts, MAX_RETX);
    this->transmit_(o.packet);
    o.next_ms = now + RETX_BASE_MS * o.attempts;
    i++;
  }
}

int Meshtastic::find_channel_index_(const std::string &name) {
  if (name.empty())
    return 0;  // primary / first channel
  for (size_t i = 0; i < this->channels_.size(); i++) {
    if (this->channels_[i].name == name)
      return (int) i;
  }
  return -1;
}

void Meshtastic::send_text(const std::string &text, uint32_t dest, const std::string &channel, bool want_ack) {
  if (dest != MESHTASTIC_BROADCAST_ADDR && dest != 0 && this->has_keypair_) {
    meshtastic_NodeInfoLite *peer = this->nodedb_.find(dest);
    if (peer != nullptr && peer->has_user && peer->user.public_key.size == 32) {
      this->send_dm_(dest, meshtastic_PortNum_TEXT_MESSAGE_APP, (const uint8_t *) text.data(), text.size(), want_ack, 0);
    } else {
      this->queue_pending_dm_(dest, text, want_ack);
      this->request_node_info_(dest);
    }
    return;
  }
  const int idx = this->find_channel_index_(channel);
  if (idx < 0) {
    ESP_LOGW(TAG, "send_text: unknown channel \"%s\"", channel.c_str());
    return;
  }
  this->send_data_(meshtastic_PortNum_TEXT_MESSAGE_APP, (const uint8_t *) text.data(), text.size(), dest, idx, want_ack);
}

// Meshtastic PKC nonce (13 bytes): packet id (4 LE) | extra nonce (4 LE) | from-node (4 LE) | 0.
static void build_pkc_nonce_(uint8_t nonce[13], uint32_t id, uint32_t from, uint32_t extra) {
  memset(nonce, 0, 13);
  memcpy(nonce, &id, 4);
  memcpy(nonce + 4, &extra, 4);
  memcpy(nonce + 8, &from, 4);
}

bool Meshtastic::pkc_decode_(const PacketHeader &h, const uint8_t *cipher, size_t cipher_len,
                             const uint8_t peer_public_key[32], meshtastic_Data *out) {
  // On-wire DM payload = ciphertext | tag(8) | extra_nonce(4).
  const size_t ct_len = cipher_len - 12;
  const uint8_t *tag = cipher + ct_len;
  uint32_t extra_nonce;
  memcpy(&extra_nonce, cipher + ct_len + 8, 4);

  uint8_t shared[32];
  if (!x25519_shared(this->private_key_, peer_public_key, shared))
    return false;
  uint8_t key[32];
  sha256_hash(shared, 32, key);

  uint8_t nonce[13];
  build_pkc_nonce_(nonce, h.id, h.from, extra_nonce);  // sender's id + from + their extra nonce

  std::vector<uint8_t> plain(ct_len);
  if (!aes_ccm_decrypt(key, nonce, 13, cipher, ct_len, tag, 8, plain.data()))
    return false;
  pb_istream_t s = pb_istream_from_buffer(plain.data(), ct_len);
  return pb_decode(&s, meshtastic_Data_fields, out);
}

void Meshtastic::send_dm_(uint32_t dest, uint32_t portnum, const uint8_t *payload, size_t payload_len, bool want_ack,
                          uint32_t request_id) {
  meshtastic_NodeInfoLite *peer = this->nodedb_.find(dest);
  if (peer == nullptr || !peer->has_user || peer->user.public_key.size != 32) {
    ESP_LOGW(TAG, "send_dm: no public key for !%08x", dest);
    return;
  }
  meshtastic_Data data = meshtastic_Data_init_zero;
  data.portnum = (meshtastic_PortNum) portnum;
  data.request_id = request_id;
  if (payload_len > sizeof(data.payload.bytes))
    return;
  data.payload.size = payload_len;
  memcpy(data.payload.bytes, payload, payload_len);
  uint8_t databuf[256];
  pb_ostream_t os = pb_ostream_from_buffer(databuf, sizeof(databuf));
  if (!pb_encode(&os, meshtastic_Data_fields, &data)) {
    ESP_LOGW(TAG, "DM Data encode failed");
    return;
  }

  uint8_t shared[32];
  if (!x25519_shared(this->private_key_, peer->user.public_key.bytes, shared))
    return;
  uint8_t key[32];
  sha256_hash(shared, 32, key);
  uint32_t id = random_uint32();
  if (id == 0)
    id = 1;
  uint32_t extra_nonce = random_uint32();
  uint8_t nonce[13];
  build_pkc_nonce_(nonce, id, this->node_num_, extra_nonce);

  // On-wire payload = ciphertext | tag(8) | extra_nonce(4).
  std::vector<uint8_t> packet(MESHTASTIC_HEADER_LEN + os.bytes_written + 12);
  uint8_t *ct = packet.data() + MESHTASTIC_HEADER_LEN;
  uint8_t tag[8];
  if (!aes_ccm_encrypt(key, nonce, 13, databuf, os.bytes_written, ct, tag, 8)) {
    ESP_LOGW(TAG, "DM encrypt failed");
    return;
  }
  memcpy(ct + os.bytes_written, tag, 8);
  memcpy(ct + os.bytes_written + 8, &extra_nonce, 4);

  PacketHeader hh{};
  hh.to = dest;
  hh.from = this->node_num_;
  hh.id = id;
  hh.hop_limit = this->hop_limit_;
  hh.hop_start = this->hop_limit_;
  hh.want_ack = want_ack;
  hh.channel = 0;  // channel hash 0 marks a PKC direct message
  serialize_header(hh, packet.data());

  this->dedup_.is_duplicate(this->node_num_, id, millis());  // remember our own packet
  ESP_LOGD(TAG, "TX PKC DM to=!%08x id=0x%08x %uB", dest, id, (unsigned) packet.size());
  this->transmit_(packet);
  if (want_ack)
    this->track_want_ack_(id, packet);
}

void Meshtastic::send_our_node_info_(uint32_t dest, size_t channel_idx, bool want_response) {
  meshtastic_User user = meshtastic_User_init_zero;
  snprintf(user.id, sizeof(user.id), "!%08x", this->node_num_);
  strncpy(user.long_name, this->long_name_.c_str(), sizeof(user.long_name) - 1);
  strncpy(user.short_name, this->short_name_.c_str(), sizeof(user.short_name) - 1);
  user.hw_model = (meshtastic_HardwareModel) this->hw_model_;
  user.role = (meshtastic_Config_DeviceConfig_Role) this->role_;
  if (this->has_keypair_) {
    user.public_key.size = sizeof(this->public_key_);
    memcpy(user.public_key.bytes, this->public_key_, sizeof(this->public_key_));
  }
  uint8_t buf[meshtastic_User_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_User_fields, &user)) {
    ESP_LOGW(TAG, "User encode failed");
    return;
  }
  this->send_data_(meshtastic_PortNum_NODEINFO_APP, buf, os.bytes_written, dest, channel_idx, false, 0, want_response);
}

void Meshtastic::request_node_info_(uint32_t dest) {
  ESP_LOGD(TAG, "Requesting NodeInfo from !%08x", dest);
  this->send_our_node_info_(dest, 0, true);
}

// Rate-limit auto NodeInfo requests so we stay a good mesh citizen: a per-node cooldown plus a global
// minimum spacing between any two requests.
static const uint32_t NI_REQUEST_COOLDOWN_MS = 15 * 60 * 1000;
static const uint32_t NI_REQUEST_SPACING_MS = 5000;
static const size_t NI_REQUEST_MAP_CAP = 64;

void Meshtastic::maybe_request_node_info_(uint32_t node) {
  const uint32_t now = millis();
  if (this->last_nodeinfo_request_ms_ != 0 && now - this->last_nodeinfo_request_ms_ < NI_REQUEST_SPACING_MS)
    return;
  auto it = this->nodeinfo_requested_at_.find(node);
  if (it != this->nodeinfo_requested_at_.end() && now - it->second < NI_REQUEST_COOLDOWN_MS)
    return;
  if (this->nodeinfo_requested_at_.size() >= NI_REQUEST_MAP_CAP)
    this->nodeinfo_requested_at_.clear();
  this->nodeinfo_requested_at_[node] = now;
  this->last_nodeinfo_request_ms_ = now;
  this->request_node_info_(node);
}

static const size_t MAX_PENDING_DMS = 8;

void Meshtastic::queue_pending_dm_(uint32_t dest, const std::string &text, bool want_ack) {
  if (this->pending_dms_.size() >= MAX_PENDING_DMS)
    this->pending_dms_.erase(this->pending_dms_.begin());
  PendingDm dm{};
  dm.peer = dest;
  dm.queued_at = millis();
  dm.is_rx = false;
  dm.text = text;
  dm.want_ack = want_ack;
  this->pending_dms_.push_back(std::move(dm));
  ESP_LOGD(TAG, "Queued DM for !%08x (awaiting its public key)", dest);
}

void Meshtastic::queue_pending_rx_(uint32_t from, const std::vector<uint8_t> &packet, float rssi, float snr) {
  if (this->pending_dms_.size() >= MAX_PENDING_DMS)
    this->pending_dms_.erase(this->pending_dms_.begin());  // drop oldest
  PendingDm dm{};
  dm.peer = from;
  dm.queued_at = millis();
  dm.is_rx = true;
  dm.packet = packet;
  dm.rssi = rssi;
  dm.snr = snr;
  this->pending_dms_.push_back(std::move(dm));
  ESP_LOGD(TAG, "Buffered an undecryptable DM from !%08x (awaiting its public key)", from);
}

void Meshtastic::flush_pending_dms_(uint32_t learned_node) {
  // Copy the key out: dispatch_decoded_ may grow the node DB and invalidate the NodeInfoLite pointer.
  uint8_t peer_key[32];
  bool have_key = false;
  if (meshtastic_NodeInfoLite *peer = this->nodedb_.find(learned_node)) {
    if (peer->user.public_key.size == 32) {
      memcpy(peer_key, peer->user.public_key.bytes, sizeof(peer_key));
      have_key = true;
    }
  }
  for (size_t i = 0; i < this->pending_dms_.size();) {
    if (this->pending_dms_[i].peer != learned_node) {
      i++;
      continue;
    }
    const PendingDm dm = std::move(this->pending_dms_[i]);
    this->pending_dms_.erase(this->pending_dms_.begin() + i);
    if (!dm.is_rx) {
      ESP_LOGD(TAG, "Sending queued DM to !%08x", dm.peer);
      this->send_dm_(dm.peer, meshtastic_PortNum_TEXT_MESSAGE_APP, (const uint8_t *) dm.text.data(), dm.text.size(),
                     dm.want_ack, 0);
    } else if (have_key) {
      PacketHeader h;
      if (parse_header(dm.packet, h)) {
        const uint8_t *cipher = dm.packet.data() + MESHTASTIC_HEADER_LEN;
        const size_t cipher_len = dm.packet.size() - MESHTASTIC_HEADER_LEN;
        meshtastic_Data data = meshtastic_Data_init_zero;
        if (this->pkc_decode_(h, cipher, cipher_len, peer_key, &data)) {
          ESP_LOGD(TAG, "  decoded buffered DM from !%08x: portnum=%d", h.from, (int) data.portnum);
          this->dispatch_decoded_(data, h, "", dm.rssi, dm.snr);
        }
      }
    }
  }
}

void Meshtastic::expire_pending_dms_() {
  static const uint32_t TIMEOUT_MS = 120000;  // 2 minutes
  const uint32_t now = millis();
  for (size_t i = 0; i < this->pending_dms_.size();) {
    if (now - this->pending_dms_[i].queued_at > TIMEOUT_MS) {
      ESP_LOGW(TAG, "Dropping queued DM %s !%08x (no public key after %us)",
               this->pending_dms_[i].is_rx ? "from" : "to", this->pending_dms_[i].peer,
               (unsigned) (TIMEOUT_MS / 1000));
      this->pending_dms_.erase(this->pending_dms_.begin() + i);
    } else {
      i++;
    }
  }
}

void Meshtastic::send_position(double latitude, double longitude, int32_t altitude, uint32_t precision_bits,
                               const std::string &channel, bool want_ack) {
  const int idx = this->find_channel_index_(channel);
  if (idx < 0) {
    ESP_LOGW(TAG, "send_position: unknown channel \"%s\"", channel.c_str());
    return;
  }
  meshtastic_Position pos = meshtastic_Position_init_zero;
  int32_t lat_i = (int32_t) lround(latitude * 1e7);
  int32_t lon_i = (int32_t) lround(longitude * 1e7);
  if (precision_bits > 0 && precision_bits < 32) {
    const uint32_t mask = 0xFFFFFFFFu << (32 - precision_bits);
    lat_i = (int32_t) ((uint32_t) lat_i & mask);
    lon_i = (int32_t) ((uint32_t) lon_i & mask);
  }
  pos.has_latitude_i = true;
  pos.latitude_i = lat_i;
  pos.has_longitude_i = true;
  pos.longitude_i = lon_i;
  pos.has_altitude = true;
  pos.altitude = altitude;
  pos.precision_bits = precision_bits;

  this->self_position_ = pos;
  this->has_self_position_ = true;

  uint8_t buf[meshtastic_Position_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_Position_fields, &pos)) {
    ESP_LOGW(TAG, "Position encode failed");
    return;
  }
  ESP_LOGD(TAG, "TX position %.6f, %.6f (prec=%ubits)", latitude, longitude, precision_bits);
  this->send_data_(meshtastic_PortNum_POSITION_APP, buf, os.bytes_written, MESHTASTIC_BROADCAST_ADDR, idx, want_ack);
}

void Meshtastic::send_telemetry_(const meshtastic_Telemetry &tel, size_t channel_idx, bool want_ack) {
  uint8_t buf[meshtastic_Telemetry_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_Telemetry_fields, &tel)) {
    ESP_LOGW(TAG, "Telemetry encode failed");
    return;
  }
  this->send_data_(meshtastic_PortNum_TELEMETRY_APP, buf, os.bytes_written, MESHTASTIC_BROADCAST_ADDR, channel_idx,
                   want_ack);
}

void Meshtastic::send_device_metrics(const meshtastic_DeviceMetrics &metrics, const std::string &channel,
                                     bool want_ack) {
  const int idx = this->find_channel_index_(channel);
  if (idx < 0) {
    ESP_LOGW(TAG, "send_telemetry: unknown channel \"%s\"", channel.c_str());
    return;
  }
  this->self_metrics_ = metrics;
  this->has_self_metrics_ = true;
  meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
  tel.which_variant = meshtastic_Telemetry_device_metrics_tag;
  tel.variant.device_metrics = metrics;
  ESP_LOGD(TAG, "TX device telemetry");
  this->send_telemetry_(tel, idx, want_ack);
}

void Meshtastic::send_environment_metrics(const meshtastic_EnvironmentMetrics &metrics, const std::string &channel,
                                          bool want_ack) {
  const int idx = this->find_channel_index_(channel);
  if (idx < 0) {
    ESP_LOGW(TAG, "send_environment_metrics: unknown channel \"%s\"", channel.c_str());
    return;
  }
  meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
  tel.which_variant = meshtastic_Telemetry_environment_metrics_tag;
  tel.variant.environment_metrics = metrics;
  ESP_LOGD(TAG, "TX environment telemetry");
  this->send_telemetry_(tel, idx, want_ack);
}

void Meshtastic::send_node_info() {
  if (this->channels_.empty())
    return;
  ESP_LOGD(TAG, "Broadcasting NodeInfo");
  this->send_our_node_info_(MESHTASTIC_BROADCAST_ADDR, 0, false);
}

void Meshtastic::send_neighbor_info() {
  if (this->channels_.empty())
    return;
  meshtastic_NeighborInfo ni = meshtastic_NeighborInfo_init_zero;
  ni.node_id = this->node_num_;
  ni.node_broadcast_interval_secs = this->neighbor_info_interval_ / 1000;
  const pb_size_t cap = (pb_size_t) (sizeof(ni.neighbors) / sizeof(ni.neighbors[0]));
  for (const auto &nd : this->nodedb_.nodes()) {
    if (!(nd.has_hops_away && nd.hops_away == 0))
      continue;
    if (ni.neighbors_count >= cap)
      break;
    meshtastic_Neighbor &n = ni.neighbors[ni.neighbors_count++];
    n = meshtastic_Neighbor_init_zero;
    n.node_id = nd.num;
    n.snr = nd.snr;
  }
  uint8_t buf[meshtastic_NeighborInfo_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_NeighborInfo_fields, &ni)) {
    ESP_LOGW(TAG, "NeighborInfo encode failed");
    return;
  }
  ESP_LOGD(TAG, "Broadcasting NeighborInfo (%u neighbors)", (unsigned) ni.neighbors_count);
  this->send_data_(meshtastic_PortNum_NEIGHBORINFO_APP, buf, os.bytes_written, MESHTASTIC_BROADCAST_ADDR, 0, false);
}

void Meshtastic::send_traceroute(uint32_t dest, const std::string &channel, bool want_ack) {
  const int idx = this->find_channel_index_(channel);
  if (idx < 0) {
    ESP_LOGW(TAG, "traceroute: unknown channel \"%s\"", channel.c_str());
    return;
  }
  meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;  // empty route; hops appended along the way
  uint8_t buf[meshtastic_RouteDiscovery_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_RouteDiscovery_fields, &rd)) {
    ESP_LOGW(TAG, "RouteDiscovery encode failed");
    return;
  }
  ESP_LOGD(TAG, "TX traceroute to !%08x", dest);
  this->send_data_(meshtastic_PortNum_TRACEROUTE_APP, buf, os.bytes_written, dest, idx, want_ack, 0, true);
}

}  // namespace meshtastic
}  // namespace esphome
