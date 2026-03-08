#include "crypto.h"

#include <cstring>

#if __has_include("mbedtls/cipher.h")
#include "mbedtls/cipher.h"
#define MESHTASTIC_AES_MBEDTLS
#elif __has_include(<bearssl/bearssl_block.h>)
#include <bearssl/bearssl_block.h>
#define MESHTASTIC_AES_BEARSSL
#else
#error "meshtastic: no AES-CTR backend for this platform (need mbedtls or BearSSL)"
#endif

namespace esphome {
namespace meshtastic {

bool aes_ctr(const uint8_t *key, size_t key_len, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len) {
#if defined(MESHTASTIC_AES_MBEDTLS)
  const mbedtls_cipher_type_t type = (key_len == 32) ? MBEDTLS_CIPHER_AES_256_CTR : MBEDTLS_CIPHER_AES_128_CTR;
  const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(type);
  if (info == nullptr)
    return false;

  // mbedtls_cipher_crypt may write back the updated counter, so copy the IV.
  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, sizeof(iv_copy));

  mbedtls_cipher_context_t ctx;
  mbedtls_cipher_init(&ctx);
  size_t olen = 0;
  bool ok = mbedtls_cipher_setup(&ctx, info) == 0 &&
            mbedtls_cipher_setkey(&ctx, key, (int) (key_len * 8), MBEDTLS_ENCRYPT) == 0 &&
            mbedtls_cipher_crypt(&ctx, iv_copy, sizeof(iv_copy), in, len, out, &olen) == 0;
  mbedtls_cipher_free(&ctx);
  return ok && olen == len;

#elif defined(MESHTASTIC_AES_BEARSSL)
  br_aes_ct_ctr_keys kc;
  br_aes_ct_ctr_init(&kc, key, key_len);
  if (out != in)
    memcpy(out, in, len);
  br_aes_ct_ctr_run(&kc, iv, 0, out, len);
  return true;
#endif
}

}  // namespace meshtastic
}  // namespace esphome
