// BLE side: the relay is a GATT server that looks exactly like a phone running MeshChat (same service and
// characteristics, same advertisement), so phones connect to it by themselves. Phones always dial in; the relay never dials out.
#pragma once
#include <Arduino.h>
#include <deque>
#include <functional>
#include <map>
#include "mc_bytes.h"
#include "mc_frames.h"
#include "mc_packet.h"

class BleLink {
public:
    std::function<void(uint32_t peer, const mc::Bytes& packet)> onPacket;   // a complete mesh packet from a bound phone
    std::function<void(uint32_t peer)> onDown;                              // a bound phone disconnected

    void begin(uint32_t myId, uint8_t caps);
    void poll(uint32_t nowMs);
    bool send(uint32_t peer, const mc::Bytes& packet);                      // queued; returns false if the phone is not linked
    size_t links() const;

    // called from the NimBLE task
    void _write(uint16_t conn, mc::Channel ch, const std::string& value);
    void _connected(uint16_t conn);

private:
    struct Ev { bool connect; uint16_t conn; mc::Channel ch; mc::Bytes data; };
    struct OutFrame { mc::Channel ch; mc::Bytes data; };
    struct Session {
        uint32_t peer = 0;                 // 0 = not bound yet (waiting for the phone's first HELLO)
        mc::FrameReassembler re[3];
        mc::FrameSplitter split;
        std::deque<OutFrame> out;
        uint32_t stuckSince = 0;
    };
    std::map<uint16_t, Session> sessions_;
    std::deque<Ev> inbox_;
    uint32_t myId_ = 0;
    bool wantAdv_ = false;
    uint32_t lastAdvCheck_ = 0;
    bool pump(uint16_t conn, Session& s, uint32_t now);
};
