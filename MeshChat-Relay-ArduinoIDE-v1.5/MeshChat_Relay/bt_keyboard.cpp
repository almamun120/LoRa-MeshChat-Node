#include "bt_keyboard.h"
#include <NimBLEDevice.h>
#include <mutex>

using namespace mc;

static BtKeyboard* gSelf = nullptr;
static NimBLEClient* gClient = nullptr;
static std::mutex gMu;
static const uint16_t kHidService = 0x1812, kHidReport = 0x2A4D, kBootKbdInput = 0x2A22;

class KbScanCb : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override {
        if (!gSelf) return;
        bool hid = d->isAdvertisingService(NimBLEUUID(kHidService));
        if (!hid && d->haveAppearance()) { uint16_t a = d->getAppearance(); hid = a >= 0x03C0 && a <= 0x03C3; }   // HID generic / keyboard / mouse / joystick
        gSelf->_seen(String(d->getName().c_str()), String(d->getAddress().toString().c_str()), d->getAddress().getType(), d->getRSSI(), hid);
    }
};
static KbScanCb gScanCb;

class KbClientCb : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*) override { if (gSelf) gSelf->_lost = true; }
};
static KbClientCb gClientCb;

static void notifyCb(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { if (gSelf) gSelf->_notify(d, n); }
static void taskMain(void*) { if (gSelf) gSelf->_task(); vTaskDelete(nullptr); }

void BtKeyboard::begin(Settings* s) {
    set_ = s;
    gSelf = this;
    enabled_ = s->btKbOn;
    name_ = s->btKbName;
    state_ = enabled_ ? (s->btKbAddr.length() ? "looking for " + s->btKbName : "no keyboard paired") : "off";
    xTaskCreatePinnedToCore(taskMain, "btkb", 6144, nullptr, 1, nullptr, 0);
}

void BtKeyboard::poll(uint32_t) {
    if (saveNew_) {                                   // a keyboard was paired: remember it
        saveNew_ = false;
        set_->btKbAddr = newAddr_; set_->btKbType = newType_; set_->btKbName = newName_;
        settingsSave(*set_);
    }
    if (saveClear_) {
        saveClear_ = false;
        set_->btKbAddr = ""; set_->btKbName = "";
        settingsSave(*set_);
    }
}

bool BtKeyboard::next(KeyEvent& ev) {
    std::lock_guard<std::mutex> g(gMu);
    if (keys_.empty()) return false;
    ev = keys_.front();
    keys_.pop_front();
    return true;
}

void BtKeyboard::_notify(const uint8_t* d, size_t n) {
    std::vector<KeyEvent> out;
    std::lock_guard<std::mutex> g(gMu);
    if (!dec_.report(d, n, out)) return;
    for (auto& e : out) if (keys_.size() < 64) keys_.push_back(e);
}

void BtKeyboard::_seen(const String& name, const String& addr, int type, int rssi, bool isHid) {
    std::lock_guard<std::mutex> g(gMu);
    if (target_.length() && addr.equalsIgnoreCase(target_)) {      // the saved keyboard is on the air: stop searching, connect
        targetSeen_ = true;
        targetType_ = type;
        NimBLEDevice::getScan()->stop();
        return;
    }
    if (!isHid || !scanning_ || target_.length()) return;
    for (auto& f : found_) if (f.addr == addr) { f.rssi = rssi; if (name.length()) f.name = name; return; }
    if (found_.size() >= 8) return;
    Found f;
    f.name = name.length() ? name : String("(no name)");
    f.addr = addr; f.type = type; f.rssi = rssi;
    found_.push_back(f);
}

void BtKeyboard::startSearch() { if (enabled_ && !connected_) cmdSearch_ = true; }
void BtKeyboard::pair(const String& addr, int type) { pairAddr_ = addr; pairType_ = type; cmdPair_ = true; }
void BtKeyboard::forget() { cmdForget_ = true; }

BtKeyboard::Status BtKeyboard::status() {
    std::lock_guard<std::mutex> g(gMu);
    Status s;
    s.enabled = enabled_;
    s.connected = connected_;
    s.scanning = scanning_;
    s.state = state_;
    s.name = name_;
    s.passkey = passkey_;
    s.found = found_;
    return s;
}

bool BtKeyboard::scan(int seconds, const String& target) {
    {
        std::lock_guard<std::mutex> g(gMu);
        target_ = target;
        targetSeen_ = false;
        if (!target.length()) found_.clear();
        scanning_ = true;
    }
    NimBLEScan* sc = NimBLEDevice::getScan();
    sc->setAdvertisedDeviceCallbacks(&gScanCb, false);
    sc->setActiveScan(true);
    sc->setInterval(100);                 // listen about 60 % of the time: phones keep their links and keep seeing our advertisement
    sc->setWindow(60);
    sc->start(seconds, false);            // blocks this task only
    sc->clearResults();
    std::lock_guard<std::mutex> g(gMu);
    scanning_ = false;
    bool seen = targetSeen_;
    target_ = "";
    return seen;
}

void BtKeyboard::dropClient() {
    if (gClient) {
        if (gClient->isConnected()) gClient->disconnect();
        NimBLEDevice::deleteClient(gClient);
        gClient = nullptr;
    }
    connected_ = false;
    std::lock_guard<std::mutex> g(gMu);
    dec_.reset();
    keys_.clear();
}

bool BtKeyboard::connectTo(const String& addr, int type, const String& name, bool newPairing) {
    dropClient();
    { std::lock_guard<std::mutex> g(gMu); state_ = "connecting to " + (name.length() ? name : addr); }
    // A six-digit code the user types on the keyboard (most keyboards ask for one; some pair without).
    uint32_t code = 100000 + (esp_random() % 900000);
    NimBLEDevice::setSecurityPasskey(code);
    if (newPairing) passkey_ = code;
    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCb, false);
    gClient->setConnectTimeout(10);
    String why;
    bool ok = false;
    do {
        if (!gClient->connect(NimBLEAddress(std::string(addr.c_str()), (uint8_t)type))) { why = "could not connect"; break; }
        if (!gClient->secureConnection()) { why = newPairing ? "pairing failed (wrong code?)" : "encryption failed"; break; }
        NimBLERemoteService* hid = gClient->getService(NimBLEUUID(kHidService));
        if (!hid) { why = "not a keyboard"; break; }
        int n = 0;
        auto* chars = hid->getCharacteristics(true);
        if (chars) for (auto* c : *chars) {
            NimBLEUUID u = c->getUUID();
            if ((u == NimBLEUUID(kHidReport) || u == NimBLEUUID(kBootKbdInput)) && c->canNotify()) {
                if (c->subscribe(true, notifyCb)) n++;
            }
        }
        if (!n) { why = "no key reports"; break; }
        ok = true;
    } while (false);
    passkey_ = 0;
    if (!ok) {
        dropClient();
        std::lock_guard<std::mutex> g(gMu);
        state_ = "failed: " + why;
        return false;
    }
    _lost = false;
    connected_ = true;
    {
        std::lock_guard<std::mutex> g(gMu);
        name_ = name.length() ? name : String("keyboard");
        state_ = "connected";
    }
    if (newPairing) { newAddr_ = addr; newType_ = type; newName_ = name_; saveNew_ = true; }
    return true;
}

