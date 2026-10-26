#include "mc_node.h"
#include <algorithm>

namespace mc {

static const uint32_t kBleRouteTtl = 60 * 1000;
static const uint32_t kLoraRouteTtl = 15 * 60 * 1000;
static const uint32_t kDupTtl = 10 * 60 * 1000;
static const size_t kSeenCap = 512;
static const size_t kContactCap = 48;
static const size_t kLogCap = 80;
static const size_t kMaxPrivateChars = 500;

static size_t utf8Chars(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

RelayNode::RelayNode(const crypto::Identity& id, const NodeConfig& cfg, Io io)
    : me_(id), cfg_(cfg), io_(std::move(io)), frag_(uint16_t(id.nodeId() >> 16)) {
    nextOwnBeacon_ = 20000 + rnd(20000);
}

void RelayNode::setConfig(const NodeConfig& c) { cfg_ = c; }

uint32_t RelayNode::rnd(uint32_t n) {
    uint8_t b[4];
    crypto::random(b, 4);
    return n ? get32(b) % n : 0;
}

void RelayNode::setWallClock(uint64_t epochMs) { wallBase_ = epochMs > now_ ? epochMs - now_ : 1; clockUser_ = true; }
uint64_t RelayNode::wallMs() const { return wallBase_ ? wallBase_ + now_ : now_; }

void RelayNode::observeTime(uint64_t ts) {
    if (clockUser_ || ts < 1600000000000ull || ts > 4000000000000ull) return;
    if (!wallBase_ || ts > wallMs()) wallBase_ = ts - now_;
}

void RelayNode::drop(const std::string& why) { stats_.dropped++; stats_.lastDrop = why; say("drop: " + why); }

// ------------------------------------------------------------------ duplicates, rate limit, routes

bool RelayNode::dup(const Packet& p) {
    // A message for somebody else is only "a duplicate" for a short guard time (long enough to swallow the echo of our own
    // LoRa transmission): the sender's retries must be able to pass again when the first copy was lost on the radio.
    // Everything else (broadcasts, messages for this node) is remembered for 10 minutes.
    uint32_t window = kDupTtl;
    if ((p.type == T_PRIVATE || p.type == T_ACK) && p.dst != id())
        window = std::max<uint32_t>(7000, std::min<uint32_t>(30000, 4 * loraAirtimeMs(kLoraMaxFrame, cfg_.loraSf, cfg_.loraBwHz, cfg_.loraCr)));
    uint64_t k = p.key();
    for (auto& s : seen_) if (s.key == k) {
        if (now_ - s.at <= window) return true;
        s.at = now_;
        return false;
    }
    if (seen_.size() < kSeenCap) seen_.push_back({k, now_});
    else { seen_[seenPos_ % kSeenCap] = {k, now_}; seenPos_++; }
    return false;
}

void RelayNode::forgetDup(const Packet& p) {
    uint64_t k = p.key();
    for (auto& s : seen_) if (s.key == k) s.key = 0;
}

bool RelayNode::allowPost(uint32_t src) {
    for (auto& b : buckets_) if (b.src == src) {
        b.tokens = std::min(5.0f, b.tokens + float(now_ - b.at) / 12000.0f);
        b.at = now_;
        if (b.tokens >= 1) { b.tokens -= 1; return true; }
        return false;
    }
    if (buckets_.size() > 40) buckets_.erase(buckets_.begin());
    buckets_.push_back({src, 4.0f, now_});
    return true;
}

bool RelayNode::allowIdReq(uint32_t src, uint32_t windowMs) {
    for (auto& r : idReqs_) if (r.src == src) {
        if (now_ - r.at < windowMs) return false;
        r.at = now_;
        return true;
    }
    if (idReqs_.size() >= 16) idReqs_.erase(idReqs_.begin());
    idReqs_.push_back({src, now_});
    return true;
}

uint32_t RelayNode::beaconInterval() const {
    // The base interval, stretched when the channel is busy (up to 4x): beacons are the first thing to give up airtime.
    uint32_t heard = std::max(loraWindowRx_, loraPrevRx_);
    uint32_t scale = std::min<uint32_t>(4, 1 + heard / 30);
    return cfg_.identityBeaconMs * scale;
}

void RelayNode::routeUpdate(uint32_t dest, uint32_t via, uint8_t hops, bool lora) {
    if (dest == id() || dest == kBroadcast) return;
    auto it = routes_.find(dest);
    if (it != routes_.end()) {
        const Route& e = it->second;
        uint32_t ttl = e.via == 0 ? kLoraRouteTtl : kBleRouteTtl;
        if (!(e.via == via || hops < e.hops || now_ - e.updated > ttl)) return;
    }
    routes_[dest] = {via, hops, now_, lora || via == 0};
}

const RelayNode::Route* RelayNode::routeFor(uint32_t dest) const {
    auto it = routes_.find(dest);
    if (it == routes_.end()) return nullptr;
    uint32_t ttl = it->second.via == 0 ? kLoraRouteTtl : kBleRouteTtl;
    return now_ - it->second.updated <= ttl ? &it->second : nullptr;
}

void RelayNode::learnRoute(const Packet& p, Origin o) {
    if (o.none) return;
    routeUpdate(p.src, o.lora ? 0 : o.peer, uint8_t(std::min<int>(p.hops + 1, 255)));
}

// ------------------------------------------------------------------ packets

Packet RelayNode::newPacket(uint8_t type, uint32_t dst, const Bytes& payload, int ttl, bool sign) {
    Packet p;
    p.type = type;
    p.ttl = uint8_t(ttl);
    crypto::random(p.msgId.data(), 8);
    p.src = id();
    p.dst = dst;
    p.ts = wallMs();
    p.payload = payload;
    if (sign) p.sig = crypto::sign(me_, p.signedBytes());
    return p;
}

Packet RelayNode::ownIdentityPacket(int ttl, bool withWeather) {
    IdentityPayload ip;
    ip.caps = cfg_.caps;
    ip.name = cfg_.name;
    ip.publicKey = me_.spki();
    // A reading older than 6 hours (sensor gone) is not announced any more. Plain beacons never carry weather: only the
    // weather interval the owner chose decides how often a report crosses the air.
    if (withWeather && wxOwn_ && now_ - wxMineAt_ < 6u * 3600u * 1000u) ip.hasWx = weatherToWire(wxMine_, ip.wxT10, ip.wxP10);
    return newPacket(T_IDENTITY, kBroadcast, encodeIdentity(ip), ttl, true);
}

void RelayNode::toBle(uint32_t peer, const Packet& p) {
    if (!io_.bleSend) return;
    if (io_.bleSend(peer, p.encode())) stats_.txBle++;
}

void RelayNode::sendHello(uint32_t peer) {
    Hello h;
    h.name = cfg_.name;
    h.caps = cfg_.caps;
    std::vector<std::pair<uint8_t, uint32_t>> ads;
    for (auto& kv : routes_) {
        const Route* r = routeFor(kv.first);
        if (!r || r->via == peer || kv.first == peer || r->hops >= kMaxHops) continue;
        ads.push_back({r->hops, kv.first});
    }
    std::sort(ads.begin(), ads.end());
    for (size_t i = 0; i < ads.size() && i < kMaxRouteAds; i++) {
        const Route* r = routeFor(ads[i].second);
        h.routes.push_back({ads[i].second, ads[i].first, r && (r->lora || r->via == 0)});   // phones learn which nodes are behind a radio link
    }
    toBle(peer, newPacket(T_HELLO, peer, encodeHello(h), 1, false));
}

void RelayNode::linkUp(uint32_t peer) {
    say("ble link up " + idLabel(peer));
    LinkState ls;
    ls.nextHello = now_ + 10000;
    ls.nextReplay = now_ + 300;
    for (auto& kv : contacts_) if (kv.first != peer) ls.replay.push_back(kv.first);
    links_[peer] = ls;
    routes_[peer] = {peer, 1, now_};
    sendHello(peer);
    toBle(peer, ownIdentityPacket(1, true));
}

void RelayNode::onBleLinkDown(uint32_t peer) {
    if (!links_.erase(peer)) return;
    for (auto it = routes_.begin(); it != routes_.end();) {
        if (it->second.via == peer || it->first == peer) it = routes_.erase(it); else ++it;
    }
    say("ble link down " + idLabel(peer));
}

void RelayNode::onBleFrame(uint32_t peer, const Bytes& bytes) {
    Packet p;
    if (!Packet::decode(bytes, p)) { drop("malformed packet (ble)"); return; }
    stats_.rxBle++;
    if (!links_.count(peer)) linkUp(peer);
    if (p.src == id()) return;
    Origin o;
    o.peer = peer;
    switch (p.type) {
        case T_HELLO: onHello(peer, p); break;
        case T_SYNC_INV: case T_SYNC_WANT: break;           // the app's post-sync: not needed here
        default: routable(p, o);
    }
}

void RelayNode::onHello(uint32_t from, const Packet& p) {
    if (p.src != from) { drop("hello src mismatch"); return; }
    Hello h;
    if (!decodeHello(p.payload, h)) { drop("bad hello"); return; }
    names_[from] = h.name;
    routeUpdate(from, from, 1);
    for (auto& ad : h.routes) {
        if (ad.node == id() || ad.node == from || ad.node == kBroadcast) continue;
        int hops = ad.hops + 1;
        if (hops > kMaxHops) continue;
        routeUpdate(ad.node, from, uint8_t(hops), ad.lora);
    }
}

void RelayNode::routable(const Packet& p, Origin o) {
    if (dup(p)) {
        if (p.type == T_PRIVATE && p.dst == id()) sendAck(p.src, p.msgId);   // our ACK probably got lost
        else stats_.duplicates++;
        return;
    }
    learnRoute(p, o);
    observeTime(p.ts);
    switch (p.type) {
        case T_IDENTITY: onIdentity(p, o); break;
        case T_ANNOUNCE: onAnnounce(p, o); break;
        case T_PRIVATE: onPrivate(p, o); break;
        case T_ACK: onAck(p, o); break;
        case T_SOS: onSos(p, o); break;
        case T_IDENTITY_REQ: onIdentityReq(p, o); break;
        case T_MEDIA: case T_MEDIA_NACK: case T_CALL: {
            // Pictures, voice and calls are too big for LoRa. Between two phones on this relay they pass straight through.
            if (p.dst == id() || p.isBroadcast() || p.ttl <= 1) { drop("media/call for this node"); break; }
            Packet f = p.forwarded();
            const Route* r = routeFor(f.dst);
            uint32_t via = links_.count(f.dst) ? f.dst : (r && r->via ? r->via : 0);
            if (via && links_.count(via) && !(!o.lora && !o.none && o.peer == via)) toBle(via, f);
            else drop("media: no BLE route");
            break;
        }
        default: drop("unexpected type");
    }
}

// ------------------------------------------------------------------ flooding and unicast

void RelayNode::floodBroadcast(const Packet& p, Origin o, bool toLora) {
    Packet f = p.forwarded();
    bool toPhones = o.lora || p.ttl > 1;     // a packet that reaches us from LoRa is always handed to our phones
    if (toPhones) {
        for (auto& kv : links_) {
            if (!o.lora && !o.none && kv.first == o.peer) continue;
            if (kv.first == p.src) continue;
            toBle(kv.first, f);
        }
        stats_.forwarded++;
    }
    if (toLora && p.ttl > 1 && cfg_.loraEnabled && loraType(p.type)) loraEnqueue(f, 150 + rnd(900), o.lora);
}

bool RelayNode::sendToward(const Packet& p, Origin from, bool allowLoraFlood) {
    bool fromBle = !from.lora && !from.none;
    if (links_.count(p.dst) && !(fromBle && from.peer == p.dst)) { toBle(p.dst, p); return true; }
    const Route* r = routeFor(p.dst);
    if (r && r->via && links_.count(r->via) && !(fromBle && from.peer == r->via)) { toBle(r->via, p); return true; }
    if (allowLoraFlood && cfg_.loraEnabled && loraType(p.type)) {
        loraEnqueue(p, from.none ? rnd(300) : 150 + rnd(900), from.lora);
        return true;
    }
    return false;
}

void RelayNode::forwardUnicast(const Packet& p, Origin o) {
    if (p.ttl <= 1) { drop("ttl expired"); return; }
    if (sendToward(p.forwarded(), o)) stats_.forwarded++;
    else drop("no route");
}

// ------------------------------------------------------------------ LoRa queue

void RelayNode::loraEnqueue(const Packet& pIn, uint32_t delayMs, bool cancelOnHear) {
    Packet p = pIn;
    // Public posts and identities do not need to cross the whole country on a slow shared channel; SOS and routed messages keep their reach.
    if ((p.type == T_ANNOUNCE || p.type == T_IDENTITY) && p.isBroadcast()) {
        int cap = std::max(1, std::min(cfg_.loraMaxTtl, kMaxHops));
        if (p.ttl > cap) p.ttl = uint8_t(cap);
    }
    Bytes b = p.encode();
    if (b.size() > kLoraMaxData * kLoraMaxFrames) { drop("too big for LoRa"); return; }
    uint64_t k = p.key();
    for (auto& j : jobs_) if (j.key == k) return;
    if (jobs_.size() >= 16) {
        auto it = std::find_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.cancelOnHear; });
        if (it == jobs_.end()) { drop("lora queue full"); return; }
        jobs_.erase(it);
    }
    Job j;
    j.pkt = std::move(b);
    j.key = k;
    j.due = now_ + delayMs;
    j.cancelOnHear = cancelOnHear;
    j.seq = jobSeq_++;
    jobs_.push_back(std::move(j));
}

