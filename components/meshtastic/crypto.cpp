#include "crypto.h"

#include <cstring>
#include <vector>

#include "esphome/core/helpers.h"

// Backend selection (compile time):
//   PSA     - PSA Crypto API: Zephyr / nRF Connect SDK (nrf_security is PSA-first, the legacy API is
//             deprecated there) and Mbed TLS >= 4 (legacy crypto API removed)
//   mbedtls - legacy mbedtls_* API: ESP-IDF 5.x / Arduino-ESP32 (hardware acceleration sits behind it)
//   BearSSL - ESP8266, RP2040
// Override with -DMESHTASTIC_CRYPTO_PSA or -DMESHTASTIC_CRYPTO_LEGACY.
#if __has_include("mbedtls/build_info.h")
#include "mbedtls/build_info.h"
#endif
#if !defined(MESHTASTIC_CRYPTO_PSA) && !defined(MESHTASTIC_CRYPTO_LEGACY)
#if defined(USE_ZEPHYR) || (defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 4)
#define MESHTASTIC_CRYPTO_PSA
#endif
#endif

#if defined(MESHTASTIC_CRYPTO_PSA)
#include <psa/crypto.h>
#define MESHTASTIC_AES_PSA
#define MESHTASTIC_PKC_PSA
#if !defined(PSA_WANT_KEY_TYPE_AES) || !defined(PSA_WANT_ALG_CTR) || !defined(PSA_WANT_ALG_CCM)
#error "meshtastic: PSA backend needs PSA_WANT_KEY_TYPE_AES, PSA_WANT_ALG_CTR and PSA_WANT_ALG_CCM"
#endif
#if !defined(PSA_WANT_ALG_ECDH) || !defined(PSA_WANT_ECC_MONTGOMERY_255) || !defined(PSA_WANT_ALG_SHA_256)
#error "meshtastic: PSA backend needs PSA_WANT_ALG_ECDH, PSA_WANT_ECC_MONTGOMERY_255 and PSA_WANT_ALG_SHA_256"
#endif

#else  // legacy mbedtls / BearSSL

#if __has_include("mbedtls/cipher.h")
#include "mbedtls/cipher.h"
#define MESHTASTIC_AES_MBEDTLS
#elif __has_include(<bearssl/bearssl_block.h>)
#include <bearssl/bearssl_block.h>
#define MESHTASTIC_AES_BEARSSL
#else
#error "meshtastic: no AES-CTR backend for this platform (need PSA, mbedtls or BearSSL)"
#endif

#if __has_include("mbedtls/ecdh.h")
#define MESHTASTIC_PKC_MBEDTLS
#include "mbedtls/ccm.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#else
#define MESHTASTIC_PKC_BEARSSL
#include <bearssl/bearssl_aead.h>
#include <bearssl/bearssl_block.h>
#include <bearssl/bearssl_ec.h>
#include <bearssl/bearssl_hash.h>
#endif

#endif  // MESHTASTIC_CRYPTO_PSA

