// Host-side tests for the relay core (no ESP32 needed):  make -C test
#include "../lib/meshcore/mc_node.h"
#include "../lib/meshcore/mc_frames.h"
#include "../lib/meshcore/mc_ps2.h"
#include "../lib/meshcore/mc_alert.h"
#include "../lib/meshcore/mc_weather.h"
#include <cstdio>
#include <functional>
#include <memory>
#include <set>

using namespace mc;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---------------------------------------------------------------- a stand-in for the Android app's engine
struct Phone {
    crypto::Identity id;
    std::string name;
    RelayNode* node = nullptr;
    std::map<uint32_t, std::pair<std::string, Bytes>> keys;    // known identities
    std::vector<std::string> texts, posts;
    std::vector<Bytes> acks;
    std::vector<uint32_t> sosFrom;
    int received = 0;
    std::map<uint32_t, uint8_t> caps;
    int reqs = 0;
    std::set<uint32_t> loraAds, localAds;     // nodes the relay advertised to us: behind a radio link / reached over Bluetooth
    int announceTtl = -1;
    std::map<uint32_t, IdentityPayload> idPayloads;   // the last IDENTITY payload seen from each node (weather included)

    Phone(const std::string& n) : name(n) { crypto::generate(id); }
    uint32_t nid() const { return id.nodeId(); }

    Packet make(uint8_t type, uint32_t dst, const Bytes& payload, int ttl, bool sign) {
        Packet p;
        p.type = type; p.ttl = uint8_t(ttl);
        crypto::random(p.msgId.data(), 8);
        p.src = nid(); p.dst = dst; p.ts = 1760000000000ull; p.payload = payload;
        if (sign) p.sig = crypto::sign(id, p.signedBytes());
        return p;
    }
    void tx(const Packet& p) { node->onBleFrame(nid(), p.encode()); }
    void hello() { Hello h; h.name = name; h.caps = 7; tx(make(T_HELLO, node->id(), encodeHello(h), 1, false)); }
    void identity(int ttl) { IdentityPayload ip; ip.caps = 7; ip.name = name; ip.publicKey = id.spki(); tx(make(T_IDENTITY, kBroadcast, encodeIdentity(ip), ttl, true)); }
    void announce(const std::string& t, int ttl = 7) { tx(make(T_ANNOUNCE, kBroadcast, encodeAnnounce({name, t}), ttl, true)); }
    void request(uint32_t target) { tx(make(T_IDENTITY_REQ, target, {}, kMaxHops, false)); }
    void sos(bool active) {
        Bytes b = {uint8_t(active), 80, 0, uint8_t(name.size())};
        b.insert(b.end(), name.begin(), name.end());
        tx(make(T_SOS, kBroadcast, b, 7, true));
    }
    bool priv(uint32_t dst, const std::string& text, int ttl = 8) {
        auto it = keys.find(dst);
        if (it == keys.end()) return false;
        Packet p = make(T_PRIVATE, dst, {}, ttl, false);
        crypto::encrypt(id, it->second.second, dst, crypto::aad(p.src, p.dst, p.msgId.data()), encodeText(text), p.payload);
        tx(p);
        return true;
    }
    // what the node sends to us
    void rx(const Bytes& bytes) {
        Packet p;
        if (!Packet::decode(bytes, p)) return;
        received++;
        if (p.type == T_IDENTITY) {
            IdentityPayload ip;
            if (decodeIdentity(p.payload, ip) && crypto::nodeIdOf(ip.publicKey) == p.src && crypto::verify(ip.publicKey, p.signedBytes(), p.sig))
                { keys[p.src] = {ip.name, ip.publicKey}; caps[p.src] = ip.caps; idPayloads[p.src] = ip; }
        } else if (p.type == T_HELLO) {
            Hello h;
            if (decodeHello(p.payload, h)) for (auto& ad : h.routes) (ad.lora ? loraAds : localAds).insert(ad.node);
        } else if (p.type == T_IDENTITY_REQ) reqs++;
        else if (p.type == T_PRIVATE && p.dst == nid()) {
            auto it = keys.find(p.src);
            Bytes plain;
            Content c;
            if (it != keys.end() && crypto::decrypt(id, it->second.second, p.src, crypto::aad(p.src, p.dst, p.msgId.data()), p.payload, plain) && decodeContent(plain, c)) {
                texts.push_back(c.text);
                Packet a = make(T_ACK, p.src, Bytes(p.msgId.begin(), p.msgId.end()), 8, false);
                tx(a);
            }
        } else if (p.type == T_ACK && p.dst == nid()) acks.push_back(p.payload);
        else if (p.type == T_ANNOUNCE) { announceTtl = p.ttl; AnnouncePayload a; if (decodeAnnounce(p.payload, a)) posts.push_back(a.author + ": " + a.content); }
        else if (p.type == T_SOS) sosFrom.push_back(p.src);
    }
};

