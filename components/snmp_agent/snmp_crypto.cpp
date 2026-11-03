// SPDX-License-Identifier: GPL-2.0-or-later
#include "snmp_crypto.h"

#include <cstring>

#ifdef SNMP_CRYPTO_OPENSSL
#include <openssl/evp.h>
#include <openssl/hmac.h>
#else
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#endif

namespace snmp {

size_t hash_len(Auth a) {
  static const size_t n[] = {0, 16, 20, 28, 32, 48, 64};
  return n[a];
}
size_t mac_len(Auth a) {
  static const size_t n[] = {0, 12, 12, 16, 24, 32, 48};
  return n[a];
}

#ifdef SNMP_CRYPTO_OPENSSL

static const EVP_MD *md(Auth a) {
  switch (a) {
    case AUTH_MD5: return EVP_md5();
    case AUTH_SHA: return EVP_sha1();
    case AUTH_SHA224: return EVP_sha224();
    case AUTH_SHA256: return EVP_sha256();
    case AUTH_SHA384: return EVP_sha384();
    case AUTH_SHA512: return EVP_sha512();
    default: return nullptr;
  }
}

bool crypto_hash(Auth a, const uint8_t *d, size_t n, uint8_t *out) {
  return md(a) && EVP_Digest(d, n, out, nullptr, md(a), nullptr) == 1;
}

bool crypto_hash_repeated(Auth a, const uint8_t *pw, size_t pwlen, size_t total, uint8_t *out) {
  if (!md(a) || pwlen == 0) return false;
  EVP_MD_CTX *c = EVP_MD_CTX_new();
  bool ok = EVP_DigestInit_ex(c, md(a), nullptr) == 1;
  uint8_t buf[64];
  for (size_t done = 0; ok && done < total; done += sizeof buf) {
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = pw[(done + i) % pwlen];
    ok = EVP_DigestUpdate(c, buf, total - done < sizeof buf ? total - done : sizeof buf) == 1;
  }
  ok = ok && EVP_DigestFinal_ex(c, out, nullptr) == 1;
  EVP_MD_CTX_free(c);
  return ok;
}

bool crypto_hmac(Auth a, const uint8_t *key, size_t keylen, const uint8_t *d, size_t n, uint8_t *out) {
  return md(a) && HMAC(md(a), key, (int) keylen, d, n, out, nullptr) != nullptr;
}

bool crypto_aes128_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
  int l = 0;
  bool ok = EVP_EncryptInit_ex(c, EVP_aes_128_ecb(), nullptr, key, nullptr) == 1 && EVP_CIPHER_CTX_set_padding(c, 0) == 1 &&
            EVP_EncryptUpdate(c, out, &l, in, 16) == 1 && l == 16;
  EVP_CIPHER_CTX_free(c);
  return ok;
}

#else

static const mbedtls_md_info_t *md(Auth a) {
  static const mbedtls_md_type_t t[] = {MBEDTLS_MD_NONE,   MBEDTLS_MD_MD5,    MBEDTLS_MD_SHA1,  MBEDTLS_MD_SHA224,
                                        MBEDTLS_MD_SHA256, MBEDTLS_MD_SHA384, MBEDTLS_MD_SHA512};
  return mbedtls_md_info_from_type(t[a]);
}

bool crypto_hash(Auth a, const uint8_t *d, size_t n, uint8_t *out) { return md(a) && mbedtls_md(md(a), d, n, out) == 0; }

bool crypto_hash_repeated(Auth a, const uint8_t *pw, size_t pwlen, size_t total, uint8_t *out) {
  if (!md(a) || pwlen == 0) return false;
  mbedtls_md_context_t c;
  mbedtls_md_init(&c);
  bool ok = mbedtls_md_setup(&c, md(a), 0) == 0 && mbedtls_md_starts(&c) == 0;
  uint8_t buf[64];
  for (size_t done = 0; ok && done < total; done += sizeof buf) {
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = pw[(done + i) % pwlen];
    ok = mbedtls_md_update(&c, buf, total - done < sizeof buf ? total - done : sizeof buf) == 0;
  }
  ok = ok && mbedtls_md_finish(&c, out) == 0;
  mbedtls_md_free(&c);
  return ok;
}

bool crypto_hmac(Auth a, const uint8_t *key, size_t keylen, const uint8_t *d, size_t n, uint8_t *out) {
  return md(a) && mbedtls_md_hmac(md(a), key, keylen, d, n, out) == 0;
}

bool crypto_aes128_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  mbedtls_aes_context c;
  mbedtls_aes_init(&c);
  bool ok = mbedtls_aes_setkey_enc(&c, key, 128) == 0 && mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_ENCRYPT, in, out) == 0;
  mbedtls_aes_free(&c);
  return ok;
}

#endif

}  // namespace snmp
