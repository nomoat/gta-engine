#include "bundle.h"
#include <cstring>

namespace engine {

namespace {
const char* MAGIC = "BNDL1";
const size_t MAGIC_LEN = 5;

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    bool fail = false;

    bool take(void* dst, size_t len) {
        if (fail || pos + len > n) { fail = true; return false; }
        std::memcpy(dst, p + pos, len);
        pos += len;
        return true;
    }
    uint8_t u8() { uint8_t v = 0; take(&v, 1); return v; }
    uint16_t u16() { uint8_t b[2]; take(b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }
    uint32_t u32() { uint8_t b[4]; take(b, 4); return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)); }
    uint64_t u64() {
        uint8_t b[8]; take(b, 8);
        uint64_t v = 0;
        for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
        return v;
    }
};

struct Writer {
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { out.push_back((uint8_t)(v & 0xFF)); out.push_back((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; i++) out.push_back((uint8_t)(v >> (8 * i))); }
    void bytes(const void* p, size_t n) { out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
};
} // namespace

const BundleEntry* Bundle::find(const std::string& name) const {
    for (const auto& e : entries)
        if (e.name == name) return &e;
    return nullptr;
}

bool parse_bundle_header(const uint8_t* data, size_t size, Bundle& out) {
    if (!data || size < MAGIC_LEN) return false;
    if (std::memcmp(data, MAGIC, MAGIC_LEN) != 0) return false;

    Reader r{data, size};
    r.pos = MAGIC_LEN;
    uint32_t count = r.u32();
    if (r.fail) return false;

    Bundle b;
    b.entries.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        BundleEntry e;
        uint16_t nameLen = r.u16();
        e.name.resize(nameLen);
        if (!r.take(e.name.data(), nameLen)) return false;
        e.type = (AssetType)r.u8();
        e.offset = r.u64();
        e.length = r.u64();
        if (r.fail) return false;
        b.entries.push_back(std::move(e));
    }
    out = std::move(b);
    return true;
}

std::vector<uint8_t> build_bundle_header(const std::vector<BundleEntry>& entries) {
    Writer w;
    w.bytes(MAGIC, MAGIC_LEN);
    w.u32((uint32_t)entries.size());
    for (const auto& e : entries) {
        w.u16((uint16_t)e.name.size());
        w.bytes(e.name.data(), e.name.size());
        w.u8((uint8_t)e.type);
        w.u64(e.offset);
        w.u64(e.length);
    }
    return w.out;
}

} // namespace engine
