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

#include "esphome/core/helpers.h"
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
#ifdef MESHTASTIC_PKC_MBEDTLS
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
#ifdef MESHTASTIC_PKC_MBEDTLS
  mbedtls_sha256(data, len, out, 0);
#else
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, data, len);
  br_sha256_out(&ctx, out);
#endif
}

bool aes_ccm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *plain,
                     size_t len, uint8_t *cipher_out, uint8_t *tag_out, size_t tag_len) {
#ifdef MESHTASTIC_PKC_MBEDTLS
  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);
  bool ok = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
            mbedtls_ccm_encrypt_and_tag(&ctx, len, nonce, nonce_len, nullptr, 0, plain, cipher_out, tag_out,
                                        tag_len) == 0;
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

bool aes_ccm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len, const uint8_t *cipher,
                     size_t len, const uint8_t *tag, size_t tag_len, uint8_t *plain_out) {
#ifdef MESHTASTIC_PKC_MBEDTLS
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
