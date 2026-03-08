#include "meshtastic.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include "mesh.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
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

  if (this->node_info_interval_ > 0) {
    this->defer([this]() { this->broadcast_node_info_(); });
    this->set_interval(this->node_info_interval_, [this]() { this->broadcast_node_info_(); });
  }
}

void Meshtastic::dump_config() {
  ESP_LOGCONFIG(TAG, "Meshtastic:");
  ESP_LOGCONFIG(TAG, "  Node: !%08x \"%s\" (%s)", this->node_num_, this->long_name_.c_str(),
                this->short_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Role: %u  Hop limit: %u", this->role_, this->hop_limit_);
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

    if (h.from != 0 && h.from != this->node_num_) {
      bool is_new = false;
      meshtastic_NodeInfoLite *node = this->nodedb_.get_or_create(h.from, &is_new);
      node->num = h.from;
      node->snr = snr;
      node->last_heard = millis();
      node->has_hops_away = true;
      node->hops_away = (h.hop_start >= h.hop_limit) ? (h.hop_start - h.hop_limit) : 0;
      if (data.portnum == meshtastic_PortNum_NODEINFO_APP) {
        meshtastic_User user = meshtastic_User_init_zero;
        pb_istream_t us = pb_istream_from_buffer(data.payload.bytes, data.payload.size);
        if (pb_decode(&us, meshtastic_User_fields, &user)) {
          node->has_user = true;
          memcpy(node->user.long_name, user.long_name, sizeof(node->user.long_name));
          memcpy(node->user.short_name, user.short_name, sizeof(node->user.short_name));
          node->user.hw_model = user.hw_model;
          node->user.role = user.role;
          node->user.is_licensed = user.is_licensed;
          ESP_LOGD(TAG, "  node !%08x \"%s\" (%s) %s [%u known]", h.from, node->user.long_name, node->user.short_name,
                   is_new ? "NEW" : "updated", (unsigned) this->nodedb_.size());
          for (auto *t : this->on_nodeinfo_triggers_)
            t->trigger(h.from, std::string(node->user.long_name), std::string(node->user.short_name),
                       (uint32_t) node->user.hw_model, (uint32_t) node->user.role);
        }
      } else if (is_new) {
        ESP_LOGD(TAG, "  node !%08x heard (awaiting NodeInfo) [%u known]", h.from, (unsigned) this->nodedb_.size());
      }
    }

    if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
      std::string text((const char *) data.payload.bytes, data.payload.size);
      ESP_LOGD(TAG, "  text: %s", text.c_str());
      for (auto *t : this->on_text_triggers_)
        t->trigger(h.from, h.to, (uint8_t) ci, text, rssi, snr);
    }
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
                            size_t channel_idx, bool want_ack) {
  if (channel_idx >= this->channels_.size())
    return;
  Channel &ch = this->channels_[channel_idx];

  meshtastic_Data data = meshtastic_Data_init_zero;
  data.portnum = (meshtastic_PortNum) portnum;
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

void Meshtastic::broadcast_node_info_() {
  if (this->channels_.empty())
    return;
  meshtastic_User user = meshtastic_User_init_zero;
  snprintf(user.id, sizeof(user.id), "!%08x", this->node_num_);
  strncpy(user.long_name, this->long_name_.c_str(), sizeof(user.long_name) - 1);
  strncpy(user.short_name, this->short_name_.c_str(), sizeof(user.short_name) - 1);
  user.hw_model = (meshtastic_HardwareModel) this->hw_model_;
  user.role = (meshtastic_Config_DeviceConfig_Role) this->role_;

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
