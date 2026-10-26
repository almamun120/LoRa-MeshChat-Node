#include "mc_packet.h"
#include <cstdio>
#include <cmath>

namespace mc {

uint64_t Packet::key() const {
    // FNV-1a over src + msgId: 64 bits is plenty for a cache of a few hundred entries.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint8_t b) { h ^= b; h *= 1099511628211ull; };
    for (int i = 3; i >= 0; i--) mix((src >> (i * 8)) & 0xFF);
    for (uint8_t b : msgId) mix(b);
    return h;
}

Bytes Packet::signedBytes() const {
    Bytes b;
    b.push_back(type);
    putBytes(b, msgId.data(), 8);
    put32(b, src);
    put32(b, dst);
    put64(b, ts);
    putBytes(b, payload);
    return b;
}

Bytes Packet::encode() const {
    Bytes b;
    b.reserve(kHeaderSize + payload.size() + sig.size() + 1);
    bool signedPkt = !sig.empty();
    b.push_back(kVersion);
    b.push_back(type);
    b.push_back(signedPkt ? (flags | kFlagSigned) : (flags & ~kFlagSigned));
    b.push_back(ttl);
    b.push_back(hops);
    putBytes(b, msgId.data(), 8);
    put32(b, src);
    put32(b, dst);
    put64(b, ts);
    put16(b, uint16_t(payload.size()));
    putBytes(b, payload);
    if (signedPkt) { b.push_back(uint8_t(sig.size())); putBytes(b, sig); }
    return b;
}

bool Packet::decode(const uint8_t* d, size_t n, Packet& o) {
    if (n < kHeaderSize || n > kMaxPacket) return false;
    if (d[0] != kVersion) return false;
    uint8_t type = d[1];
    if (type < T_HELLO || type > T_IDENTITY_REQ) return false;
    o = Packet();
    o.type = type;
    o.flags = d[2];
    o.ttl = d[3];
    o.hops = d[4];
    if (o.ttl > kMaxHops || o.hops > 64) return false;
    memcpy(o.msgId.data(), d + 5, 8);
    o.src = get32(d + 13);
    o.dst = get32(d + 17);
    o.ts = get64(d + 21);
    size_t len = (size_t(d[29]) << 8) | d[30];
    size_t pos = kHeaderSize;
    if (n - pos < len) return false;
    o.payload.assign(d + pos, d + pos + len);
    pos += len;
    if (o.flags & kFlagSigned) {
        if (pos >= n) return false;
        size_t sl = d[pos++];
        if (sl == 0 || n - pos < sl) return false;
        o.sig.assign(d + pos, d + pos + sl);
        pos += sl;
    }
    return pos == n;
}

// ------------------------------------------------------------------ payloads

static void putShortStr(Bytes& b, const std::string& s, size_t max) {
    std::string c = clampUtf8(s, max);
    b.push_back(uint8_t(c.size()));
    b.insert(b.end(), c.begin(), c.end());
}
static bool getShortStr(const Bytes& b, size_t& pos, std::string& out) {
    if (pos >= b.size()) return false;
    size_t n = b[pos++];
    if (b.size() - pos < n) return false;
    out.assign(reinterpret_cast<const char*>(b.data() + pos), n);
    pos += n;
    return true;
}

Bytes encodeHello(const Hello& h) {
    Bytes b;
    putShortStr(b, h.name, kMaxNameBytes);
    b.push_back(h.caps);
    size_t n = std::min(h.routes.size(), kMaxRouteAds);
    b.push_back(uint8_t(n));
    for (size_t i = 0; i < n; i++) { put32(b, h.routes[i].node); b.push_back(uint8_t((h.routes[i].hops & 0x7F) | (h.routes[i].lora ? 0x80 : 0))); }
    return b;
}

bool decodeHello(const Bytes& b, Hello& o) {
    size_t pos = 0;
    if (!getShortStr(b, pos, o.name)) return false;
    if (b.size() - pos < 2) return false;
    o.caps = b[pos++];
    size_t n = b[pos++];
    if (n > kMaxRouteAds || b.size() - pos < n * 5) return false;
    o.routes.clear();
    for (size_t i = 0; i < n; i++) { o.routes.push_back({get32(b.data() + pos), uint8_t(b[pos + 4] & 0x7F), (b[pos + 4] & 0x80) != 0}); pos += 5; }
    return true;
}

Bytes encodeIdentity(const IdentityPayload& i) {
    Bytes b;
    b.push_back(i.caps);
    putShortStr(b, i.name, kMaxNameBytes);
    b.push_back(uint8_t(i.publicKey.size()));
    putBytes(b, i.publicKey);
    std::string e = clampUtf8(i.email, 64);
    if (!e.empty() || i.hasWx) putShortStr(b, e, 64);        // the weather field sits behind the e-mail slot, which is then written even when empty
    if (i.hasWx) { put16(b, uint16_t(i.wxT10)); put16(b, i.wxP10); }
    return b;
}

bool decodeIdentity(const Bytes& b, IdentityPayload& o) {
    if (b.empty()) return false;
    size_t pos = 0;
    o.caps = b[pos++];
    if (!getShortStr(b, pos, o.name)) return false;
    if (pos >= b.size()) return false;
    size_t kl = b[pos++];
    if (kl == 0 || b.size() - pos < kl) return false;
    o.publicKey.assign(b.begin() + pos, b.begin() + pos + kl);
    pos += kl;
    o.email.clear();
    o.hasWx = false;
    if (pos < b.size() && !getShortStr(b, pos, o.email)) return false;
    if (pos < b.size()) {
        if (b.size() - pos != 4) return false;
        o.wxT10 = int16_t((b[pos] << 8) | b[pos + 1]);
        o.wxP10 = uint16_t((b[pos + 2] << 8) | b[pos + 3]);
        if (o.wxT10 < -600 || o.wxT10 > 800 || o.wxP10 < 3000 || o.wxP10 > 11000) return false;   // not believable: a broken sensor must not become "weather"
        o.hasWx = true;
        pos += 4;
    }
    return pos == b.size();
}

Bytes encodeAnnounce(const AnnouncePayload& a) {
    Bytes b;
    putShortStr(b, a.author, kMaxNameBytes);
    b.insert(b.end(), a.content.begin(), a.content.end());
    return b;
}

bool decodeAnnounce(const Bytes& b, AnnouncePayload& o) {
    size_t pos = 0;
    if (!getShortStr(b, pos, o.author)) return false;
    o.content.assign(reinterpret_cast<const char*>(b.data() + pos), b.size() - pos);
    return true;
}

bool decodeSos(const Bytes& b, SosPayload& o) {
    if (b.size() < 4) return false;
    size_t pos = 0;
    int state = b[pos++];
    o.battery = b[pos++];
    int flags = b[pos++];
    if (state > 1 || (o.battery > 100 && o.battery != 255)) return false;
    o.active = state == 1;
    o.hasLoc = false;
    if (flags & 1) {
        if (b.size() - pos < 10) return false;
        o.lat = int32_t(get32(b.data() + pos)) / 1e6;
        o.lon = int32_t(get32(b.data() + pos + 4)) / 1e6;
        o.accuracyM = (b[pos + 8] << 8) | b[pos + 9];
        pos += 10;
        if (std::fabs(o.lat) > 90 || std::fabs(o.lon) > 180) return false;
        o.hasLoc = true;
    }
    if (!getShortStr(b, pos, o.name)) return false;
    return pos == b.size();
}

// ------------------------------------------------------------------ private-message content

bool decodeContent(const Bytes& p, Content& o) {
    if (p.empty()) return false;
    int first = p[0];
    int kind = first & 0x7F;
    if (kind > K_GROUP_LEAVE) return false;
    size_t pos = 1;
    o = Content();
    o.kind = ContentKind(kind);
    if (first & 0x80) {
        if (pos >= p.size()) return false;
        int flags = p[pos++];
        if (flags & 1) { if (p.size() - pos < 4) return false; o.ttlSec = int(get32(p.data() + pos)); pos += 4; }
        if (flags & 2) {
            if (p.size() - pos < 9) return false;
            pos += 8;
            size_t ql = p[pos++];
            if (p.size() - pos < ql) return false;
            pos += ql;
        }
        if (flags & 4) { if (p.size() - pos < 16) return false; pos += 16; o.inGroup = true; }
    }
    size_t rest = p.size() - pos;
    switch (o.kind) {
        case K_TEXT:
            o.text.assign(reinterpret_cast<const char*>(p.data() + pos), rest);
            break;
        case K_LOCATION: {
            if (rest != 20) return false;
            uint64_t la = get64(p.data() + pos), lo = get64(p.data() + pos + 8);
            double lat, lon;
            memcpy(&lat, &la, 8);
            memcpy(&lon, &lo, 8);
            char buf[64];
            snprintf(buf, sizeof buf, "[location %.5f, %.5f]", lat, lon);
            o.text = buf;
            break;
        }
        case K_IMAGE: o.text = "[photo - this device cannot show pictures]"; break;
        case K_VOICE: o.text = "[voice message - not supported here]"; break;
        default: o.control = true; break;
    }
    return true;
}

Bytes encodeText(const std::string& text) {
    Bytes b;
    b.push_back(K_TEXT);
    b.insert(b.end(), text.begin(), text.end());
    return b;
}

}  // namespace mc