struct World {
    std::unique_ptr<RelayNode> n1, n2;
    std::vector<Phone*> phones1, phones2;
    uint32_t t = 0;
    int loraTx = 0;
    std::vector<uint32_t> txTimes, txTimes1;
    double loss = 0;           // share of LoRa frames that never arrive
    int bleToPhone = 0;

    World(int sf = 7) {
        NodeConfig c1, c2;
        c1.name = "Relay-A"; c2.name = "Relay-B";
        c1.loraSf = c2.loraSf = sf;
        crypto::Identity i1, i2;
        crypto::generate(i1, 0xF0); crypto::generate(i2, 0xF0);
        RelayNode::Io io1, io2;
        io1.bleSend = [this](uint32_t peer, const Bytes& b) { for (auto* p : phones1) if (p->nid() == peer) { p->rx(b); bleToPhone++; } return true; };
        io2.bleSend = [this](uint32_t peer, const Bytes& b) { for (auto* p : phones2) if (p->nid() == peer) { p->rx(b); bleToPhone++; } return true; };
        io1.loraSend = [this](const Bytes& f) { loraTx++; txTimes.push_back(t); txTimes1.push_back(t); if (drand48() >= loss) n2->onLoraFrame(f); return true; };
        io2.loraSend = [this](const Bytes& f) { loraTx++; txTimes.push_back(t); if (drand48() >= loss) n1->onLoraFrame(f); return true; };
        n1.reset(new RelayNode(i1, c1, io1));
        n2.reset(new RelayNode(i2, c2, io2));
    }
    void add(Phone& p, int relay) { p.node = relay == 1 ? n1.get() : n2.get(); (relay == 1 ? phones1 : phones2).push_back(&p); }
    void run(uint32_t ms) { for (uint32_t i = 0; i < ms; i += 50) { t += 50; n1->tick(t); n2->tick(t); } }
};

// ---------------------------------------------------------------- tests