void RelayNode::pumpLora() {
    if (!io_.loraSend || now_ < nextTxAllowed_ || jobs_.empty()) return;
    int best = -1;
    for (size_t i = 0; i < jobs_.size(); i++) {
        if (jobs_[i].due > now_) continue;
        if (best < 0 || jobs_[i].due < jobs_[best].due) best = int(i);
    }
    if (best < 0) return;
    if (io_.loraBusy && busyDefers_ < 8 && io_.loraBusy()) {      // listen before talk: wait a little, but never starve
        busyDefers_++;
        stats_.loraDeferred++;
        nextTxAllowed_ = now_ + 40 + rnd(160);
        return;
    }
    busyDefers_ = 0;
    Job j = jobs_[best];
    jobs_.erase(jobs_.begin() + best);
    auto frames = frag_.split(j.pkt);
    uint32_t air = 0;
    for (auto& f : frames) {
        air += loraAirtimeMs(f.size(), cfg_.loraSf, cfg_.loraBwHz, cfg_.loraCr);
        io_.loraSend(f);
    }
    stats_.txLora++;
    int duty = std::max(1, std::min(100, cfg_.dutyPercent));
    nextTxAllowed_ = now_ + air * (100 / duty);
}

void RelayNode::onLoraFrame(const Bytes& frame) {
    Bytes bytes;
    if (!reasm_.accept(frame, now_, bytes)) return;
    Packet p;
    if (!Packet::decode(bytes, p)) { drop("malformed packet (lora)"); return; }
    stats_.rxLora++;
    loraWindowRx_++;
    if (p.src == id()) return;
    uint64_t k = p.key();
    for (auto it = jobs_.begin(); it != jobs_.end();) {      // somebody else already re-sent it: save the airtime
        if (it->key == k && it->cancelOnHear) { it = jobs_.erase(it); stats_.loraSaved++; } else ++it;
    }
    Origin o;
    o.lora = true;
    routable(p, o);
}

