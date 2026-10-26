// Bluetooth (BLE) keyboard: this relay dials out to ONE paired keyboard (the phones always dial in to the relay).
// All the slow Bluetooth work (scan, connect, pair, subscribe) runs in its own task, so LoRa, phones and Wi-Fi are never blocked.
// The key codes are turned into characters in lib/meshcore/mc_hid.* (tested on a PC).
#pragma once
#include <Arduino.h>
#include <deque>
#include <vector>
#include "mc_hid.h"
#include "settings.h"

class BtKeyboard {
public:
    struct Found { String name; String addr; int type = 0; int rssi = 0; };
    struct Status {
        bool enabled = false, connected = false, scanning = false;
        String state;                 // human readable: off / no keyboard paired / looking for ... / connected
        String name;                  // the paired keyboard
        uint32_t passkey = 0;         // != 0 while pairing needs the user to type this on the keyboard
        std::vector<Found> found;     // keyboards seen by the last search
    };

    void begin(Settings* s);          // after BleLink::begin() (NimBLE must already be initialised)
    void poll(uint32_t now);          // main loop: saves a new pairing to flash
    bool next(mc::KeyEvent& ev);      // key events typed on the keyboard
    void startSearch();               // looks for keyboards for 30 s (they must be in pairing mode)
    void pair(const String& addr, int type);
    void forget();
    Status status();
    bool connected() const { return connected_; }
    uint32_t passkey() const { return passkey_; }

    // from the Bluetooth task / callbacks
    void _task();
    void _notify(const uint8_t* d, size_t n);
    void _seen(const String& name, const String& addr, int type, int rssi, bool isHid);
    volatile bool _lost = false;

private:
    bool scan(int seconds, const String& target);
    bool connectTo(const String& addr, int type, const String& name, bool newPairing);
    void dropClient();

    Settings* set_ = nullptr;
    volatile bool enabled_ = true, connected_ = false, scanning_ = false;
    volatile bool cmdSearch_ = false, cmdPair_ = false, cmdForget_ = false;
    volatile uint32_t passkey_ = 0;
    String pairAddr_;
    int pairType_ = 0;
    String state_ = "starting";
    String name_;
    String target_;                    // while scanning for the saved keyboard
    volatile bool targetSeen_ = false;
    int targetType_ = 0;
    std::vector<Found> found_;
    std::deque<mc::KeyEvent> keys_;
    mc::HidKeyboardDecoder dec_;
    // set by the task, applied by poll() (Settings belong to the main thread)
    volatile bool saveNew_ = false, saveClear_ = false;
    String newAddr_, newName_;
    int newType_ = 0;
    uint32_t nextTry_ = 0;
};