static void testCrypto() {
    printf("crypto\n");
    crypto::Identity a, b;
    CHECK(crypto::generate(a) && crypto::generate(b));
    CHECK(a.spki().size() == 91);
    uint8_t pub[65];
    CHECK(crypto::rawFromSpki(a.spki(), pub) && memcmp(pub, a.pub, 65) == 0);
    crypto::Identity a2;
    CHECK(crypto::fromPrivate(a2, a.priv) && memcmp(a2.pub, a.pub, 65) == 0);
    Bytes msg = {1, 2, 3, 4, 5};
    Bytes sig = crypto::sign(a, msg);
    CHECK(sig.size() >= 8 && sig.size() <= 72 && sig[0] == 0x30);
    CHECK(crypto::verify(a.spki(), msg, sig));
    Bytes bad = msg; bad[0] ^= 1;
    CHECK(!crypto::verify(a.spki(), bad, sig));
    CHECK(!crypto::verify(b.spki(), msg, sig));
    uint8_t rs[64];
    CHECK(crypto::rawFromDer(sig, rs) && crypto::derFromRaw(rs) == sig);
    uint8_t mid[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Bytes aad = crypto::aad(a.nodeId(), b.nodeId(), mid), blob, plain;
    CHECK(crypto::encrypt(a, b.spki(), b.nodeId(), aad, encodeText("hello"), blob));
    CHECK(blob.size() == 12 + 6 + 16);
    CHECK(crypto::decrypt(b, a.spki(), a.nodeId(), aad, blob, plain) && plain == encodeText("hello"));
    Bytes aad2 = aad; aad2[0] ^= 1;
    CHECK(!crypto::decrypt(b, a.spki(), a.nodeId(), aad2, blob, plain));
    crypto::Identity hi;
    CHECK(crypto::generate(hi, 0xF0) && (hi.nodeId() >> 24) >= 0xF0 && hi.nodeId() != kBroadcast);
}

static void testPacket() {
    printf("packet\n");
    Packet p;
    p.type = T_PRIVATE; p.ttl = kMaxHops; p.hops = 2; p.src = 0x11223344; p.dst = 0x55667788; p.ts = 1760000000123ull;
    for (int i = 0; i < 8; i++) p.msgId[i] = uint8_t(i + 1);
    p.payload = {9, 8, 7};
    Bytes e = p.encode();
    CHECK(e.size() == kHeaderSize + 3);
    CHECK(e[0] == 1 && e[1] == 4 && e[3] == kMaxHops && e[4] == 2);
    Packet q;
    CHECK(Packet::decode(e, q) && q.src == p.src && q.dst == p.dst && q.ts == p.ts && q.payload == p.payload && q.msgId == p.msgId);
    p.sig = {0x30, 1, 2};
    CHECK(Packet::decode(p.encode(), q) && q.sig == p.sig && (q.flags & kFlagSigned));
    Bytes cut = e; cut.pop_back();
    CHECK(!Packet::decode(cut, q));
    Bytes extra = e; extra.push_back(0);
    CHECK(!Packet::decode(extra, q));
    Bytes badType = e; badType[1] = 99;
    CHECK(!Packet::decode(badType, q));
    Bytes badTtl = e; badTtl[3] = kMaxHops + 1;
    CHECK(!Packet::decode(badTtl, q));
    Hello h; h.name = "Nodé"; h.caps = 3; h.routes = {{0xAABBCCDD, 2}, {1, 5}};
    Hello h2;
    CHECK(decodeHello(encodeHello(h), h2) && h2.name == h.name && h2.routes.size() == 2 && h2.routes[0].node == 0xAABBCCDD && h2.routes[1].hops == 5);
    Content c;
    CHECK(decodeContent(encodeText("hi"), c) && c.text == "hi" && !c.control);
    Bytes grp = {0x80, 4};
    for (int i = 0; i < 16; i++) grp.push_back(uint8_t(i));
    grp.push_back('x');
    CHECK(decodeContent(grp, c) && c.inGroup && c.text == "x");
    CHECK(!decodeContent({}, c));
    CHECK(clampUtf8("aé", 2) == "a");
}

static void testLora() {
    printf("lora\n");
    LoraFragmenter f(0xABCD);
    LoraReassembler r;
    Bytes big(700);
    for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t(i * 7);
    auto frames = f.split(big);
    CHECK(frames.size() == 3);
    for (auto& fr : frames) CHECK(fr.size() <= kLoraMaxFrame);
    Bytes out;
    bool done = false;
    for (int i = int(frames.size()) - 1; i >= 0; i--) done = r.accept(frames[i], 1000, out);   // out of order
    CHECK(done && out == big);
    frames = f.split(big);
    done = false;
    for (size_t i = 0; i < frames.size(); i++) if (i != 2) done = r.accept(frames[i], 2000, out) || done;
    CHECK(!done);                                                                              // a lost frame never completes
    CHECK(f.split(Bytes(kLoraMaxData * kLoraMaxFrames + 1)).empty());
    CHECK(f.split(Bytes(10)).size() == 1);
    CHECK(loraAirtimeMs(20, 7, 125000) >= 56 && loraAirtimeMs(20, 7, 125000) <= 57);
    CHECK(loraAirtimeMs(200, 9, 125000) > loraAirtimeMs(200, 7, 125000));
}

static void testFramesAndKeys() {
    printf("frames + keyboard\n");
    FrameSplitter sp;
    FrameReassembler ra;
    Bytes big(500);
    for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t(i);
    auto fr = sp.split(big, 20);
    CHECK(fr.size() == 30 && fr[0][0] == 0 && fr[0][2] == 30 && fr[29][1] == 29);
    Bytes out;
    bool done = false;
    for (auto& f : fr) done = ra.accept(f, 100, out);
    CHECK(done && out == big);
    CHECK(sp.split(Bytes(10), 244)[0][0] == 1);                                  // next fragment id
    auto f2 = sp.split(big, 100);
    done = false;
    for (size_t i = 0; i < f2.size(); i++) if (i != 2) done = ra.accept(f2[i], 200, out) || done;
    CHECK(!done);                                                                // gap -> discarded
    CHECK(ra.accept(sp.split(Bytes{7, 7}, 244)[0], 300, out) && out == Bytes({7, 7}));   // and it recovers

    Ps2Decoder k;
    auto press = [&](std::initializer_list<uint8_t> codes) { KeyEvent last; for (uint8_t c : codes) { KeyEvent e = k.push(c); if (e.key != Key::None) last = e; } return last; };
    KeyEvent e = press({0x1C, 0xF0, 0x1C});
    CHECK(e.key == Key::Char && e.ch == 'a');
    e = press({0x12, 0x1C, 0xF0, 0x1C, 0xF0, 0x12});
    CHECK(e.ch == 'A');
    e = press({0x1C, 0xF0, 0x1C});
    CHECK(e.ch == 'a');                                                          // shift released
    e = press({0x12, 0x16, 0xF0, 0x16, 0xF0, 0x12});
    CHECK(e.ch == '!');
    CHECK(press({0x5A, 0xF0, 0x5A}).key == Key::Enter);
    CHECK(press({0x66}).key == Key::Backspace);
    CHECK(press({0x0D}).key == Key::Tab);
    CHECK(press({0xE0, 0x75}).key == Key::Up);
    CHECK(press({0xE0, 0xF0, 0x75}).key == Key::None);
    CHECK(press({0x58, 0xF0, 0x58, 0x1C}).ch == 'A');                            // caps lock
    CHECK(press({0x58, 0xF0, 0x58, 0x1C}).ch == 'a');
}

static void testRelayChain() {
    printf("relay chain  P1 - A ~LoRa~ B - P2\n");
    World w;
    Phone p1("Rahim"), p2("Karim");
    w.add(p1, 1); w.add(p2, 2);
    p1.hello(); p1.identity(1);
    p2.hello(); p2.identity(1);
    w.run(180000);
    CHECK(w.n1->bleLinks().size() == 1 && w.n2->bleLinks().size() == 1);
    CHECK(p1.keys.count(w.n1->id()) && p2.keys.count(w.n2->id()));          // the relay's own identity reaches its phone at link-up
    CHECK(p2.keys.count(p1.nid()));                                          // P1's key crossed LoRa and was replayed to P2
    CHECK(p1.keys.count(p2.nid()));
    CHECK(p1.keys.count(w.n2->id()) && p2.keys.count(w.n1->id()));           // relay identities beacon over LoRa as well

    // HELLO route ads: P2's relay must tell P2 that P1 is reachable
    int before = w.loraTx;
    CHECK(p1.priv(p2.nid(), "salam from Rahim"));
    w.run(20000);
    CHECK(p2.texts.size() == 1 && p2.texts[0] == "salam from Rahim");
    CHECK(p1.acks.size() == 1);
    CHECK(w.loraTx > before && w.loraTx - before < 12);

    p2.priv(p1.nid(), "hi back");
    w.run(20000);
    CHECK(p1.texts.size() == 1 && p1.texts[0] == "hi back");

    // duplicates: the same private packet fed twice is delivered once
    size_t n = p2.texts.size();
    Packet dupP = p1.make(T_PRIVATE, p2.nid(), {}, 8, false);
    crypto::encrypt(p1.id, p2.keys[p1.nid()].second == Bytes() ? p2.id.spki() : p2.id.spki(), p2.nid(), crypto::aad(dupP.src, dupP.dst, dupP.msgId.data()), encodeText("once"), dupP.payload);
    p1.tx(dupP); p1.tx(dupP);
    w.run(20000);
    CHECK(p2.texts.size() == n + 1);

    // public board
    p1.announce("hello everyone");
    w.run(20000);
    bool got = false;
    for (auto& s : p2.posts) if (s == "Rahim: hello everyone") got = true;
    CHECK(got);
    bool inLog = false;
    for (auto& m : w.n2->log()) if (m.pub && m.text == "hello everyone" && m.fromName == "Rahim") inLog = true;
    CHECK(inLog);

    // SOS
    p1.sos(true);
    w.run(20000);
    CHECK(!p2.sosFrom.empty() && p2.sosFrom[0] == p1.nid());
    CHECK(w.n2->alerts().size() == 1 && w.n2->alerts()[0].active && w.n2->alerts()[0].name == "Rahim");

    // the relay's own chat (keyboard / web) with a phone on the far side
    std::string err;
    CHECK(w.n1->sendPrivate(p2.nid(), "from the relay keyboard", "Web:Sumi", &err));
    w.run(30000);
    CHECK(!p2.texts.empty() && p2.texts.back() == "[Web:Sumi] from the relay keyboard");
    bool delivered = false;
    for (auto& m : w.n1->log()) if (m.dir == ChatMsg::OUT && !m.pub && m.status == ChatMsg::DELIVERED) delivered = true;
    CHECK(delivered);
    CHECK(w.n1->sendPublic("Sumi", "board post from relay", &err));
    w.run(20000);
    bool seen = false;
    for (auto& s : p2.posts) if (s == "Sumi: board post from relay") seen = true;
    CHECK(seen);
    CHECK(!w.n1->sendPrivate(0x01020304, "x", "", &err) && !err.empty());
    CHECK(!w.n1->sendPublic("a", "   ", &err));

    // a phone sends us a private message
    p2.priv(w.n2->id(), "to the relay itself");
    w.run(5000);
    bool rl = false;
    for (auto& m : w.n2->log()) if (m.text == "to the relay itself" && m.fromName == "Karim") rl = true;
    CHECK(rl);
    printf("  lora frames sent: %d, ble frames to phones: %d\n", w.loraTx, w.bleToPhone);
}

static void testLimits() {
    printf("limits\n");
    World w;
    Phone p1("Rahim"), p2("Karim");
    w.add(p1, 1); w.add(p2, 2);
    p1.hello(); p1.identity(1); p2.hello(); p2.identity(1);
    w.run(120000);
    // ttl=1 private from a phone must not be forwarded
    int n = int(p2.texts.size());
    p1.priv(p2.nid(), "ttl one", 1);
    w.run(20000);
    CHECK(int(p2.texts.size()) == n);
    // forged identity is ignored
    crypto::Identity evil;
    crypto::generate(evil);
    IdentityPayload ip; ip.caps = 7; ip.name = "Fake"; ip.publicKey = evil.spki();
    Packet f = p1.make(T_IDENTITY, kBroadcast, encodeIdentity(ip), 7, true);   // signed by p1 but src is p1, key is evil: id/key mismatch
    p1.tx(f);
    Packet g = p1.make(T_IDENTITY, kBroadcast, encodeIdentity(ip), 7, false);
    g.src = evil.nodeId(); g.sig = Bytes{0x30, 6, 2, 1, 1, 2, 1, 1};            // right id, wrong signature
    p1.tx(g);
    w.run(10000);
    CHECK(!w.n1->hasContact(evil.nodeId()));
    // duty cycle: gaps between transmissions grow with airtime
    World d(12);
    Phone a("A"), b("B");
    d.add(a, 1); d.add(b, 2);
    a.hello(); a.identity(1);
    for (int i = 0; i < 6; i++) { a.announce("burst " + std::to_string(i)); }
    d.run(120000);
    uint32_t minGap = 1u << 30;
    for (size_t i = 1; i < d.txTimes1.size(); i++) if (d.txTimes1[i] != d.txTimes1[i - 1]) minGap = std::min(minGap, d.txTimes1[i] - d.txTimes1[i - 1]);
    printf("  SF12: relay A sent %zu frames, smallest gap between its packets %u ms\n", d.txTimes1.size(), minGap);
    CHECK(minGap >= 1000);
    // lossy LoRa: a message still gets through thanks to retries
    World l;
    l.loss = 0.35;
    Phone x("X"), y("Y");
    l.add(x, 1); l.add(y, 2);
    x.hello(); x.identity(1); y.hello(); y.identity(1);
    l.run(2400000);
    CHECK(x.keys.count(y.nid()) && y.keys.count(x.nid()));
    if (x.keys.count(y.nid())) {
        l.n2->tick(l.t);
        // the phone's own retry schedule (8, 16, 32, 64, 120 s) re-sends the identical packet
        Packet pk = x.make(T_PRIVATE, y.nid(), {}, 8, false);
        crypto::encrypt(x.id, x.keys[y.nid()].second, y.nid(), crypto::aad(pk.src, pk.dst, pk.msgId.data()), encodeText("through the noise"), pk.payload);
        uint32_t waits[] = {0, 8000, 16000, 32000, 64000, 120000};
        for (uint32_t wt : waits) { l.run(wt + 50); x.tx(pk); }
        l.run(300000);
        CHECK(!y.texts.empty());
    } else printf("  (identity exchange did not complete under loss - not asserted)\n");
}


static void testBridge() {
    printf("bridge (v2.10)\n");
    World w;
    Phone p1("Rahim"), p2("Karim"), p3("Sumi");
    w.add(p1, 1); w.add(p2, 2); w.add(p3, 2);
    p1.hello(); p1.identity(1);
    p2.hello(); p2.identity(1);
    p3.hello();                                                  // linked, but has not announced itself
    w.run(450000);
    // the relay says what it is: phones recognise a LoRa bridge by this capability bit
    CHECK(p1.caps.count(w.n1->id()) && (p1.caps[w.n1->id()] & kCapLora) && (p1.caps[w.n1->id()] & kCapRelay));
    CHECK(!(p1.caps[p2.nid()] & kCapLora));                      // an ordinary phone is not a bridge

    // the relay tells its phones which nodes are behind the radio link, so the app can refuse what cannot cross it
    CHECK(p2.loraAds.count(p1.nid()) == 1);                       // P1 is on the other side of LoRa
    CHECK(p2.localAds.count(p3.nid()) == 1 && p2.loraAds.count(p3.nid()) == 0);   // P3 shares the relay: Bluetooth, no flag
    CHECK(p1.loraAds.count(p2.nid()) == 1);
    Hello rt; rt.name = "x"; rt.routes = {{1, 16, true}, {2, 3, false}};
    Hello rt2;
    CHECK(decodeHello(encodeHello(rt), rt2) && rt2.routes[0].lora && rt2.routes[0].hops == 16 && !rt2.routes[1].lora && rt2.routes[1].hops == 3);

    // a phone that repeats its identity does not make the relay repeat it over the radio
    int before = w.loraTx;
    for (int i = 0; i < 5; i++) { p1.identity(15); w.run(500); }
    w.run(3000);
    CHECK(w.loraTx == before);

    // public posts and identities have a short reach on LoRa: ttl 15 from the phone arrives at the far phone with 5 left
    p1.announce("far away", 15);
    w.run(20000);
    CHECK(p2.announceTtl > 0 && p2.announceTtl <= w.n1->config().loraMaxTtl - 1);

    // identity request answered from the relay's memory: no radio traffic, one hop left
    p1.keys.erase(p2.nid());
    before = w.loraTx;
    p1.request(p2.nid());
    w.run(2000);
    CHECK(p1.keys.count(p2.nid()) && w.loraTx == before);
    CHECK(w.n1->stats().idAnswered >= 1);

    // a request for the relay itself: signed answer addressed to the asker
    p1.keys.erase(w.n1->id());
    p1.request(w.n1->id());
    w.run(2000);
    CHECK(p1.keys.count(w.n1->id()));

    // a request for a phone the relay has never heard: crosses LoRa, the phone answers, the answer crosses back
    CHECK(!p1.keys.count(p3.nid()));
    p1.request(p3.nid());
    w.run(30000);
    CHECK(p3.reqs == 1);
    IdentityPayload ip; ip.caps = 7; ip.name = "Sumi"; ip.publicKey = p3.id.spki();
    p3.tx(p3.make(T_IDENTITY, p1.nid(), encodeIdentity(ip), kMaxHops, true));
    w.run(60000);
    CHECK(p1.keys.count(p3.nid()));
    CHECK(w.n1->hasContact(p3.nid()));                           // and now the relay can answer for it
    CHECK(p1.priv(p3.nid(), "now we can talk"));
    w.run(60000);
    CHECK(p3.texts.size() == 1 && p3.texts[0] == "now we can talk");

    // the relay's own keyboard asks for a missing key instead of just failing
    std::string err;
    CHECK(!w.n1->sendPrivate(0x0A0B0C0D, "x", "", &err) && err.find("asked") != std::string::npos);

    // listen before talk: a busy channel delays a frame, but never forever
    int sent = 0;
    RelayNode::Io io;
    io.bleSend = [](uint32_t, const Bytes&) { return true; };
    io.loraSend = [&](const Bytes&) { sent++; return true; };
    io.loraBusy = [] { return true; };
    crypto::Identity id;
    crypto::generate(id, 0xF0);
    RelayNode n(id, NodeConfig(), io);
    Phone q("Q");
    q.node = &n;
    q.hello();
    n.tick(1000);
    q.announce("hello channel");
    for (uint32_t t = 1000; t < 20000; t += 50) n.tick(t);
    CHECK(sent >= 1);
    CHECK(n.stats().loraDeferred >= 1 && n.stats().loraDeferred <= 16);
}

// ---------------------------------------------------------------- indicator light + speaker
struct AlertRig {
    bool led = false;
    int hz = 0;
    std::vector<int> tones;                  // every tone that was started, in order
    int ledOn = 0;
    AlertPlayer p;
    uint32_t t = 0;
    AlertRig() : p(AlertPlayer::Out{[this](bool on) { led = on; if (on) ledOn++; }, [this](int h) { hz = h; if (h) tones.push_back(h); }}) {}
    void run(uint32_t ms) { for (uint32_t i = 0; i < ms; i += 10) { t += 10; p.tick(t); } }
};

static void testAlerts() {
    printf("alerts (LED + speaker)\n");
    {   AlertRig r;
        r.p.trigger(AlertKind::Private, r.t);
        r.run(50);
        CHECK(r.led && r.hz == 1500);                           // first beep of the private-message tone
        r.run(1000);
        CHECK(r.hz == 0 && r.tones.size() == 2);                // two beeps, then silence
        CHECK(r.led && r.p.unread());                           // the light stays on until a key is pressed
        r.p.acknowledge();
        r.run(20);
        CHECK(!r.led && !r.p.unread());
    }
    {   AlertRig r;                                             // quiet mode: light only
        r.p.configure(true, false);
        r.p.trigger(AlertKind::Private, r.t);
        r.run(500);
        CHECK(r.tones.empty() && r.led);
    }
    {   AlertRig r;                                             // switched off: nothing at all
        r.p.configure(false, true);
        r.p.trigger(AlertKind::Sos, r.t);
        r.p.trigger(AlertKind::Private, r.t);
        r.run(1000);
        CHECK(r.tones.empty() && !r.led);
    }
    {   AlertRig r;                                             // a busy public board is not a siren
        for (int i = 0; i < 10; i++) { r.p.trigger(AlertKind::Public, r.t); r.run(200); }
        CHECK(r.tones.size() <= 1 + 2000 / 3000 + 1);
    }
    {   AlertRig r;                                             // SOS beats a message and repeats until acknowledged
        r.p.trigger(AlertKind::Private, r.t);
        r.run(20);
        r.p.trigger(AlertKind::Sos, r.t);
        r.run(100);
        CHECK(r.hz == 2200 || r.hz == 0 || r.hz == 1800);
        size_t first = r.tones.size();
        r.run(12000);
        CHECK(r.tones.size() > first + 6);                      // it came again
        r.p.acknowledge();
        r.run(1500);                                            // let the pattern that was playing finish
        size_t n = r.tones.size();
        r.run(12000);
        CHECK(r.tones.size() == n);                             // silent after a key press
        r.p.sosActive(false, r.t);
        r.p.sosActive(true, r.t);                               // a NEW sos is loud again
        r.run(300);
        CHECK(r.tones.size() > n);
    }
    {   AlertRig r;                                             // LoRa traffic: a flash, never a sound
        r.p.trigger(AlertKind::LoraRx, r.t);
        r.run(20);
        CHECK(r.led && r.tones.empty());
        r.run(100);
        CHECK(!r.led);
    }
    {   AlertRig r;                                             // a quiet message does not cut off a louder one
        r.p.trigger(AlertKind::Sos, r.t);
        r.run(30);
        int before = int(r.tones.size());
        r.p.trigger(AlertKind::Public, r.t);
        r.run(30);
        CHECK(int(r.tones.size()) == before);
    }
}


static void testWeather() {
    printf("weather station (BMP280) - carried in the IDENTITY\n");
    // the worked example of the BMP280 datasheet: T = 25.08 degC, p = 100653.27 Pa
    uint8_t raw[24];
    int v[12] = {27504, 26435, -1000, 36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000};
    for (int i = 0; i < 12; i++) { raw[2 * i] = uint8_t(v[i] & 0xFF); raw[2 * i + 1] = uint8_t((v[i] >> 8) & 0xFF); }
    Bmp280Calib c = bmp280ParseCalib(raw);
    CHECK(c.T1 == 27504 && c.T2 == 26435 && c.T3 == -1000 && c.P1 == 36477 && c.P2 == -10685 && c.P9 == 6000);
    WeatherReading r;
    CHECK(bmp280Compensate(c, 519888, 415148, r));
    CHECK(r.tempC > 25.07f && r.tempC < 25.09f);
    CHECK(r.pressHpa > 1006.52f && r.pressHpa < 1006.54f);
    CHECK(!bmp280Compensate(c, 0x80000, 415148, r));              // skipped measurement
    CHECK(!bmp280Compensate(Bmp280Calib(), 519888, 415148, r));   // no calibration read

    // wire units
    int16_t t10; uint16_t p10;
    WeatherReading in; in.tempC = 27.54f; in.pressHpa = 1008.21f;
    CHECK(weatherToWire(in, t10, p10) && t10 == 275 && p10 == 10082);
    in.tempC = -4.0f; in.pressHpa = 995.0f;
    CHECK(weatherToWire(in, t10, p10) && t10 == -40 && p10 == 9950);
    in.tempC = 999.0f; CHECK(!weatherToWire(in, t10, p10));
    in.tempC = 20.0f; in.pressHpa = 5.0f; CHECK(!weatherToWire(in, t10, p10));

    // identity payload: no weather = the old bytes exactly; with weather = old bytes + empty e-mail slot + 4 bytes
    crypto::Identity id;
    crypto::generate(id, 0xF0);
    IdentityPayload ip;
    ip.caps = 11; ip.name = "Relay"; ip.publicKey = id.spki();
    Bytes plain = encodeIdentity(ip);
    ip.hasWx = true; ip.wxT10 = -40; ip.wxP10 = 9950;
    Bytes wx = encodeIdentity(ip);
    CHECK(wx.size() == plain.size() + 1 + 4);
    CHECK(std::equal(plain.begin(), plain.end(), wx.begin()));
    const uint8_t tail[5] = {0x00, 0xFF, 0xD8, 0x26, 0xDE};                 // same bytes the Android test expects (ProtocolTest.kt)
    CHECK(std::equal(tail, tail + 5, wx.end() - 5));
    IdentityPayload back;
    CHECK(decodeIdentity(wx, back) && back.hasWx && back.wxT10 == -40 && back.wxP10 == 9950 && back.email.empty() && back.name == "Relay");
    CHECK(decodeIdentity(plain, back) && !back.hasWx);
    ip.email = "a@b.c";
    CHECK(decodeIdentity(encodeIdentity(ip), back) && back.hasWx && back.email == "a@b.c" && back.wxT10 == -40);
    Bytes cut = wx; cut.pop_back();
    CHECK(!decodeIdentity(cut, back));                                      // a half weather field is malformed
    Bytes bad = wx; bad[bad.size() - 4] = 0x7F;                             // 32000 = 3200 degC
    CHECK(!decodeIdentity(bad, back));

    // two relays over LoRa, one phone on each
    World w;
    Phone p1("Rahim"), p2("Karim");
    w.add(p1, 1); w.add(p2, 2);
    p1.hello(); p2.hello();
    w.run(5000);
    CHECK(w.n1->stations().empty());
    in.tempC = 27.5f; in.pressHpa = 1008.2f;
    CHECK(w.n1->sendWeather(in));
    CHECK(!w.n1->sendWeather(WeatherReading{500.0f, 1000.0f}));
    w.run(20000);
    auto st1 = w.n1->stations();
    auto st2 = w.n2->stations();
    CHECK(st1.size() == 1 && st1[0].local && st1[0].name == "Relay-A" && st1[0].id == w.n1->id());
    CHECK(st2.size() == 1 && !st2[0].local && st2[0].id == w.n1->id() && st2[0].name == "Relay-A" && st2[0].via == 2);
    CHECK(st2.size() == 1 && st2[0].r.tempC > 27.4f && st2[0].r.tempC < 27.6f && st2[0].r.pressHpa > 1008.1f && st2[0].r.pressHpa < 1008.3f);
    CHECK(w.n1->log().empty() && w.n2->log().empty() && p1.posts.empty() && p2.posts.empty());   // no chat post: it is a field of the identity
    CHECK(p2.idPayloads.count(w.n1->id()) && p2.idPayloads[w.n1->id()].hasWx && p2.idPayloads[w.n1->id()].wxT10 == 275 && p2.idPayloads[w.n1->id()].wxP10 == 10082);
    CHECK(p1.idPayloads.count(w.n1->id()) && p1.idPayloads[w.n1->id()].hasWx);   // the relay's own phone too

    // B answers; both lists now hold both stations, local first
    WeatherReading rb; rb.tempC = 31.0f; rb.pressHpa = 1001.5f;
    w.n2->sendWeather(rb);
    w.run(20000);
    st1 = w.n1->stations();
    CHECK(st1.size() == 2 && st1[0].local && st1[1].name == "Relay-B" && st1[1].r.tempC > 30.9f);

    // a new report replaces the old one; one every 5 minutes for two hours, none lost to a limiter
    int updates = 0;
    float lastSeen = 0;
    for (int i = 0; i < 24; i++) {
        in.tempC = 20.0f + i * 0.5f;
        w.n1->sendWeather(in);
        w.run(5 * 60 * 1000);
        for (auto& s2 : w.n2->stations()) if (s2.id == w.n1->id() && s2.r.tempC != lastSeen) { updates++; lastSeen = s2.r.tempC; }
    }
    CHECK(updates >= 23);
    CHECK(lastSeen > 31.4f && lastSeen < 31.6f);
    CHECK(w.n2->stats().dropped == 0);

    // plain identity beacons do not carry weather (only the owner's weather interval decides how often a report crosses the air)
    int tx0 = w.loraTx;
    w.run(30 * 60 * 1000);
    CHECK(w.loraTx > tx0);
    CHECK(p2.idPayloads[w.n1->id()].wxT10 == 315);                          // still the last real report, not touched by beacons
}

int main(int argc, char** argv) {
    srand48(argc > 1 ? atoi(argv[1]) : 7);
    testCrypto();
    testPacket();
    testLora();
    testFramesAndKeys();
    testRelayChain();
    testLimits();
    testBridge();
    testAlerts();
    testWeather();
    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