// ------------------------------------------------------------------ handlers

void RelayNode::onIdentity(const Packet& p, Origin o) {
    IdentityPayload ip;
    if (!decodeIdentity(p.payload, ip) || p.sig.empty()) { drop("bad identity"); return; }
    if (crypto::nodeIdOf(ip.publicKey) != p.src) { drop("identity: id/key mismatch"); return; }
    if (!crypto::verify(ip.publicKey, p.signedBytes(), p.sig)) { drop("identity: bad signature"); return; }
    auto it = contacts_.find(p.src);
    if (it != contacts_.end() && it->second.spki != ip.publicKey) { drop("identity: key changed (pinned)"); return; }
    bool fresh = it == contacts_.end();
    bool renamed = !fresh && it->second.name != ip.name;
    Contact& c = contacts_[p.src];
    c.id = p.src;
    c.name = ip.name;
    c.spki = ip.publicKey;
    if (!(ip.hasWx && !c.rawIdentity.empty())) c.rawIdentity = p.encode();   // a weather-bearing copy is not replayed later (it would show an old reading as new)
    c.lastSeen = now_;
    if (ip.hasWx) { c.hasWx = true; c.wx = weatherFromWire(ip.wxT10, ip.wxP10); c.wxAt = now_; }
    bool viaBle = !o.lora && !o.none;
    if (viaBle && (fresh || !c.viaBle)) c.nextBeacon = now_ + 60000 + rnd(30000);   // the identity itself goes out at once below; the first beacon repeats it later, in case that one was lost
    if (viaBle) c.viaBle = true; else if (!links_.count(p.src)) c.viaBle = false;
    if (contacts_.size() > kContactCap) {
        uint32_t victim = 0, worst = 0;
        bool have = false;
        for (auto& kv : contacts_) {
            if (kv.first == p.src || links_.count(kv.first)) continue;
            uint32_t age = now_ - kv.second.lastSeen;
            if (!have || age > worst) { victim = kv.first; worst = age; have = true; }
        }
        if (have) contacts_.erase(victim);
    }
    if (!p.isBroadcast()) {
        // An answer to somebody's identity request: remembered above, now on its way to the one who asked.
        if (p.dst != id()) forwardUnicast(p, o);
        return;
    }
    if (viaBle && p.ttl <= 1) {
        // a phone just linked to us: let our other phones learn its key too (they cannot hear it directly)
        for (auto& kv : links_) if (kv.first != o.peer) toBle(kv.first, p.forwarded());
        return;
    }
    // Identities are not re-flooded over LoRa every time a phone repeats them: the beacon schedule (beacons()) carries a
    // phone's identity over the air at a calm pace, and anybody who needs a key sooner asks for it (IDENTITY_REQ).
    // What comes in over LoRa goes on only when it is news, to our phones and to the next LoRa hop.
    if (o.lora) {
        if (fresh || renamed || ip.hasWx) floodBroadcast(p, o, true);   // a weather report is news every time
        return;
    }
    if (p.ttl > 1) floodBroadcast(p, o, fresh || renamed || ip.hasWx);   // news goes out at once; repeats of a known identity stay on Bluetooth
}

