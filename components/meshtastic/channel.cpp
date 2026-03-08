#include <cstring>

#include "channel.h"
#include "crypto.h"

namespace esphome {
namespace meshtastic {

static uint8_t xor_hash(const uint8_t *p, size_t len) {
  uint8_t h = 0;
  for (size_t i = 0; i < len; i++)
    h ^= p[i];
  return h;
}

uint8_t channel_hash(const std::string &name, const std::vector<uint8_t> &key) {
  return xor_hash(reinterpret_cast<const uint8_t *>(name.data()), name.size()) ^ xor_hash(key.data(), key.size());
}

bool Channel::crypt(uint32_t from, uint32_t id, const uint8_t *in, size_t len, uint8_t *out) const {
  if (this->key.empty()) {
    memcpy(out, in, len);
    return true;
  }

  // CTR nonce: [0..3]=packet id LE, [4..7]=0, [8..11]=from LE, [12..15]=block counter (starts 0).
  uint8_t nonce[16] = {0};
  nonce[0] = (uint8_t) id;
  nonce[1] = (uint8_t) (id >> 8);
  nonce[2] = (uint8_t) (id >> 16);
  nonce[3] = (uint8_t) (id >> 24);
  nonce[8] = (uint8_t) from;
  nonce[9] = (uint8_t) (from >> 8);
  nonce[10] = (uint8_t) (from >> 16);
  nonce[11] = (uint8_t) (from >> 24);

  return aes_ctr(this->key.data(), this->key.size(), nonce, in, out, len);
}

}  // namespace meshtastic
}  // namespace esphome
