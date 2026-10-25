// C++ side of the interop check: reads what the app's Kotlin code produced, runs it through a RelayNode, writes what
// the relay answers.   usage: interop <kotlin_out.txt> <cpp_out.txt>
#include "../lib/meshcore/mc_node.h"
#include "../lib/meshcore/mc_frames.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

using namespace mc;
static int fails = 0;
static void check(const char* name, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", name); if (!ok) fails++; }

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::map<std::string, std::string> kv;
    std::ifstream in(argv[1]);
    for (std::string line; std::getline(in, line);) { auto e = line.find('='); if (e != std::string::npos) kv[line.substr(0, e)] = line.substr(e + 1); }
    auto H = [&](const char* k) { Bytes b; unhex(kv[k], b); return b; };

    Bytes dpriv = H("D_PRIV");
    crypto::Identity d;
    check("relay identity loaded from the app-generated key", crypto::fromPrivate(d, dpriv.data()) && d.spki() == H("D_SPKI") && idHex(d.nodeId()) == kv["D_ID"]);
    Bytes aspki = H("A_SPKI");
    check("Node ID of the phone key matches the app", idHex(crypto::nodeIdOf(aspki)) == kv["A_ID"]);
    uint32_t aid = crypto::nodeIdOf(aspki);

    // the app's own packets, decoded and verified by the C++ code
    Packet ip;
    check("app IDENTITY decodes", Packet::decode(H("A_IDENTITY"), ip) && ip.type == T_IDENTITY && ip.sig.size() > 0);
    IdentityPayload idp;
    check("app IDENTITY payload (with e-mail) decodes", decodeIdentity(ip.payload, idp) && idp.name == "Rahim" && idp.email == "rahim@example.com");
    check("app IDENTITY signature verifies in C++", crypto::verify(idp.publicKey, ip.signedBytes(), ip.sig));
    Packet an;
    AnnouncePayload ap;
    check("app ANNOUNCE verifies in C++", Packet::decode(H("A_ANNOUNCE"), an) && decodeAnnounce(an.payload, ap) && crypto::verify(aspki, an.signedBytes(), an.sig) && ap.author == "Rahim");
    Packet sp;
    SosPayload sos;
    check("app SOS decodes (name, battery, location)", Packet::decode(H("A_SOS"), sp) && decodeSos(sp.payload, sos) && sos.active && sos.battery == 64 && sos.hasLoc && sos.name == "Rahim" && sos.lat > 23.81 && sos.lat < 23.82);
    Packet pm;
    check("app PRIVATE decodes", Packet::decode(H("A_TO_D"), pm) && pm.type == T_PRIVATE && pm.dst == d.nodeId());
    Bytes plain;
    Content c;
    check("app PRIVATE decrypts in C++", crypto::decrypt(d, aspki, aid, crypto::aad(pm.src, pm.dst, pm.msgId.data()), pm.payload, plain) && decodeContent(plain, c));
    Bytes textBytes = H("TEXT");
    std::string expected(textBytes.begin(), textBytes.end());
    check("decrypted text equals the Bangla text byte for byte", c.text == expected);

    {   // BLE framing must produce the same bytes as the app's Fragmenter
        FrameSplitter fs;
        std::string joined;
        for (auto& f : fs.split(H("FRAMES_SRC"), 100)) joined += (joined.empty() ? "" : ",") + hex(f);
        check("BLE frames are byte-identical to the app's Fragmenter", joined == kv["FRAMES"]);
    }
    // now run the real packets through a RelayNode and capture what it sends back to the phone
    NodeConfig cfg;
    cfg.name = "TestRelay";
    cfg.loraEnabled = false;
    std::vector<Bytes> toPhone;
    RelayNode::Io io;
    io.bleSend = [&](uint32_t, const Bytes& b) { toPhone.push_back(b); return true; };
    RelayNode node(d, cfg, io);
    node.tick(1000);
    node.onBleFrame(aid, H("A_HELLO"));                 // binds the link, relay answers with HELLO + IDENTITY
    node.onBleFrame(aid, H("A_IDENTITY"));
    node.onBleFrame(aid, H("A_ANNOUNCE"));
    node.onBleFrame(aid, H("A_SOS"));
    node.onBleFrame(aid, H("A_TO_D"));
    node.onBleFrame(aid, H("A_TO_D_ENV"));
    check("relay knows the phone's name from its IDENTITY", node.nameOf(aid) == "Rahim");
    int texts = 0, posts = 0, env = 0;
    for (auto& m : node.log()) {
        if (!m.pub && m.dir == ChatMsg::IN && m.text == expected) texts++;
        if (!m.pub && m.text == "with envelope") env++;
        if (m.pub && m.text == "public post: " + expected && m.fromName == "Rahim") posts++;
    }
    check("relay shows the private message from the app", texts == 1);
    check("relay shows the message that has reply+timer envelope", env == 1);
    check("relay shows the public post from the app", posts == 1);
    check("relay recorded the SOS", node.alerts().size() == 1 && node.alerts()[0].hasLoc && node.alerts()[0].battery == 64);

    // the relay learned a route from the phone's HELLO (0A0B0C0D at 2 hops) and answers with it +1
    node.tick(2000);
    node.tick(12000);            // next periodic HELLO
    Bytes dHello, dIdent, dAck;
    for (auto& b : toPhone) {
        Packet p;
        if (!Packet::decode(b, p)) continue;
        if (p.type == T_HELLO) dHello = b;      // the latest one wins
        if (p.type == T_IDENTITY && p.src == d.nodeId()) dIdent = b;
        if (p.type == T_ACK && dAck.empty()) dAck = b;
    }
    // The route ad for 0A0B0C0D goes to OTHER phones, not back to the one that taught it (split horizon): add a second phone.
    std::vector<Bytes> toB;
    NodeConfig cfg2 = cfg;
    RelayNode::Io io2;
    io2.bleSend = [&](uint32_t peer, const Bytes& b) { if (peer == 0x99000001) toB.push_back(b); else toPhone.push_back(b); return true; };
    RelayNode node2(d, cfg2, io2);
    node2.tick(1000);
    node2.onBleFrame(aid, H("A_HELLO"));
    Packet helloB;
    Hello hb;
    hb.name = "B"; hb.caps = 7;
    helloB.type = T_HELLO; helloB.ttl = 1; crypto::random(helloB.msgId.data(), 8); helloB.src = 0x99000001; helloB.dst = d.nodeId(); helloB.ts = 1; helloB.payload = encodeHello(hb);
    node2.onBleFrame(0x99000001, helloB.encode());
    node2.tick(12000);
    for (auto& b : toB) { Packet p; if (Packet::decode(b, p) && p.type == T_HELLO) dHello = b; }
    Packet hp;
    Hello hh;
    check("relay HELLO to a second phone lists the first phone and its far node",
          Packet::decode(dHello, hp) && decodeHello(hp.payload, hh) && hh.routes.size() == 2);
    Bytes ackedId(pm.msgId.begin(), pm.msgId.end());
    Packet ackP;
    check("relay ACKed the app's private message", Packet::decode(dAck, ackP) && ackP.payload == ackedId && ackP.dst == aid);

    // relay speaks first: identity, announce, private message to the app's phone
    std::string err;
    node.setWallClock(1760000000000ull);
    check("relay can write to the phone (key known from IDENTITY)", node.sendPrivate(aid, expected, "Web:Sumi", &err));
    check("relay can post on the public board", node.sendPublic("Sumi (web)", "board: " + expected, &err));
    Packet lastPriv, lastAnn;
    for (auto& b : toPhone) { Packet p; if (!Packet::decode(b, p)) continue; if (p.type == T_PRIVATE) lastPriv = p; if (p.type == T_ANNOUNCE) lastAnn = p; }

    std::ofstream out(argv[2]);
    out << "D_IDENTITY=" << hex(dIdent) << "\n";
    out << "D_ANNOUNCE=" << hex(lastAnn.encode()) << "\n";
    out << "D_HELLO=" << hex(dHello) << "\n";
    out << "D_ACK=" << hex(dAck) << "\n";
    out << "ACKED_ID=" << hex(ackedId) << "\n";
    out << "D_TO_A=" << hex(lastPriv.encode()) << "\n";
    printf("%s\n", fails ? "CPP INTEROP: FAILED" : "CPP INTEROP: ALL OK");
    return fails ? 1 : 0;
}
