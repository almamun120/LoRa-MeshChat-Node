// BLE frame fragmentation, identical to the app's Fragmenter/Reassembler (core/Fragmenter.kt):
// frame = fragId(1) | index(1) | total(1) | data.   maxFrame = usable ATT payload (MTU - 3).
#pragma once
#include "mc_bytes.h"

namespace mc {

class FrameSplitter {
public:
    std::vector<Bytes> split(const Bytes& data, size_t maxFrame);
private:
    uint8_t next_ = 0;
};

/** One per (link, channel). Frames of one link arrive in order; any gap, id mismatch or timeout drops the partial packet. */
class FrameReassembler {
public:
    bool accept(const Bytes& frame, uint32_t nowMs, Bytes& packet);
private:
    int id_ = -1;
    size_t expected_ = 0, total_ = 0;
    uint32_t started_ = 0;
    Bytes buf_;
    void reset() { id_ = -1; expected_ = total_ = 0; buf_.clear(); }
};

}  // namespace mc
