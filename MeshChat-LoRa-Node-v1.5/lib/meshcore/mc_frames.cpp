#include "mc_frames.h"
#include <algorithm>

namespace mc {

std::vector<Bytes> FrameSplitter::split(const Bytes& data, size_t maxFrame) {
    size_t chunk = maxFrame > 3 ? maxFrame - 3 : 1;
    size_t total = std::max<size_t>(1, (data.size() + chunk - 1) / chunk);
    std::vector<Bytes> out;
    if (total > 255) return out;
    uint8_t id = next_++;
    for (size_t i = 0; i < total; i++) {
        size_t from = i * chunk, to = std::min(data.size(), from + chunk);
        Bytes f = {id, uint8_t(i), uint8_t(total)};
        f.insert(f.end(), data.begin() + from, data.begin() + to);
        out.push_back(std::move(f));
    }
    return out;
}

bool FrameReassembler::accept(const Bytes& f, uint32_t now, Bytes& packet) {
    if (f.size() < 3) return false;
    int id = f[0];
    size_t index = f[1], tot = f[2];
    if (tot == 0 || index >= tot) return false;
    if (index == 0) {
        reset();
        id_ = id; total_ = tot; expected_ = 0; started_ = now;
    } else if (id_ != id || tot != total_ || index != expected_ || now - started_ > 10000) {
        reset();
        return false;
    }
    buf_.insert(buf_.end(), f.begin() + 3, f.end());
    expected_ = index + 1;
    if (expected_ == total_) { packet = buf_; reset(); return true; }
    return false;
}

}  // namespace mc
