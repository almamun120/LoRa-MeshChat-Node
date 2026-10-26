// MeshChat relay core - byte helpers. Portable C++17 (no Arduino dependency) so it is unit-tested on a PC.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace mc {

using Bytes = std::vector<uint8_t>;

inline void put16(Bytes& b, uint16_t v) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
inline void put32(Bytes& b, uint32_t v) { for (int i = 3; i >= 0; i--) b.push_back((v >> (i * 8)) & 0xFF); }
inline void put64(Bytes& b, uint64_t v) { for (int i = 7; i >= 0; i--) b.push_back((v >> (i * 8)) & 0xFF); }
inline void putBytes(Bytes& b, const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }
inline void putBytes(Bytes& b, const Bytes& o) { b.insert(b.end(), o.begin(), o.end()); }
inline uint32_t get32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
inline uint64_t get64(const uint8_t* p) { return (uint64_t(get32(p)) << 32) | get32(p + 4); }

inline std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}
inline std::string hex(const Bytes& b) { return hex(b.data(), b.size()); }

inline bool unhex(const std::string& s, Bytes& out) {
    if (s.size() % 2) return false;
    out.clear();
    auto v = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
    for (size_t i = 0; i < s.size(); i += 2) { int a = v(s[i]), b = v(s[i + 1]); if (a < 0 || b < 0) return false; out.push_back(uint8_t(a * 16 + b)); }
    return true;
}

/** Node ID as the 8 hex characters the apps show ("7F3A92B1"). */
inline std::string idHex(uint32_t id) { Bytes b; put32(b, id); return hex(b); }
inline std::string idLabel(uint32_t id) { return "MC-" + idHex(id); }
inline bool parseId(const std::string& s, uint32_t& id) {
    std::string t = s;
    if (t.rfind("MC-", 0) == 0) t = t.substr(3);
    Bytes b;
    if (t.size() != 8 || !unhex(t, b)) return false;
    id = get32(b.data());
    return true;
}

constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

/** Cuts a UTF-8 string to at most maxBytes without splitting a character. */
inline std::string clampUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) n--;
    return s.substr(0, n);
}

}  // namespace mc
