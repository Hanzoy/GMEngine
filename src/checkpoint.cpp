#include "checkpoint.hpp"
#include <bit>
#include <cstring>
#include <set>
#include <stdexcept>
namespace gm {
namespace {
constexpr size_t Limit = 256ull * 1024 * 1024;
void check(bool value, const char *error) {
    if (!value)
        throw std::runtime_error(error);
}
uint64_t hash(std::span<const uint8_t> bytes) {
    uint64_t h = 14695981039346656037ull;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
struct Writer {
    std::vector<uint8_t> bytes;
    void block(std::span<const uint8_t> data) {
        check(data.size() <= Limit - bytes.size(), "Checkpoint exceeds limit");
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    void word(uint64_t v) {
        uint8_t raw[8];
        for (int i = 0; i < 8; ++i)
            raw[i] = uint8_t(v >> (i * 8));
        block(raw);
    }
    void text(const std::string &value) {
        word(value.size());
        block({reinterpret_cast<const uint8_t *>(value.data()), value.size()});
    }
};
struct Reader {
    std::span<const uint8_t> bytes;
    size_t offset = 0;
    std::span<const uint8_t> block(uint64_t n) {
        check(n <= bytes.size() - offset, "Truncated checkpoint");
        auto part = bytes.subspan(offset, size_t(n));
        offset += size_t(n);
        return part;
    }
    uint64_t word() {
        auto raw = block(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= uint64_t(raw[i]) << (i * 8);
        return v;
    }
    uint32_t count(uint32_t max) {
        auto n = word();
        check(n <= max, "Checkpoint count exceeds limit");
        return uint32_t(n);
    }
    std::string text() {
        auto raw = block(count(128));
        std::string s(reinterpret_cast<const char *>(raw.data()), raw.size());
        check(s.find('\0') == std::string::npos, "Invalid checkpoint string");
        return s;
    }
};
} // namespace
std::vector<uint8_t> encodeCheckpoint(const RuntimeCheckpoint &state) {
    Writer out;
    out.word(0x315641534d47ull);
    out.word(1);
    auto scene = encodeScene(state.scene);
    out.word(scene.size());
    out.block(scene);
    out.word(state.tick);
    out.word(state.runtimeSequence);
    out.word(std::bit_cast<uint64_t>(state.remainder));
    out.word(std::bit_cast<uint64_t>(state.dropped));
    out.word(state.systems.size());
    for (const auto &s : state.systems) {
        out.text(s.name);
        out.word(s.version);
        out.word(s.bytes.size());
        out.block(s.bytes);
    }
    out.word(state.events.size());
    for (const auto &e : state.events) {
        out.word(e.event.type);
        out.text(e.source);
        out.text(e.target);
        out.word(e.event.size);
        out.block({e.event.data, e.event.size});
    }
    out.word(state.contacts.size());
    for (const auto &[a, b] : state.contacts) {
        out.text(a);
        out.text(b);
    }
    out.word(hash(out.bytes));
    return out.bytes;
}
RuntimeCheckpoint decodeCheckpoint(std::span<const uint8_t> bytes) {
    check(bytes.size() >= 48 && bytes.size() <= Limit, "Invalid checkpoint size");
    Reader tail{bytes.last(8)};
    check(tail.word() == hash(bytes.first(bytes.size() - 8)),
          "Checkpoint checksum mismatch; current World retained");
    Reader in{bytes.first(bytes.size() - 8)};
    check(in.word() == 0x315641534d47ull && in.word() == 1, "Unsupported checkpoint format");
    RuntimeCheckpoint state;
    state.scene = decodeScene(in.block(in.word()));
    state.tick = in.word();
    state.runtimeSequence = in.word();
    check(state.runtimeSequence != 0, "Invalid runtime identity sequence");
    state.remainder = std::bit_cast<double>(in.word());
    state.dropped = std::bit_cast<double>(in.word());
    auto count = in.count(128);
    std::set<std::string> names;
    for (uint32_t i = 0; i < count; ++i) {
        SavedSystem s;
        s.name = in.text();
        check(!s.name.empty() && names.insert(s.name).second, "Duplicate System state");
        s.version = in.count(UINT32_MAX);
        check(s.version != 0, "Invalid System state version");
        auto data = in.block(in.count(64 * 1024 * 1024));
        s.bytes.assign(data.begin(), data.end());
        state.systems.push_back(std::move(s));
    }
    count = in.count(65536);
    for (uint32_t i = 0; i < count; ++i) {
        SavedEvent e;
        e.event.type = in.word();
        e.source = in.text();
        e.target = in.text();
        e.event.size = in.count(64);
        auto data = in.block(e.event.size);
        std::memcpy(e.event.data, data.data(), data.size());
        state.events.push_back(std::move(e));
    }
    count = in.count(30000);
    for (uint32_t i = 0; i < count; ++i) {
        auto a = in.text();
        auto b = in.text();
        state.contacts.emplace_back(std::move(a), std::move(b));
    }
    check(in.offset == in.bytes.size(), "Unexpected checkpoint trailing data");
    return state;
}
} // namespace gm
