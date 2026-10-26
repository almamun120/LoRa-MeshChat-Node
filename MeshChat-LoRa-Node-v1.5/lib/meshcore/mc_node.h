// The relay node's brain: BLE phones on one side, LoRa radio on the other, plus a chat identity of its own
// (used by the keyboard, the LCD and the Wi-Fi web page). No Arduino code in here, so it runs in unit tests.
#pragma once
#include "mc_packet.h"
#include "mc_crypto.h"
#include "mc_lora.h"
#include "mc_weather.h"
#include <deque>
#include <functional>
#include <map>

namespace mc {

struct NodeConfig {
    std::string name = "MeshRelay";
    uint8_t caps = kCapChat | kCapRelay | kCapLora;
    bool loraEnabled = true;          // forward between BLE and LoRa
    int loraSf = 9;
    int loraBwHz = 125000;
    int loraCr = 5;                   // 4/5
    int dutyPercent = 10;             // transmit at most this share of the time
    uint32_t identityBeaconMs = 12 * 60 * 1000;
    int loraMaxTtl = 6;               // hops a public post / identity may travel on LoRa (each hop is another relay, and costs airtime)
};

struct ChatMsg {
    uint32_t seq = 0;
    uint64_t ts = 0;                  // wall clock ms (as far as this node knows it)
    uint32_t from = 0, to = 0;        // to == kBroadcast: public board
    std::string fromName;
    std::string text;
    enum Dir : uint8_t { IN = 0, OUT = 1, SYS = 2 } dir = IN;
    enum Status : uint8_t { PENDING = 0, SENT = 1, DELIVERED = 2, FAILED = 3 } status = SENT;
    bool pub = false;
    uint8_t via = 0;                  // 0 local, 1 BLE, 2 LoRa
};

struct Contact {
    uint32_t id = 0;
    std::string name;
    Bytes spki;
    Bytes rawIdentity;                // the signed IDENTITY packet as received, replayed to new neighbours
    uint32_t lastSeen = 0;
    bool viaBle = false;              // this identity came in over BLE: this relay beacons it over LoRa
    uint32_t nextBeacon = 0;
    bool hasWx = false;               // its identity carried weather
    WeatherReading wx;
    uint32_t wxAt = 0;
    uint8_t beacons = 0;              // how many LoRa beacons were sent (the first ones come quickly, in case one is lost)
};

struct SosAlert { uint32_t id = 0; std::string name; bool active = false; int battery = 255; bool hasLoc = false; double lat = 0, lon = 0; uint64_t ts = 0; };

struct Stats { uint32_t rxBle = 0, rxLora = 0, txLora = 0, txBle = 0, forwarded = 0, dropped = 0, duplicates = 0, idAnswered = 0, loraDeferred = 0, loraSaved = 0; std::string lastDrop; };

class RelayNode {
public:
    struct Io {
        std::function<bool(uint32_t peer, const Bytes& packet)> bleSend;  // transport fragments for the link; false = failed
        std::function<bool(const Bytes& frame)> loraSend;                  // blocking transmit of one radio frame
        std::function<void(const std::string&)> log;
        std::function<bool()> loraBusy;                                    // optional: true while somebody else is on the channel (listen before talk)
    };

    RelayNode(const crypto::Identity& id, const NodeConfig& cfg, Io io);

    uint32_t id() const { return me_.nodeId(); }
    const NodeConfig& config() const { return cfg_; }
    void setConfig(const NodeConfig& c);

    // ---- time
    void setWallClock(uint64_t epochMs);
    uint64_t wallMs() const;
    bool clockSet() const { return wallBase_ != 0; }

    // ---- transport events
    /** A frame from a phone that finished a BLE link (its first HELLO must already have bound it to [peer]). */
    void onBleFrame(uint32_t peer, const Bytes& packet);
    void onBleLinkDown(uint32_t peer);
    void onLoraFrame(const Bytes& frame);
    void tick(uint32_t nowMs);

    // ---- chat (keyboard, LCD, web)
    bool sendPublic(const std::string& author, const std::string& text, std::string* err = nullptr);
    bool sendPrivate(uint32_t dst, const std::string& text, const std::string& senderLabel, std::string* err = nullptr);

    // ---- weather: our own BMP280 reading travels as one more field of our IDENTITY (name + Node ID + key + weather), so the
    // device list of every phone and relay shows it next to the name. Returns false for an unbelievable reading.
    bool sendWeather(const WeatherReading& r);
    /** This node (when it has a reading) and every contact that sent weather, local first, then by name. */
    std::vector<Station> stations() const;

