#include "save_state/ppsspp_objects.hpp"

#include <algorithm>

namespace sarusaru::save_state {
namespace {
template<class T> void named(PayloadReader &r, T &value) {
    value.native_size = r.u32();
    const auto bytes = r.bytes(value.name.size()); std::copy(bytes.begin(), bytes.end(), value.name.begin());
}
template<class T> void named(PayloadWriter &w, const T &value) { w.u32(value.native_size); w.bytes(value.name); }
std::uint32_t count(PayloadReader &r, std::size_t width) {
    const auto n = r.u32();
    if (n > 4096 || n > r.remaining() / width) throw FormatError("PPST: invalid synchronization collection size");
    return n;
}
PspEventWaiter waiter(PayloadReader &r) { return {r.u32(), r.u32(), r.u32(), r.u32(), r.u64()}; }
void waiter(PayloadWriter &w, const PspEventWaiter &v) {
    w.u32(v.thread); w.u32(v.bits); w.u32(v.mode); w.u32(v.output_address); w.u64(v.paused_timeout);
}
template<class T> void limit(const T &value) {
    if (value.waiters.size() > 4096 || value.paused_waits.size() > 4096)
        throw FormatError("PPST: too many synchronization waiters");
}
void finish(PayloadReader &r) { r.end_section(); r.finish(); }
void input_size(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize) throw FormatError("PPST: synchronization record exceeds limit");
}
} // namespace
PspSemaphore decode_psp_semaphore(std::span<const std::uint8_t> bytes) {
    input_size(bytes);
    PayloadReader r(bytes); (void)r.section("Semaphore", 1, 1);
    PspSemaphore s; named(r, s);
    s.attributes = r.u32(); s.initial_count = r.u32(); s.count = r.u32(); s.maximum = r.u32(); s.waiter_count = r.u32();
    for (auto n = count(r, 4); n != 0; --n) s.waiters.push_back(r.u32());
    for (auto n = count(r, 12); n != 0; --n) {
        const auto key = r.u32(); const auto ticks = r.u64();
        if (!s.paused_waits.emplace(key, ticks).second) throw FormatError("PPST: duplicate paused semaphore wait");
    }
    finish(r); return s;
}
std::vector<std::uint8_t> encode_psp_semaphore(const PspSemaphore &s) {
    limit(s); PayloadWriter w; w.section("Semaphore", 1); named(w, s);
    w.u32(s.attributes); w.u32(s.initial_count); w.u32(s.count); w.u32(s.maximum); w.u32(s.waiter_count);
    w.u32(static_cast<std::uint32_t>(s.waiters.size())); for (auto id : s.waiters) w.u32(id);
    w.u32(static_cast<std::uint32_t>(s.paused_waits.size()));
    for (const auto &[key, ticks] : s.paused_waits) { w.u32(key); w.u64(ticks); }
    w.end_section(); return w.data();
}
PspEventFlag decode_psp_event_flag(std::span<const std::uint8_t> bytes) {
    input_size(bytes);
    PayloadReader r(bytes); (void)r.section("EventFlag", 1, 1);
    PspEventFlag f; named(r, f);
    f.attributes = r.u32(); f.initial_pattern = r.u32(); f.pattern = r.u32(); f.waiter_count = r.u32();
    for (auto n = count(r, 24); n != 0; --n) f.waiters.push_back(waiter(r));
    for (auto n = count(r, 28); n != 0; --n) {
        const auto key = r.u32(); auto value = waiter(r);
        if (!f.paused_waits.emplace(key, value).second) throw FormatError("PPST: duplicate paused event wait");
    }
    finish(r); return f;
}
std::vector<std::uint8_t> encode_psp_event_flag(const PspEventFlag &f) {
    limit(f); PayloadWriter w; w.section("EventFlag", 1); named(w, f);
    w.u32(f.attributes); w.u32(f.initial_pattern); w.u32(f.pattern); w.u32(f.waiter_count);
    w.u32(static_cast<std::uint32_t>(f.waiters.size())); for (const auto &v : f.waiters) waiter(w, v);
    w.u32(static_cast<std::uint32_t>(f.paused_waits.size()));
    for (const auto &[key, value] : f.paused_waits) { w.u32(key); waiter(w, value); }
    w.end_section(); return w.data();
}
PspCallback decode_psp_callback(std::span<const std::uint8_t> bytes) {
    input_size(bytes);
    PayloadReader r(bytes); (void)r.section("Callback", 1, 1);
    PspCallback c; named(r, c);
    c.owner = r.u32(); c.entry = r.u32(); c.argument = r.u32(); c.notify_count = r.u32(); c.notify_argument = r.u32();
    for (auto &v : c.legacy_registers) v = r.u32();
    finish(r); return c;
}
std::vector<std::uint8_t> encode_psp_callback(const PspCallback &c) {
    PayloadWriter w; w.section("Callback", 1); named(w, c);
    w.u32(c.owner); w.u32(c.entry); w.u32(c.argument); w.u32(c.notify_count); w.u32(c.notify_argument);
    for (auto v : c.legacy_registers) w.u32(v);
    w.end_section(); return w.data();
}
} // namespace sarusaru::save_state