namespace esphome {
namespace meshtastic {

#ifdef MESHTASTIC_CRYPTO_PSA
// psa_crypto_init() is idempotent and cheap after the first call.
static bool psa_ready_() {
  static bool ready = false;
  if (!ready)
    ready = psa_crypto_init() == PSA_SUCCESS;
  return ready;
}

// Import a volatile key; the caller must psa_destroy_key() it.
static bool psa_import_(psa_key_type_t type, size_t bits, psa_key_usage_t usage, psa_algorithm_t alg,
                        const uint8_t *key, size_t key_len, psa_key_id_t *id) {
  if (!psa_ready_())
    return false;
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attr, type);
  psa_set_key_bits(&attr, bits);
  psa_set_key_usage_flags(&attr, usage);
  psa_set_key_algorithm(&attr, alg);
  psa_status_t st = psa_import_key(&attr, key, key_len, id);
  psa_reset_key_attributes(&attr);
  return st == PSA_SUCCESS;
}
#endif

bool aes_ctr(const uint8_t *key, size_t key_len, const uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len) {
#if defined(MESHTASTIC_AES_PSA)
  psa_key_id_t id;
  if (!psa_import_(PSA_KEY_TYPE_AES, key_len * 8, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_CTR, key, key_len, &id))
    return false;
  // Multi-part API: the one-shot psa_cipher_encrypt() would generate its own IV.
  psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
  bool ok = psa_cipher_encrypt_setup(&op, id, PSA_ALG_CTR) == PSA_SUCCESS &&
            psa_cipher_set_iv(&op, iv, 16) == PSA_SUCCESS;
  // Through a small bounce buffer, so in == out (in place) is safe regardless of the driver.
  uint8_t chunk[64];
  size_t done = 0;
  while (ok && done < len) {
    size_t n = len - done < sizeof(chunk) ? len - done : sizeof(chunk);
    memcpy(chunk, in + done, n);
    size_t olen = 0;
    ok = psa_cipher_update(&op, chunk, n, out + done, n, &olen) == PSA_SUCCESS && olen == n;
    done += n;
  }
  size_t flen = 0;
  ok = ok && psa_cipher_finish(&op, nullptr, 0, &flen) == PSA_SUCCESS && flen == 0;
  if (!ok)
    psa_cipher_abort(&op);
  psa_destroy_key(id);
  return ok;

#elif defined(MESHTASTIC_AES_MBEDTLS)
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

#ifdef MESHTASTIC_PKC_MBEDTLS
static int meshtastic_mbedtls_rng_(void *ctx, unsigned char *buf, size_t len) {
  (void) ctx;
  random_bytes(buf, len);
  return 0;
}
#endif

// X25519 ECDH: shared = scalarmult(clamp(private_key), public_key). Curve25519 keys are 32-byte LE.
bool x25519_shared(const uint8_t private_key[32], const uint8_t public_key[32], uint8_t shared_out[32]) {
  uint8_t clamped[32];
  memcpy(clamped, private_key, 32);
  clamped[0] &= 248;
  clamped[31] &= 127;
  clamped[31] |= 64;
#if defined(MESHTASTIC_PKC_PSA)
  psa_key_id_t id;
  if (!psa_import_(PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY), 255, PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH,
                   clamped, sizeof(clamped), &id))
    return false;
  size_t olen = 0;
  bool ok = psa_raw_key_agreement(PSA_ALG_ECDH, id, public_key, 32, shared_out, 32, &olen) == PSA_SUCCESS &&
            olen == 32;
  psa_destroy_key(id);
  memset(clamped, 0, sizeof(clamped));
  return ok;
#elif defined(MESHTASTIC_PKC_MBEDTLS)
  mbedtls_ecp_group grp;
  mbedtls_mpi d, z;
  mbedtls_ecp_point Q;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_mpi_init(&z);
  mbedtls_ecp_point_init(&Q);

  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
            mbedtls_mpi_read_binary_le(&d, clamped, 32) == 0 &&
            mbedtls_ecp_point_read_binary(&grp, &Q, public_key, 32) == 0 &&
            mbedtls_ecdh_compute_shared(&grp, &z, &Q, &d, meshtastic_mbedtls_rng_, nullptr) == 0 &&
            mbedtls_mpi_write_binary_le(&z, shared_out, 32) == 0;

  mbedtls_ecp_group_free(&grp);
  mbedtls_mpi_free(&d);
  mbedtls_mpi_free(&z);
  mbedtls_ecp_point_free(&Q);
  return ok;
#else  // BearSSL
  uint8_t shared[32];
  memcpy(shared, public_key, sizeof(shared));
  uint32_t r = br_ec_c25519_i15.mul(shared, sizeof(shared), clamped, sizeof(clamped), BR_EC_curve25519);
  memcpy(shared_out, shared, sizeof(shared));
  return r == 1;
#endif
}

void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32]) {
#if defined(MESHTASTIC_PKC_PSA)
  size_t olen = 0;
  if (!psa_ready_() || psa_hash_compute(PSA_ALG_SHA_256, data, len, out, 32, &olen) != PSA_SUCCESS)
    memset(out, 0, 32);  // void API: a zero key makes the following CCM step fail authentication
#elif defined(MESHTASTIC_PKC_MBEDTLS)
  mbedtls_sha256(data, len, out, 0);
#else
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, data, len);
  br_sha256_out(&ctx, out);