    // ---- views
    const std::deque<ChatMsg>& log() const { return log_; }
    std::vector<Contact> contacts() const;
    std::vector<uint32_t> bleLinks() const;
    std::vector<SosAlert> alerts() const { return alerts_; }
    const Stats& stats() const { return stats_; }
    std::string nameOf(uint32_t id) const;
    size_t routeCount() const { return routes_.size(); }
    size_t loraQueueLen() const { return jobs_.size(); }
    bool hasContact(uint32_t id) const { return contacts_.count(id) != 0; }

private:
    struct Origin { bool lora = false; bool none = false; uint32_t peer = 0; };
    struct Route { uint32_t via = 0; uint8_t hops = 0; uint32_t updated = 0; bool lora = false; };   // via 0 = over LoRa
    struct Job { Bytes pkt; uint64_t key = 0; uint32_t due = 0; bool cancelOnHear = false; uint32_t seq = 0; };
    struct OutMsg { uint32_t dst = 0; Packet pkt; uint32_t logSeq = 0; int attempts = 0; uint32_t nextAt = 0; };
    struct LinkState { uint32_t nextHello = 0; std::deque<uint32_t> replay; uint32_t nextReplay = 0; };

    // dispatch
    void routable(const Packet& p, Origin o);
    void onHello(uint32_t from, const Packet& p);
    void onIdentity(const Packet& p, Origin o);
    void onAnnounce(const Packet& p, Origin o);
    void onPrivate(const Packet& p, Origin o);
    void onAck(const Packet& p, Origin o);
    void onSos(const Packet& p, Origin o);
    void onIdentityReq(const Packet& p, Origin o);

    // sending
    void linkUp(uint32_t peer);
    void sendHello(uint32_t peer);
    void floodBroadcast(const Packet& received, Origin o, bool toLora = true);
    void forwardUnicast(const Packet& received, Origin o);
    bool sendToward(const Packet& p, Origin from, bool allowLoraFlood = true);
    void toBle(uint32_t peer, const Packet& p);
    bool loraType(uint8_t t) const { return t == T_IDENTITY || t == T_ANNOUNCE || t == T_PRIVATE || t == T_ACK || t == T_SOS || t == T_IDENTITY_REQ; }
    void loraEnqueue(const Packet& p, uint32_t delayMs, bool cancelOnHear);
    void pumpLora();
    Packet newPacket(uint8_t type, uint32_t dst, const Bytes& payload, int ttl, bool sign);
    Packet ownIdentityPacket(int ttl, bool withWeather = false);
    void sendAck(uint32_t to, const std::array<uint8_t, 8>& msgId);
    void learnRoute(const Packet& p, Origin o);
    void routeUpdate(uint32_t dest, uint32_t via, uint8_t hops, bool lora = false);
    const Route* routeFor(uint32_t dest) const;
    void observeTime(uint64_t ts);
    void forgetDup(const Packet& p);
    void beacons();
    bool dup(const Packet& p);
    bool allowPost(uint32_t src);
    bool allowIdReq(uint32_t src, uint32_t windowMs);
    uint32_t beaconInterval() const;
    void addLog(ChatMsg m);
    void drop(const std::string& why);
    void say(const std::string& s) { if (io_.log) io_.log(s); }
    uint32_t rnd(uint32_t n);

    crypto::Identity me_;
    NodeConfig cfg_;
    Io io_;
    LoraFragmenter frag_;
    LoraReassembler reasm_;
    uint32_t now_ = 0;
    uint64_t wallBase_ = 0;           // wall = wallBase_ + now_ (0 = never set)
    uint32_t seq_ = 0, jobSeq_ = 0;

    std::map<uint32_t, LinkState> links_;
    std::map<uint32_t, Route> routes_;
    std::map<uint32_t, Contact> contacts_;
    std::map<uint32_t, std::string> names_;    // names phones announced in their HELLO
    bool clockUser_ = false;
    std::deque<ChatMsg> log_;
    bool wxOwn_ = false;              // we have a sensor reading of our own
    WeatherReading wxMine_;
    uint32_t wxMineAt_ = 0;
    std::vector<Job> jobs_;
    std::vector<OutMsg> out_;
    std::vector<SosAlert> alerts_;
    struct Seen { uint64_t key; uint32_t at; };
    std::vector<Seen> seen_;
    size_t seenPos_ = 0;
    struct Bucket { uint32_t src; float tokens; uint32_t at; };
    std::vector<Bucket> buckets_;
    uint32_t nextTxAllowed_ = 0;
    uint8_t busyDefers_ = 0;
    struct IdReqAt { uint32_t src; uint32_t at; };
    std::vector<IdReqAt> idReqs_;
    uint32_t loraWindowStart_ = 0, loraWindowRx_ = 0, loraPrevRx_ = 0;   // LoRa frames heard per 10 minutes: a busy channel slows our beacons
    uint32_t nextOwnBeacon_ = 0;
    uint8_t ownBeacons_ = 0;
    uint32_t nextHousekeeping_ = 0;
    Stats stats_;
};

}  // namespace mc
