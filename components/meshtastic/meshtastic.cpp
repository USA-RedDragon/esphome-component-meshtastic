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
}

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

void Meshtastic::handle_rx(const std::vector<uint8_t> &packet, float rssi, float snr) {
  PacketHeader h;
  if (!parse_header(packet, h)) {
    ESP_LOGW(TAG, "Dropping runt packet: %zu bytes", packet.size());
    return;
  }
  ESP_LOGD(TAG, "RX %zuB rssi=%.0f snr=%.1f from=!%08x to=!%08x id=0x%08x ch=0x%02x hop=%u/%u%s%s", packet.size(),
           rssi, snr, h.from, h.to, h.id, h.channel, h.hop_limit, h.hop_start, h.want_ack ? " ack" : "",
           h.via_mqtt ? " mqtt" : "");

  if (this->dedup_.is_duplicate(h.from, h.id, millis())) {
    ESP_LOGV(TAG, "  duplicate, ignoring");
    return;
  }

  this->maybe_relay_(packet, h, snr);

  const uint8_t *cipher = packet.data() + MESHTASTIC_HEADER_LEN;
  const size_t cipher_len = packet.size() - MESHTASTIC_HEADER_LEN;

  // Try each channel whose hash matches; decode the first that yields a valid Data.
  for (size_t ci = 0; ci < this->channels_.size(); ci++) {
    Channel &ch = this->channels_[ci];
    if (ch.hash != h.channel)
      continue;
    std::vector<uint8_t> plain(cipher_len);
    if (!ch.crypt(h.from, h.id, cipher, cipher_len, plain.data()))
      continue;
    meshtastic_Data data = meshtastic_Data_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(plain.data(), plain.size());
    if (!pb_decode(&stream, meshtastic_Data_fields, &data))
      continue;
    ESP_LOGD(TAG, "  decoded on \"%s\": portnum=%d payload=%uB", ch.name.c_str(), (int) data.portnum,
             (unsigned) data.payload.size);

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
        }
        ESP_LOGD(TAG, "  node !%08x \"%s\" (%s) %s", h.from, user.long_name, user.short_name,
                 node == nullptr ? "(db off)" : (node_is_new ? "NEW" : "updated"));
        for (auto *t : this->on_nodeinfo_triggers_)
          t->trigger(h.from, ch.name, std::string(user.long_name), std::string(user.short_name),
                     hardware_model_name(user.hw_model), role_name(user.role), rssi, snr);
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
          t->trigger(h.from, ch.name, lat, lon, pos.altitude, pos.precision_bits, pos.time, rssi, snr);
      }
    }

    if (data.portnum == meshtastic_PortNum_TELEMETRY_APP && h.from != 0 && h.from != this->node_num_) {
      meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
      pb_istream_t ts = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
      if (pb_decode(&ts, meshtastic_Telemetry_fields, &tel) &&
          tel.which_variant == meshtastic_Telemetry_device_metrics_tag) {
        const meshtastic_DeviceMetrics &dm = tel.variant.device_metrics;
        if (node != nullptr) {
          node->has_device_metrics = true;
          node->device_metrics = dm;  // NodeInfoLite stores the full DeviceMetrics
        }
        ESP_LOGD(TAG, "  telemetry !%08x batt=%u%% %.2fV chUtil=%.1f%% airTx=%.1f%%", h.from, dm.battery_level,
                 dm.voltage, dm.channel_utilization, dm.air_util_tx);
        for (auto *t : this->on_telemetry_triggers_)
          t->trigger(h.from, ch.name, dm.battery_level, dm.voltage, dm.channel_utilization, dm.air_util_tx,
                     dm.uptime_seconds, rssi, snr);
      }
    }

    if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
      std::string text((const char *) data.payload.bytes, data.payload.size);
      ESP_LOGD(TAG, "  text: %s", text.c_str());
      for (auto *t : this->on_text_triggers_)
        t->trigger(h.from, h.to, ch.name, text, rssi, snr);
    }

    if (h.want_ack && h.to == this->node_num_ && h.from != 0 && h.from != this->node_num_)
      this->send_ack_(h.from, h.id, ci);
    return;
  }
  ESP_LOGD(TAG, "  no channel matched hash 0x%02x (or decode failed)", h.channel);
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
  this->set_timeout(delay, [this, tx]() { this->transmit_(tx); });
}

void Meshtastic::transmit_(const std::vector<uint8_t> &packet) {
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
}

void Meshtastic::send_ack_(uint32_t to, uint32_t request_id, size_t channel_idx) {
  meshtastic_Routing routing = meshtastic_Routing_init_zero;
  routing.which_variant = meshtastic_Routing_error_reason_tag;
  routing.error_reason = meshtastic_Routing_Error_NONE;

  uint8_t buf[meshtastic_Routing_size];
  pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
  if (!pb_encode(&os, meshtastic_Routing_fields, &routing)) {
    ESP_LOGW(TAG, "Routing ack encode failed");
    return;
  }
  ESP_LOGD(TAG, "ACK to=!%08x for id=0x%08x", to, request_id);
  this->send_data_(meshtastic_PortNum_ROUTING_APP, buf, os.bytes_written, to, channel_idx, false, request_id);
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
}

void Meshtastic::request_node_info_(uint32_t dest) {
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
  if (!pb_encode(&os, meshtastic_User_fields, &user))
    return;
  ESP_LOGD(TAG, "Requesting NodeInfo from !%08x", dest);
  this->send_data_(meshtastic_PortNum_NODEINFO_APP, buf, os.bytes_written, dest, 0, false, 0, true);
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
  ESP_LOGD(TAG, "Broadcasting NodeInfo");
  this->send_data_(meshtastic_PortNum_NODEINFO_APP, buf, os.bytes_written, MESHTASTIC_BROADCAST_ADDR, 0, false);
}

}  // namespace meshtastic
}  // namespace esphome
