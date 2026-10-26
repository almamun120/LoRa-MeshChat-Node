// LoRa side of the relay. LoRa frames are small (we use up to 200 bytes), so one mesh packet is cut into
// at most 15 frames:  magic(1) txId(2) tag(1) idx<<4|total(1) data...
// txId = first 2 bytes of the sending relay's Node ID, tag = a counter per sender, so two relays can talk at once.
#pragma once
#include "mc_bytes.h"
#include <map>

namespace mc {

constexpr uint8_t kLoraMagic = 0xA7;
constexpr size_t kLoraHeader = 5;
constexpr size_t kLoraMaxFrame = 255;                       // SX1278 maximum; keeps a typical IDENTITY or text packet in ONE radio frame
constexpr size_t kLoraMaxData = kLoraMaxFrame - kLoraHeader;
constexpr size_t kLoraMaxFrames = 8;                        // 8 x 250 = 2000 bytes: anything bigger is not sent over LoRa

class LoraFragmenter {
public:
    explicit LoraFragmenter(uint16_t txId) : txId_(txId) {}
    /** Returns the frames, or an empty list when the packet is too big for LoRa. */
    std::vector<Bytes> split(const Bytes& packet);
private:
    uint16_t txId_;
    uint8_t tag_ = 0;
};

class LoraReassembler {
public:
    /** Feed one received radio frame; returns true and fills [packet] when a whole mesh packet is complete. */
    bool accept(const Bytes& frame, uint32_t nowMs, Bytes& packet);
private:
    struct Part { uint8_t total = 0, got = 0; std::vector<Bytes> parts; std::vector<bool> have; uint32_t started = 0; };
    std::map<uint32_t, Part> open_;
};

/** Time on air in milliseconds (Semtech formula: explicit header, CRC on, 8 preamble symbols). cr = 5..8 means 4/5..4/8. */
uint32_t loraAirtimeMs(size_t payloadBytes, int sf, int bwHz, int cr = 5, int preamble = 8);

}  // namespace mc
