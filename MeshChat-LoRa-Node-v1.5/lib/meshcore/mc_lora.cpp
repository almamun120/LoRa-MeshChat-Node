#include "mc_lora.h"
#include <cmath>
#include <algorithm>

namespace mc {

std::vector<Bytes> LoraFragmenter::split(const Bytes& packet) {
    size_t total = std::max<size_t>(1, (packet.size() + kLoraMaxData - 1) / kLoraMaxData);
    if (total > kLoraMaxFrames) return {};
    uint8_t tag = tag_++;
    std::vector<Bytes> out;
    for (size_t i = 0; i < total; i++) {
        size_t from = i * kLoraMaxData, to = std::min(packet.size(), from + kLoraMaxData);
        Bytes f;
        f.push_back(kLoraMagic);
        f.push_back(txId_ >> 8);
        f.push_back(txId_ & 0xFF);
        f.push_back(tag);
        f.push_back(uint8_t((i << 4) | total));
        f.insert(f.end(), packet.begin() + from, packet.begin() + to);
        out.push_back(std::move(f));
    }
    return out;
}

bool LoraReassembler::accept(const Bytes& f, uint32_t now, Bytes& packet) {
    if (f.size() <= kLoraHeader || f[0] != kLoraMagic) return false;
    uint32_t key = (uint32_t(f[1]) << 16) | (uint32_t(f[2]) << 8) | f[3];
    int idx = f[4] >> 4, total = f[4] & 15;
    if (total == 0 || idx >= total) return false;
    for (auto it = open_.begin(); it != open_.end();) {
        if (now - it->second.started > 30000) it = open_.erase(it); else ++it;
    }
    if (total == 1) { packet.assign(f.begin() + kLoraHeader, f.end()); return true; }
    if (open_.size() > 8 && !open_.count(key)) open_.erase(open_.begin());
    Part& p = open_[key];
    if (p.total != total) {                    // new transfer under the same key (tag counter wrapped): start over
        p = Part();
        p.total = uint8_t(total);
        p.parts.assign(total, Bytes());
        p.have.assign(total, false);
        p.started = now;
    }
    if (!p.have[idx]) { p.parts[idx].assign(f.begin() + kLoraHeader, f.end()); p.have[idx] = true; p.got++; }
    if (p.got < p.total) return false;
    packet.clear();
    for (auto& part : p.parts) putBytes(packet, part);
    open_.erase(key);
    return true;
}

uint32_t loraAirtimeMs(size_t pl, int sf, int bw, int cr, int preamble) {
    double tsym = double(1u << sf) / bw * 1000.0;
    int de = (sf >= 11 && bw <= 125000) ? 1 : 0;
    double num = 8.0 * pl - 4.0 * sf + 28 + 16 - 0;       // CRC on, explicit header
    double sym = 8 + std::max(std::ceil(num / (4.0 * (sf - 2 * de))) * cr, 0.0);
    return uint32_t(std::ceil((preamble + 4.25 + sym) * tsym));
}

}  // namespace mc