void BtKeyboard::_task() {
    NimBLEDevice::setSecurityAuth(true, true, true);          // bonding, passkey, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);   // we show the code, the keyboard's owner types it
    for (;;) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        uint32_t now = millis();
        if (!enabled_) { if (connected_) dropClient(); continue; }
        if (connected_ && (_lost || !gClient || !gClient->isConnected())) {
            dropClient();
            std::lock_guard<std::mutex> g(gMu);
            state_ = set_->btKbAddr.length() ? "looking for " + set_->btKbName : "no keyboard paired";
            nextTry_ = now + 3000;
        }
        if (cmdForget_) {
            cmdForget_ = false;
            if (set_->btKbAddr.length()) NimBLEDevice::deleteBond(NimBLEAddress(std::string(set_->btKbAddr.c_str()), (uint8_t)set_->btKbType));
            dropClient();
            saveClear_ = true;
            std::lock_guard<std::mutex> g(gMu);
            name_ = "";
            state_ = "no keyboard paired";
            continue;
        }
        if (connected_) continue;
        if (cmdSearch_) {
            cmdSearch_ = false;
            { std::lock_guard<std::mutex> g(gMu); state_ = "searching (put the keyboard in pairing mode)"; }
            scan(30, "");
            std::lock_guard<std::mutex> g(gMu);
            state_ = found_.empty() ? "no keyboard found" : "choose a keyboard";
            continue;
        }
        if (cmdPair_) {
            cmdPair_ = false;
            String nm;
            { std::lock_guard<std::mutex> g(gMu); for (auto& f : found_) if (f.addr == pairAddr_) nm = f.name; }
            connectTo(pairAddr_, pairType_, nm, true);
            continue;
        }
        if (set_->btKbAddr.length() && now >= nextTry_) {           // the paired keyboard: look for it every 12 s, connect when it wakes up
            nextTry_ = now + 12000;
            if (scan(4, set_->btKbAddr)) connectTo(set_->btKbAddr, targetType_ ? targetType_ : set_->btKbType, set_->btKbName, false);
            else { std::lock_guard<std::mutex> g(gMu); state_ = "looking for " + set_->btKbName; }
        }
    }
}
