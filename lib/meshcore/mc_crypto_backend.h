// The only part that differs between the PC (OpenSSL, used by the tests) and the ESP32 (mbedTLS).
#pragma once
#include "mc_bytes.h"

namespace mc { namespace crypto { namespace be {
void random(uint8_t* out, size_t n);
bool ecGenerate(uint8_t priv[32], uint8_t pub[65]);
bool ecPublic(const uint8_t priv[32], uint8_t pub[65]);
bool ecdsaSign(const uint8_t priv[32], const uint8_t hash[32], uint8_t rs[64]);
bool ecdsaVerify(const uint8_t pub[65], const uint8_t hash[32], const uint8_t rs[64]);
bool ecdh(const uint8_t priv[32], const uint8_t peerPub[65], uint8_t out[32]);
void sha256(const uint8_t* in, size_t n, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t klen, const uint8_t* data, size_t dlen, uint8_t out[32]);
bool gcmEncrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen,
                const uint8_t* in, size_t n, uint8_t* out, uint8_t tag[16]);
bool gcmDecrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen,
                const uint8_t* in, size_t n, const uint8_t tag[16], uint8_t* out);
}}}  // namespace mc::crypto::be
