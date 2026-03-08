#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Meshtastic on-air wire invariants
// Header is 16 bytes, little-endian: to(4) from(4) id(4) flags(1) channel(1) next_hop(1) relay_node(1).
// flags: bits0-2 hop_limit, bit3 want_ack, bit4 via_mqtt, bits5-7 hop_start.
// Ref: meshtastic/firmware src/mesh/RadioInterface.h (PacketHeader).

namespace esphome {
namespace meshtastic {

static constexpr size_t MESHTASTIC_HEADER_LEN = 16;
static constexpr uint32_t MESHTASTIC_BROADCAST_ADDR = 0xFFFFFFFF;

struct PacketHeader {
  uint32_t to;
  uint32_t from;
  uint32_t id;
  uint8_t hop_limit;
  bool want_ack;
  bool via_mqtt;
  uint8_t hop_start;
  uint8_t channel;
  uint8_t next_hop;
  uint8_t relay_node;
};

inline uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

inline void write_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t) (v & 0xFF);
  p[1] = (uint8_t) ((v >> 8) & 0xFF);
  p[2] = (uint8_t) ((v >> 16) & 0xFF);
  p[3] = (uint8_t) ((v >> 24) & 0xFF);
}

inline bool parse_header(const std::vector<uint8_t> &pkt, PacketHeader &h) {
  if (pkt.size() < MESHTASTIC_HEADER_LEN)
    return false;
  const uint8_t *p = pkt.data();
  h.to = read_u32_le(p);
  h.from = read_u32_le(p + 4);
  h.id = read_u32_le(p + 8);
  const uint8_t flags = p[12];
  h.hop_limit = flags & 0x07;
  h.want_ack = (flags >> 3) & 0x01;
  h.via_mqtt = (flags >> 4) & 0x01;
  h.hop_start = (flags >> 5) & 0x07;
  h.channel = p[13];
  h.next_hop = p[14];
  h.relay_node = p[15];
  return true;
}

inline void serialize_header(const PacketHeader &h, uint8_t out[MESHTASTIC_HEADER_LEN]) {
  write_u32_le(out, h.to);
  write_u32_le(out + 4, h.from);
  write_u32_le(out + 8, h.id);
  out[12] = (uint8_t) ((h.hop_limit & 0x07) | (h.want_ack ? 0x08 : 0) | (h.via_mqtt ? 0x10 : 0) |
                      ((h.hop_start & 0x07) << 5));
  out[13] = h.channel;
  out[14] = h.next_hop;
  out[15] = h.relay_node;
}

}  // namespace meshtastic
}  // namespace esphome
