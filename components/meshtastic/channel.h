#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace meshtastic {

// 1-byte channel hash = XOR(name bytes) ^ XOR(key bytes). Ref: firmware Channels.cpp generateHash.
uint8_t channel_hash(const std::string &name, const std::vector<uint8_t> &key);

struct Channel {
  std::string name;
  std::vector<uint8_t> key;  // 0 (none), 16 (AES-128) or 32 (AES-256) bytes
  bool uplink{false};
  bool downlink{false};
  uint8_t hash{0};

  bool has_crypto() const { return !this->key.empty(); }

  // AES-CTR (encrypt == decrypt) of `len` bytes in->out. Nonce = id(8 LE) + from(4 LE) + counter(4=0).
  bool crypt(uint32_t from, uint32_t id, const uint8_t *in, size_t len, uint8_t *out) const;
};

}  // namespace meshtastic
}  // namespace esphome