#endif
}

bool aes_ccm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *plain, size_t len,
                     uint8_t *cipher_out, uint8_t *tag_out, size_t tag_len) {
#if defined(MESHTASTIC_PKC_PSA)
  const psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, tag_len);
  psa_key_id_t id;
  if (!psa_import_(PSA_KEY_TYPE_AES, 256, PSA_KEY_USAGE_ENCRYPT, alg, key, 32, &id))
    return false;
  // PSA writes ciphertext || tag into one buffer.
  std::vector<uint8_t> buf(len + tag_len);
  size_t olen = 0;
  bool ok = psa_aead_encrypt(id, alg, nonce, nonce_len, nullptr, 0, plain, len, buf.data(), buf.size(), &olen) ==
                PSA_SUCCESS &&
            olen == buf.size();
  psa_destroy_key(id);
  if (ok) {
    memcpy(cipher_out, buf.data(), len);
    memcpy(tag_out, buf.data() + len, tag_len);
  }
  return ok;
#elif defined(MESHTASTIC_PKC_MBEDTLS)
  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);
  bool ok =
      mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
      mbedtls_ccm_encrypt_and_tag(&ctx, len, nonce, nonce_len, nullptr, 0, plain, cipher_out, tag_out, tag_len) == 0;
  mbedtls_ccm_free(&ctx);
  return ok;
#else  // BearSSL: CCM needs a CTR+CBC-MAC block cipher (ctrcbc), run is in place.
  br_aes_ct_ctrcbc_keys bc;
  br_aes_ct_ctrcbc_init(&bc, key, 32);
  br_ccm_context ctx;
  br_ccm_init(&ctx, &bc.vtable);
  if (!br_ccm_reset(&ctx, nonce, nonce_len, 0, len, tag_len))
    return false;
  br_ccm_flip(&ctx);  // no AAD
  if (cipher_out != plain)
    memcpy(cipher_out, plain, len);
  br_ccm_run(&ctx, 1, cipher_out, len);
  br_ccm_get_tag(&ctx, tag_out);
  return true;
#endif
}

bool aes_ccm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *cipher, size_t len,
                     const uint8_t *tag, size_t tag_len, uint8_t *plain_out) {
#if defined(MESHTASTIC_PKC_PSA)
  const psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, tag_len);
  psa_key_id_t id;
  if (!psa_import_(PSA_KEY_TYPE_AES, 256, PSA_KEY_USAGE_DECRYPT, alg, key, 32, &id))
    return false;
  // PSA expects ciphertext || tag as one input.
  std::vector<uint8_t> buf(len + tag_len);
  memcpy(buf.data(), cipher, len);
  memcpy(buf.data() + len, tag, tag_len);
  size_t olen = 0;
  bool ok = psa_aead_decrypt(id, alg, nonce, nonce_len, nullptr, 0, buf.data(), buf.size(), plain_out, len, &olen) ==
                PSA_SUCCESS &&
            olen == len;
  psa_destroy_key(id);
  return ok;
#elif defined(MESHTASTIC_PKC_MBEDTLS)
  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);
  bool ok = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
            mbedtls_ccm_auth_decrypt(&ctx, len, nonce, nonce_len, nullptr, 0, cipher, plain_out, tag, tag_len) == 0;
  mbedtls_ccm_free(&ctx);
  return ok;
#else  // BearSSL
  br_aes_ct_ctrcbc_keys bc;
  br_aes_ct_ctrcbc_init(&bc, key, 32);
  br_ccm_context ctx;
  br_ccm_init(&ctx, &bc.vtable);
  if (!br_ccm_reset(&ctx, nonce, nonce_len, 0, len, tag_len))
    return false;
  br_ccm_flip(&ctx);  // no AAD
  memcpy(plain_out, cipher, len);
  br_ccm_run(&ctx, 0, plain_out, len);
  return br_ccm_check_tag(&ctx, tag) == 1;
#endif
}

}  // namespace meshtastic
}  // namespace esphome
