#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace meshtastic {

// AES-CTR (encrypt == decrypt), in-place safe (out may equal in).
// key_len is 16 (AES-128) or 32 (AES-256); iv is the 16-byte initial counter block.
// Returns false on failure.
bool aes_ctr(const uint8_t *key, size_t key_len, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len);

// X25519 ECDH: shared = scalarmult(clamp(private_key), public_key). All 32-byte little-endian.
// The private key is clamped internally (Meshtastic stores it raw). Returns false on failure.
bool x25519_shared(const uint8_t private_key[32], const uint8_t public_key[32], uint8_t shared_out[32]);

// SHA-256 of `len` bytes into a 32-byte digest (derives the AES key from the shared secret).
void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32]);

// AES-256-CCM (Meshtastic PKC uses nonce_len=13, tag_len=8; no AAD). false on failure;
// decrypt additionally returns false on authentication-tag mismatch.
bool aes_ccm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *plain,
                     size_t len, uint8_t *cipher_out, uint8_t *tag_out, size_t tag_len);
bool aes_ccm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *cipher,
                     size_t len, const uint8_t *tag, size_t tag_len, uint8_t *plain_out);

}  // namespace meshtastic
}  // namespace esphome