void RelayNode::onIdentityReq(const Packet& p, Origin o) {
    if (p.isBroadcast()) { drop("identity request without target"); return; }
    bool askerDirect = !o.lora && !o.none && links_.count(p.src) && o.peer == p.src;
    if (p.dst == id()) {
        if (!allowIdReq(p.src, askerDirect ? 1000 : 5000)) { drop("identity request: rate limit"); return; }
        Packet a = newPacket(T_IDENTITY, p.src, [&] {
            IdentityPayload ip;
            ip.caps = cfg_.caps;
            ip.name = cfg_.name;
            ip.publicKey = me_.spki();
            return encodeIdentity(ip);
        }(), kMaxHops, true);
        dup(a);
        stats_.idAnswered++;
        Origin none;
        none.none = true;
        sendToward(a, none);
        return;
    }
    // For a phone we have heard: answer from our memory instead of crossing the radio again (this is the whole point of a bridge).
    // The stored packet is the phone's own signed broadcast identity; it goes to the asker with one hop left, so nothing floods.
    auto it = contacts_.find(p.dst);
    if (it != contacts_.end() && askerDirect) {
        Packet cached;
        if (Packet::decode(it->second.rawIdentity, cached)) {
            if (!allowIdReq(p.src, 200)) { drop("identity request: rate limit"); return; }
            cached.ttl = 1;
            cached.hops = uint8_t(std::min<int>(cached.hops + 1, 255));
            toBle(p.src, cached);
            stats_.idAnswered++;
            return;
        }
    }
    forwardUnicast(p, o);
}

