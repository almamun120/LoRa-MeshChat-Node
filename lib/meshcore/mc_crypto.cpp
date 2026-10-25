#include "mc_crypto.h"
#include "mc_crypto_backend.h"
#include <algorithm>

namespace mc { namespace crypto {

static const uint8_t kSpkiPrefix[26] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01,
                                        0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

void random(uint8_t* out, size_t n) { be::random(out, n); }
void sha256(const uint8_t* in, size_t n, uint8_t out[32]) { be::sha256(in, n, out); }

Bytes spkiFromRaw(const uint8_t pub65[65]) {
    Bytes b(kSpkiPrefix, kSpkiPrefix + 26);
    putBytes(b, pub65, 65);
    return b;
}

bool rawFromSpki(const Bytes& s, uint8_t pub65[65]) {
    if (s.size() != 91 || memcmp(s.data(), kSpkiPrefix, 26) != 0 || s[26] != 0x04) return false;
    memcpy(pub65, s.data() + 26, 65);
    return true;
}

uint32_t nodeIdOf(const Bytes& spki) {
    uint8_t h[32];
    be::sha256(spki.data(), spki.size(), h);
    return get32(h);
}

Bytes Identity::spki() const { return spkiFromRaw(pub); }
uint32_t Identity::nodeId() const { return nodeIdOf(spki()); }

bool fromPrivate(Identity& id, const uint8_t priv[32]) {
    memcpy(id.priv, priv, 32);
    id.valid = be::ecPublic(id.priv, id.pub);
    return id.valid;
}

bool generate(Identity& id, uint8_t minFirstIdByte) {
    for (int i = 0; i < 4000; i++) {
        if (!be::ecGenerate(id.priv, id.pub)) return false;
        id.valid = true;
        uint32_t nid = id.nodeId();
        if (nid == kBroadcast) continue;
        if ((nid >> 24) >= minFirstIdByte) return true;
    }
    return false;
}

// ------------------------------------------------------------------ DER <-> raw

static void derInt(Bytes& out, const uint8_t* v32) {
    size_t i = 0;
    while (i < 31 && v32[i] == 0) i++;
    bool pad = (v32[i] & 0x80) != 0;
    out.push_back(0x02);
    out.push_back(uint8_t(32 - i + (pad ? 1 : 0)));
    if (pad) out.push_back(0);
    putBytes(out, v32 + i, 32 - i);
}

Bytes derFromRaw(const uint8_t rs[64]) {
    Bytes body;
    derInt(body, rs);
    derInt(body, rs + 32);
    Bytes out;
    out.push_back(0x30);
    out.push_back(uint8_t(body.size()));
    putBytes(out, body);
    return out;
}

static bool readInt(const Bytes& d, size_t& pos, uint8_t out32[32]) {
    if (pos + 2 > d.size() || d[pos] != 0x02) return false;
    size_t len = d[pos + 1];
    pos += 2;
    if (len == 0 || pos + len > d.size()) return false;
    const uint8_t* p = d.data() + pos;
    while (len > 0 && *p == 0) { p++; len--; }
    if (len > 32) return false;
    memset(out32, 0, 32);
    memcpy(out32 + (32 - len), p, len);
    pos = size_t(p - d.data()) + len;
    return true;
}

bool rawFromDer(const Bytes& d, uint8_t rs[64]) {
    if (d.size() < 8 || d[0] != 0x30) return false;
    size_t total = d[1];
    if (total + 2 != d.size()) return false;
    size_t pos = 2;
    return readInt(d, pos, rs) && readInt(d, pos, rs + 32) && pos == d.size();
}

// ------------------------------------------------------------------ sign / verify

Bytes sign(const Identity& me, const Bytes& m) {
    uint8_t h[32], rs[64];
    be::sha256(m.data(), m.size(), h);
    if (!be::ecdsaSign(me.priv, h, rs)) return {};
    return derFromRaw(rs);
}

bool verify(const Bytes& spki, const Bytes& m, const Bytes& sig) {
    uint8_t pub[65], rs[64], h[32];
    if (!rawFromSpki(spki, pub) || !rawFromDer(sig, rs)) return false;
    be::sha256(m.data(), m.size(), h);
    return be::ecdsaVerify(pub, h, rs);
}

// ------------------------------------------------------------------ private chat

Bytes aad(uint32_t src, uint32_t dst, const uint8_t msgId[8]) {
    Bytes b;
    put32(b, src);
    put32(b, dst);
    putBytes(b, msgId, 8);
    return b;
}

static bool sessionKey(const Identity& me, const Bytes& peerSpki, uint32_t peerId, uint8_t key[32]) {
    uint8_t peerPub[65], secret[32];
    if (!rawFromSpki(peerSpki, peerPub) || !be::ecdh(me.priv, peerPub, secret)) return false;
    // ids are compared as the 8-character upper-case hex strings the app sorts, which equals numeric order
    uint32_t mine = me.nodeId();
    std::string a = idHex(std::min(mine, peerId)), b = idHex(std::max(mine, peerId));
    std::string info = "meshchat-v1-e2e|" + a + "|" + b;
    const char* salt = "meshchat-salt-v1";
    uint8_t prk[32];
    be::hmacSha256(reinterpret_cast<const uint8_t*>(salt), strlen(salt), secret, 32, prk);
    Bytes data(info.begin(), info.end());
    data.push_back(0x01);
    be::hmacSha256(prk, 32, data.data(), data.size(), key);
    return true;
}

bool encrypt(const Identity& me, const Bytes& peerSpki, uint32_t peerId, const Bytes& aadBytes, const Bytes& plain, Bytes& out) {
    uint8_t key[32], nonce[12], tag[16];
    if (!sessionKey(me, peerSpki, peerId, key)) return false;
    be::random(nonce, 12);
    out.assign(nonce, nonce + 12);
    out.resize(12 + plain.size());
    if (!be::gcmEncrypt(key, nonce, aadBytes.data(), aadBytes.size(), plain.data(), plain.size(), out.data() + 12, tag)) return false;
    putBytes(out, tag, 16);
    return true;
}

bool decrypt(const Identity& me, const Bytes& peerSpki, uint32_t peerId, const Bytes& aadBytes, const Bytes& blob, Bytes& plain) {
    if (blob.size() < 12 + 16) return false;
    uint8_t key[32];
    if (!sessionKey(me, peerSpki, peerId, key)) return false;
    size_t n = blob.size() - 28;
    plain.assign(n, 0);
    return be::gcmDecrypt(key, blob.data(), aadBytes.data(), aadBytes.size(), blob.data() + 12, n, blob.data() + 12 + n, plain.data());
}

}}  // namespace mc::crypto
