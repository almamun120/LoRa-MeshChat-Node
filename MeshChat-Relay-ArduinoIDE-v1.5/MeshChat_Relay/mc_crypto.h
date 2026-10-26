// Crypto compatible with the app (core/Crypto.kt): P-256 identity, ECDSA/SHA-256 signatures (DER),
// static-static ECDH -> HKDF-SHA256 -> AES-256-GCM with AAD = src|dst|msgId.
#pragma once
#include "mc_bytes.h"

namespace mc {
namespace crypto {

struct Identity {
    uint8_t priv[32] = {0};
    uint8_t pub[65] = {0};   // 04 | X | Y
    bool valid = false;
    Bytes spki() const;      // X.509 SubjectPublicKeyInfo, 91 bytes, exactly what phones exchange
    uint32_t nodeId() const; // first 4 bytes of SHA-256(spki)
};

void random(uint8_t* out, size_t n);
void sha256(const uint8_t* in, size_t n, uint8_t out[32]);

/** Generates a key pair; with minFirstIdByte > 0 it retries until the Node ID starts with at least that byte
 *  (a high ID makes phones - which dial out when their ID is lower - the ones that open the link). */
bool generate(Identity& id, uint8_t minFirstIdByte = 0);
bool fromPrivate(Identity& id, const uint8_t priv[32]);

Bytes spkiFromRaw(const uint8_t pub65[65]);
bool rawFromSpki(const Bytes& spki, uint8_t pub65[65]);
uint32_t nodeIdOf(const Bytes& spki);

Bytes sign(const Identity& me, const Bytes& message);                                   // DER signature
bool verify(const Bytes& peerSpki, const Bytes& message, const Bytes& sigDer);

Bytes aad(uint32_t src, uint32_t dst, const uint8_t msgId[8]);
/** out = nonce(12) | ciphertext | tag(16) */
bool encrypt(const Identity& me, const Bytes& peerSpki, uint32_t peerId, const Bytes& aad, const Bytes& plain, Bytes& out);
bool decrypt(const Identity& me, const Bytes& peerSpki, uint32_t peerId, const Bytes& aad, const Bytes& blob, Bytes& plain);

// DER <-> raw r|s (exposed for tests)
Bytes derFromRaw(const uint8_t rs[64]);
bool rawFromDer(const Bytes& der, uint8_t rs[64]);

}  // namespace crypto
}  // namespace mc