void RelayNode::onAnnounce(const Packet& p, Origin o) {
    AnnouncePayload a;
    if (!decodeAnnounce(p.payload, a) || trim(a.content).empty() || utf8Chars(a.content) > kMaxPostChars || !p.isBroadcast()) { drop("bad announce"); return; }
    if (p.sig.empty()) { drop("unsigned announce"); return; }
    if (!allowPost(p.src)) { drop("spam: rate limit"); return; }
    auto it = contacts_.find(p.src);
    if (it != contacts_.end() && !crypto::verify(it->second.spki, p.signedBytes(), p.sig)) { drop("announce: bad signature"); return; }
    ChatMsg m;
    m.ts = std::min<uint64_t>(p.ts, wallMs());
    m.from = p.src;
    m.to = kBroadcast;
    m.fromName = a.author;
    m.text = a.content;
    m.pub = true;
    m.via = o.lora ? 2 : 1;
    addLog(m);
    floodBroadcast(p, o);
}

void RelayNode::onPrivate(const Packet& p, Origin o) {
    if (p.dst != id()) { forwardUnicast(p, o); return; }
    auto it = contacts_.find(p.src);
    if (it == contacts_.end()) { forgetDup(p); drop("private: sender key unknown yet"); return; }
    Bytes plain;
    if (!crypto::decrypt(me_, it->second.spki, p.src, crypto::aad(p.src, p.dst, p.msgId.data()), p.payload, plain)) { drop("private: decrypt failed"); return; }
    Content c;
    if (!decodeContent(plain, c)) { drop("private: bad content"); return; }
    if (!c.control) {
        ChatMsg m;
        m.ts = std::min<uint64_t>(p.ts, wallMs());
        m.from = p.src;
        m.to = id();
        m.fromName = it->second.name;
        m.text = c.inGroup ? "[group] " + c.text : c.text;
        m.via = o.lora ? 2 : 1;
        addLog(m);
    }
    sendAck(p.src, p.msgId);
}

