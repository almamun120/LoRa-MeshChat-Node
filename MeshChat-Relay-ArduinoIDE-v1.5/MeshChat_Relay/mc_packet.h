// Wire format shared with the Android app (core/Protocol.kt). Big-endian:
// ver(1) type(1) flags(1) ttl(1) hops(1) msgId(8) src(4) dst(4) timestamp(8) len(2) payload [sigLen(1) sig]
#pragma once
#include "mc_bytes.h"
#include <array>

namespace mc {

enum PacketType : uint8_t {
    T_HELLO = 1, T_IDENTITY = 2, T_ANNOUNCE = 3, T_PRIVATE = 4, T_ACK = 5, T_SYNC_INV = 6, T_SYNC_WANT = 7,
    T_MEDIA = 8, T_MEDIA_NACK = 9, T_CALL = 10, T_SOS = 11, T_IDENTITY_REQ = 12,
};
enum Channel : uint8_t { CH_MSG = 0, CH_SYNC = 1, CH_CONTROL = 2 };
inline Channel channelOf(uint8_t type) {
    if (type == T_HELLO) return CH_CONTROL;
    if (type == T_SYNC_INV || type == T_SYNC_WANT) return CH_SYNC;
    return CH_MSG;
}

constexpr int kVersion = 1;
constexpr size_t kHeaderSize = 31;
constexpr size_t kMaxPacket = 2048;
constexpr int kDefaultTtl = 15;
constexpr int kMaxHops = 16;
constexpr size_t kMaxNameBytes = 32;
constexpr size_t kMaxPostChars = 280;
constexpr size_t kMaxRouteAds = 40;
constexpr uint8_t kFlagSigned = 1;
constexpr uint8_t kCapChat = 1, kCapRelay = 2, kCapStore = 4;
/** v2.10: this node bridges to a LoRa radio. Phones use it to tell that a far-away contact is reached over a slow link. */
constexpr uint8_t kCapLora = 8;

struct Packet {
    uint8_t type = 0, flags = 0, ttl = 0, hops = 0;
    std::array<uint8_t, 8> msgId{};
    uint32_t src = 0, dst = 0;
    uint64_t ts = 0;
    Bytes payload;
    Bytes sig;

    bool isBroadcast() const { return dst == kBroadcast; }
    /** Duplicate-cache key (src + msgId), the same pair the app uses. */
    uint64_t key() const;
    std::string msgIdHex() const { return hex(msgId.data(), 8); }
    /** Bytes covered by the signature: type | msgId | src | dst | ts | payload (ttl and hops are excluded). */
    Bytes signedBytes() const;
    Bytes encode() const;
    /** Copy for relaying: ttl-1, hops+1. */
    Packet forwarded() const { Packet p = *this; p.ttl = p.ttl > 0 ? p.ttl - 1 : 0; p.hops++; return p; }
    static bool decode(const uint8_t* data, size_t len, Packet& out);
    static bool decode(const Bytes& b, Packet& out) { return decode(b.data(), b.size(), out); }
};

/** lora = the way to [node] crosses a LoRa radio link. Carried in the top bit of the hop byte. */
struct RouteAd { uint32_t node; uint8_t hops; bool lora = false; };
struct Hello { std::string name; uint8_t caps = 0; std::vector<RouteAd> routes; };
/** A weather station adds its reading to its identity: after the (possibly empty) e-mail come tempC*10 (int16) and hPa*10 (uint16), big-endian.
 *  Without weather the bytes are exactly the old format, so older builds keep accepting those identities. */
struct IdentityPayload { uint8_t caps = 0; std::string name; Bytes publicKey; std::string email; bool hasWx = false; int16_t wxT10 = 0; uint16_t wxP10 = 0; };
struct AnnouncePayload { std::string author; std::string content; };
struct SosPayload { bool active = false; int battery = 255; bool hasLoc = false; double lat = 0, lon = 0; int accuracyM = 0; std::string name; };

Bytes encodeHello(const Hello& h);
bool decodeHello(const Bytes& b, Hello& out);
Bytes encodeIdentity(const IdentityPayload& i);
bool decodeIdentity(const Bytes& b, IdentityPayload& out);
Bytes encodeAnnounce(const AnnouncePayload& a);
bool decodeAnnounce(const Bytes& b, AnnouncePayload& out);
bool decodeSos(const Bytes& b, SosPayload& out);

/** What a private message carries (ContentCodec.kt). Only text/location are shown; the rest is summarised. */
enum ContentKind : uint8_t { K_TEXT = 0, K_LOCATION = 1, K_IMAGE = 2, K_VOICE = 3, K_DELETE = 4, K_GROUP_INVITE = 5, K_GROUP_LEAVE = 6 };
struct Content {
    ContentKind kind = K_TEXT;
    std::string text;       // TEXT body, or a readable summary for other kinds
    bool inGroup = false;
    bool control = false;   // delete / group invite / leave: never shown
    int ttlSec = 0;
};
bool decodeContent(const Bytes& plain, Content& out);
Bytes encodeText(const std::string& text);

}  // namespace mc
