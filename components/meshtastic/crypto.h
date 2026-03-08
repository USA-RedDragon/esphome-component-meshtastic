#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace meshtastic {

// AES-CTR (encrypt == decrypt), in-place safe (out may equal in).
// key_len is 16 (AES-128) or 32 (AES-256); iv is the 16-byte initial counter block.
// Returns false on failure.
bool aes_ctr(const uint8_t *key, size_t key_len, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len);

}  // namespace meshtastic
}  // namespace esphome