void RelayNode::onAck(const Packet& p, Origin o) {
    if (p.dst != id()) { forwardUnicast(p, o); return; }
    if (p.payload.size() != 8) return;
    for (auto it = out_.begin(); it != out_.end(); ++it) {
        if (it->dst == p.src && memcmp(it->pkt.msgId.data(), p.payload.data(), 8) == 0) {
            for (auto& m : log_) if (m.seq == it->logSeq) m.status = ChatMsg::DELIVERED;
            out_.erase(it);
            return;
        }
    }
}

void RelayNode::sendAck(uint32_t to, const std::array<uint8_t, 8>& msgId) {
    Bytes pl(msgId.begin(), msgId.end());
    Packet a = newPacket(T_ACK, to, pl, kMaxHops, false);
    dup(a);
    Origin none;
    none.none = true;
    sendToward(a, none);
}

void RelayNode::onSos(const Packet& p, Origin o) {
    SosPayload sp;
    if (!decodeSos(p.payload, sp)) { drop("bad sos"); return; }
    SosAlert a;
    a.id = p.src;
    a.name = sp.name;
    a.active = sp.active;
    a.battery = sp.battery;
    a.hasLoc = sp.hasLoc;
    a.lat = sp.lat;
    a.lon = sp.lon;
    a.ts = wallMs();
    bool replaced = false;
    for (auto& x : alerts_) if (x.id == a.id) { x = a; replaced = true; }
    if (!replaced) { alerts_.push_back(a); if (alerts_.size() > 10) alerts_.erase(alerts_.begin()); }
    ChatMsg m;
    m.ts = a.ts;
    m.from = p.src;
    m.to = kBroadcast;
    m.fromName = sp.name.empty() ? idLabel(p.src) : sp.name;
    m.dir = ChatMsg::SYS;
    m.text = sp.active ? "SOS! emergency call" : "is safe now (SOS ended)";
    if (sp.active && sp.hasLoc) { char b[64]; snprintf(b, sizeof b, " - location %.5f, %.5f", sp.lat, sp.lon); m.text += b; }
    m.via = o.lora ? 2 : 1;
    addLog(m);
    floodBroadcast(p, o);
}

// ------------------------------------------------------------------ own chat

void RelayNode::addLog(ChatMsg m) {
    m.seq = ++seq_;
    if (!m.ts) m.ts = wallMs();
    log_.push_back(std::move(m));
    while (log_.size() > kLogCap) log_.pop_front();
}

