#include "ble_link.h"
#include <NimBLEDevice.h>
#include <mutex>
#include <algorithm>

using namespace mc;

// Same UUIDs as core/Protocol.kt
#define U(n) "4d455348-4348-4154-8000-00000000000" #n
static NimBLEServer* gServer = nullptr;
static NimBLECharacteristic *gTx = nullptr, *gSync = nullptr, *gCtrl = nullptr;
static std::mutex gMu;
static BleLink* gSelf = nullptr;

class CharCb : public NimBLECharacteristicCallbacks {
public:
    explicit CharCb(Channel ch) : ch_(ch) {}
    void onWrite(NimBLECharacteristic* c, ble_gap_conn_desc* desc) override {
        std::string v = c->getValue();
        if (gSelf) gSelf->_write(desc->conn_handle, ch_, v);
    }
private:
    Channel ch_;
};

class SrvCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override { if (gSelf) gSelf->_connected(desc->conn_handle); }
};

void BleLink::_write(uint16_t conn, Channel ch, const std::string& v) {
    std::lock_guard<std::mutex> g(gMu);
    if (inbox_.size() > 200) return;
    inbox_.push_back({false, conn, ch, Bytes(v.begin(), v.end())});
}

void BleLink::_connected(uint16_t conn) {
    std::lock_guard<std::mutex> g(gMu);
    inbox_.push_back({true, conn, CH_MSG, {}});
}

void BleLink::begin(uint32_t myId, uint8_t caps) {
    gSelf = this;
    myId_ = myId;
    NimBLEDevice::init("MeshRelay");
    NimBLEDevice::setMTU(247);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    gServer = NimBLEDevice::createServer();
    gServer->setCallbacks(new SrvCb());
    NimBLEService* svc = gServer->createService(U(1));

    // node info: proto(1) | caps(1) | nodeId(4). The phone checks it against the id it saw in the advertisement.
    uint8_t info[6] = {1, caps};
    for (int i = 0; i < 4; i++) info[2 + i] = (myId >> (24 - 8 * i)) & 0xFF;
    NimBLECharacteristic* ci = svc->createCharacteristic(U(2), NIMBLE_PROPERTY::READ);
    ci->setValue(info, 6);
    NimBLECharacteristic* rx = svc->createCharacteristic(U(3), NIMBLE_PROPERTY::WRITE);
    rx->setCallbacks(new CharCb(CH_MSG));
    gTx = svc->createCharacteristic(U(4), NIMBLE_PROPERTY::NOTIFY);
    gSync = svc->createCharacteristic(U(5), NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    gSync->setCallbacks(new CharCb(CH_SYNC));
    gCtrl = svc->createCharacteristic(U(6), NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    gCtrl->setCallbacks(new CharCb(CH_CONTROL));
    svc->start();

    // advertisement: flags(3) + 128-bit service UUID(18) + manufacturer data FFFF|proto|caps|nodeId (10) = 31 bytes, like the phones
    NimBLEAdvertisementData ad;
    ad.setFlags(0x06);
    ad.setCompleteServices(NimBLEUUID(U(1)));
    std::string m;
    m += char(0xFF); m += char(0xFF);
    m += char(info[0]); m += char(info[1]);
    for (int i = 2; i < 6; i++) m += char(info[i]);
    ad.setManufacturerData(m);
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAdvertisementData(ad);
    adv->start();
}

size_t BleLink::links() const {
    size_t n = 0;
    for (auto& kv : sessions_) if (kv.second.peer) n++;
    return n;
}

bool BleLink::send(uint32_t peer, const Bytes& packet) {
    if (packet.size() < 2) return false;
    for (auto& kv : sessions_) {
        Session& s = kv.second;
        if (s.peer != peer) continue;
        uint16_t mtu = gServer ? gServer->getPeerMTU(kv.first) : 23;
        if (mtu < 23) mtu = 23;
        size_t maxFrame = std::min<size_t>(mtu - 3, 244);
        Channel ch = channelOf(packet[1]);
        if (s.out.size() > 400) return false;              // phone is not reading: do not pile up memory
        for (auto& f : s.split.split(packet, maxFrame)) s.out.push_back({ch, std::move(f)});
        return true;
    }
    return false;
}

bool BleLink::pump(uint16_t conn, Session& s, uint32_t now) {
    for (int i = 0; i < 4 && !s.out.empty(); i++) {
        OutFrame& f = s.out.front();
        NimBLECharacteristic* c = f.ch == CH_MSG ? gTx : (f.ch == CH_SYNC ? gSync : gCtrl);
        os_mbuf* om = ble_hs_mbuf_from_flat(f.data.data(), f.data.size());
        if (!om) return true;
        int rc = ble_gatts_notify_custom(conn, c->getHandle(), om);   // takes the mbuf in every case
        if (rc != 0) {
            if (!s.stuckSince) s.stuckSince = now ? now : 1;
            return now - s.stuckSince < 8000;                         // false = give up on this link
        }
        s.stuckSince = 0;
        s.out.pop_front();
    }
    return true;
}

void BleLink::poll(uint32_t now) {
    std::deque<Ev> evs;
    {
        std::lock_guard<std::mutex> g(gMu);
        evs.swap(inbox_);
    }
    for (auto& e : evs) {
        if (e.connect) { sessions_[e.conn]; continue; }
        auto it = sessions_.find(e.conn);
        if (it == sessions_.end()) it = sessions_.emplace(e.conn, Session()).first;
        Session& s = it->second;
        Bytes pkt;
        if (!s.re[e.ch].accept(e.data, now, pkt)) continue;
        if (!s.peer) {
            // a link is bound to a phone by its first HELLO (link-local, src = the phone itself), exactly as the app does it
            if (pkt.size() < kHeaderSize || pkt[1] != T_HELLO) continue;
            uint32_t src = get32(pkt.data() + 13);
            if (src == myId_ || src == 0 || src == kBroadcast) continue;
            for (auto& kv : sessions_) if (kv.second.peer == src && kv.first != e.conn) {
                // the same phone came back on a new connection: the old one is stale
                uint32_t old = kv.second.peer;
                kv.second.peer = 0;
                if (onDown) onDown(old);
            }
            s.peer = src;
        }
        if (onPacket) onPacket(s.peer, pkt);
    }

    // disconnects
    if (gServer) {
        std::vector<uint16_t> alive = gServer->getPeerDevices();
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            bool up = std::find(alive.begin(), alive.end(), it->first) != alive.end();
            if (!up) {
                if (it->second.peer && onDown) onDown(it->second.peer);
                it = sessions_.erase(it);
            } else ++it;
        }
    }
    // frames out
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (!pump(it->first, it->second, now)) {
            if (gServer) gServer->disconnect(it->first);
            if (it->second.peer && onDown) onDown(it->second.peer);
            it = sessions_.erase(it);
        } else ++it;
    }
    // keep advertising while there is room for more phones
    if (now - lastAdvCheck_ > 2000) {
        lastAdvCheck_ = now;
        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        if (gServer && gServer->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS && !adv->isAdvertising()) adv->start();
    }
}
