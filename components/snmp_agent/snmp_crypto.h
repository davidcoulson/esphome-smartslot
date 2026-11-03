// SPDX-License-Identifier: GPL-2.0-or-later
// Hash, HMAC and AES-128 block primitives for SNMPv3 USM. mbedTLS on the device, OpenSSL on the host
// (build with -DSNMP_CRYPTO_OPENSSL).
#pragma once

#include <cstddef>
#include <cstdint>

namespace snmp {

enum Auth { AUTH_NONE, AUTH_MD5, AUTH_SHA, AUTH_SHA224, AUTH_SHA256, AUTH_SHA384, AUTH_SHA512 };
enum Priv { PRIV_NONE, PRIV_AES };

size_t hash_len(Auth a);  // digest size, which is also the localized key size
size_t mac_len(Auth a);   // truncated HMAC carried in msgAuthenticationParameters (RFC 3414, RFC 7860)

// Each returns false if the algorithm is not available. out holds hash_len(a) bytes.
bool crypto_hash(Auth a, const uint8_t *d, size_t n, uint8_t *out);
bool crypto_hash_repeated(Auth a, const uint8_t *pw, size_t pwlen, size_t total, uint8_t *out);  // pw repeated to total bytes
bool crypto_hmac(Auth a, const uint8_t *key, size_t keylen, const uint8_t *d, size_t n, uint8_t *out);
bool crypto_aes128_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);  // one ECB encryption

}  // namespace snmp