std::string RelayNode::nameOf(uint32_t who) const {
    if (who == kBroadcast) return "All";
    if (who == id()) return cfg_.name;
    auto it = contacts_.find(who);
    if (it != contacts_.end() && !it->second.name.empty()) return it->second.name;
    auto n = names_.find(who);
    if (n != names_.end() && !n->second.empty()) return n->second;
    return idLabel(who);
}

bool RelayNode::sendPublic(const std::string& author, const std::string& textIn, std::string* err) {
    std::string text = trim(textIn);
    if (text.empty()) { if (err) *err = "empty message"; return false; }
    if (utf8Chars(text) > kMaxPostChars) { if (err) *err = "too long (max 280 characters)"; return false; }
    AnnouncePayload a;
    a.author = author.empty() ? cfg_.name : author;
    a.content = text;
    Packet p = newPacket(T_ANNOUNCE, kBroadcast, encodeAnnounce(a), kDefaultTtl, true);
    dup(p);
    for (auto& kv : links_) toBle(kv.first, p);
    if (cfg_.loraEnabled) loraEnqueue(p, rnd(300), false);
    ChatMsg m;
    m.from = id();
    m.to = kBroadcast;
    m.fromName = clampUtf8(a.author, kMaxNameBytes);
    m.text = text;
    m.dir = ChatMsg::OUT;
    m.pub = true;
    m.status = ChatMsg::SENT;
    addLog(m);
    return true;
}

bool RelayNode::sendWeather(const WeatherReading& r) {
    int16_t t; uint16_t pr;
    if (!weatherToWire(r, t, pr)) return false;
    wxMine_ = r;
    wxOwn_ = true;
    wxMineAt_ = now_;
    for (auto& kv : links_) toBle(kv.first, ownIdentityPacket(1, true));                    // phones on this relay
    if (cfg_.loraEnabled) loraEnqueue(ownIdentityPacket(5, true), rnd(300), false);          // the rest of the network (hop limit applied there)
    return true;
}

std::vector<Station> RelayNode::stations() const {
    std::vector<Station> v;
    if (wxOwn_) { Station s; s.id = id(); s.name = cfg_.name; s.r = wxMine_; s.heardAt = wxMineAt_; s.local = true; v.push_back(s); }
    for (auto& kv : contacts_) if (kv.second.hasWx) {
        Station s;
        s.id = kv.first; s.name = kv.second.name; s.r = kv.second.wx; s.heardAt = kv.second.wxAt; s.via = kv.second.viaBle ? 1 : 2;
        v.push_back(s);
    }
    std::sort(v.begin(), v.end(), [](const Station& a, const Station& b) { if (a.local != b.local) return a.local; return a.name < b.name; });
    return v;
}

bool RelayNode::sendPrivate(uint32_t dst, const std::string& textIn, const std::string& label, std::string* err) {
    std::string text = trim(textIn);
    if (text.empty()) { if (err) *err = "empty message"; return false; }
    if (dst == id() || dst == kBroadcast) { if (err) *err = "choose another person"; return false; }
    auto it = contacts_.find(dst);
    if (it == contacts_.end()) {
        if (allowIdReq(dst, 8000)) {                                  // ask for it; the answer is stored by onIdentity
            Packet rq = newPacket(T_IDENTITY_REQ, dst, Bytes(), kMaxHops, false);
            dup(rq);
            Origin none;
            none.none = true;
            sendToward(rq, none);
            Packet mine = ownIdentityPacket(kMaxHops);
            mine.dst = dst;
            mine.sig = crypto::sign(me_, mine.signedBytes());
            dup(mine);
            sendToward(mine, none);
        }
        if (err) *err = "this person's key is not known yet - asked for it, try again in a moment";
        return false;
    }
    std::string body = label.empty() ? text : "[" + clampUtf8(label, kMaxNameBytes) + "] " + text;
    if (utf8Chars(body) > kMaxPrivateChars) { if (err) *err = "too long (max 500 characters)"; return false; }
    Packet p;
    p.type = T_PRIVATE;
    p.ttl = kMaxHops;
    crypto::random(p.msgId.data(), 8);
    p.src = id();
    p.dst = dst;
    p.ts = wallMs();
    if (!crypto::encrypt(me_, it->second.spki, dst, crypto::aad(p.src, p.dst, p.msgId.data()), encodeText(body), p.payload)) {
        if (err) *err = "encryption failed";
        return false;
    }
    dup(p);
    ChatMsg m;
    m.from = id();
    m.to = dst;
    m.fromName = cfg_.name;
    m.text = text;
    m.dir = ChatMsg::OUT;
    m.status = ChatMsg::PENDING;
    addLog(m);
    OutMsg om;
    om.dst = dst;
    om.pkt = p;
    om.logSeq = seq_;
    om.attempts = 1;
    om.nextAt = now_ + 30000;
    out_.push_back(om);
    Origin none;
    none.none = true;
    if (sendToward(p, none)) for (auto& x : log_) if (x.seq == seq_ && x.status == ChatMsg::PENDING) x.status = ChatMsg::SENT;
    return true;
}

// ------------------------------------------------------------------ periodic work

void RelayNode::beacons() {
    if (!cfg_.loraEnabled) return;
    if (now_ >= nextOwnBeacon_) {
        loraEnqueue(ownIdentityPacket(5), 0, false);
        nextOwnBeacon_ = now_ + (ownBeacons_++ < 2 ? 90000 : beaconInterval()) + rnd(30000);
    }
    for (auto& kv : contacts_) {
        Contact& c = kv.second;
        if (!c.viaBle || !links_.count(c.id) || now_ < c.nextBeacon) continue;
        Packet p;
        if (Packet::decode(c.rawIdentity, p)) {
            p.ttl = 5;                         // ttl is not covered by the signature, so it can be raised for the LoRa hop
            loraEnqueue(p, 0, false);
        }
        c.nextBeacon = now_ + (c.beacons++ < 2 ? 90000 : beaconInterval()) + rnd(60000);
        break;                                 // one per tick keeps the air quiet
    }
}

void RelayNode::tick(uint32_t nowMs) {
    now_ = nowMs;
    for (auto& kv : links_) {
        LinkState& ls = kv.second;
        if (now_ >= ls.nextHello) { sendHello(kv.first); ls.nextHello = now_ + 10000; }
        if (!ls.replay.empty() && now_ >= ls.nextReplay) {
            uint32_t cid = ls.replay.front();
            ls.replay.pop_front();
            auto it = contacts_.find(cid);
            Packet p;
            if (it != contacts_.end() && Packet::decode(it->second.rawIdentity, p)) { p.ttl = 1; p.hops++; toBle(kv.first, p); }
            ls.nextReplay = now_ + 250;
        }
    }
    static const uint32_t backoff[] = {30000, 60000, 120000, 240000, 480000};
    for (auto it = out_.begin(); it != out_.end();) {
        if (now_ < it->nextAt) { ++it; continue; }
        if (it->attempts >= 5) {
            for (auto& m : log_) if (m.seq == it->logSeq && m.status != ChatMsg::DELIVERED) m.status = ChatMsg::FAILED;
            it = out_.erase(it);
            continue;
        }
        Origin none;
        none.none = true;
        sendToward(it->pkt, none);
        it->nextAt = now_ + backoff[std::min(it->attempts, 4)];
        it->attempts++;
        ++it;
    }
    if (now_ - loraWindowStart_ >= 10 * 60 * 1000) { loraPrevRx_ = loraWindowRx_; loraWindowRx_ = 0; loraWindowStart_ = now_; }
    beacons();
    pumpLora();
    if (now_ >= nextHousekeeping_) {
        nextHousekeeping_ = now_ + 5000;
        for (auto it = routes_.begin(); it != routes_.end();) {
            uint32_t ttl = it->second.via == 0 ? kLoraRouteTtl : kBleRouteTtl;
            if (now_ - it->second.updated > ttl) it = routes_.erase(it); else ++it;
        }
    }
}

std::vector<Contact> RelayNode::contacts() const {
    std::vector<Contact> v;
    for (auto& kv : contacts_) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const Contact& a, const Contact& b) { return a.name < b.name; });
    return v;
}

std::vector<uint32_t> RelayNode::bleLinks() const {
    std::vector<uint32_t> v;
    for (auto& kv : links_) v.push_back(kv.first);
    return v;
}

}  // namespace mc
