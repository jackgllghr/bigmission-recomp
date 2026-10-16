#include "save_state/ppsspp_thread.hpp"

#include <algorithm>

namespace sarusaru::save_state {
namespace {
template<class... Values> void words(PayloadReader &r, Values &...values) { ((values = r.u32()), ...); }
template<class... Values> void words(PayloadWriter &w, const Values &...values) { (w.u32(values), ...); }
template<std::size_t N> void array(PayloadReader &r, std::array<std::uint32_t, N> &values) {
    for (auto &v : values) v = r.u32();
}
template<std::size_t N> void array(PayloadWriter &w, const std::array<std::uint32_t, N> &values) {
    for (auto v : values) w.u32(v);
}
std::uint32_t count(PayloadReader &r, std::size_t width) {
    const auto n = r.u32();
    if (n > 4096 || n > r.remaining() / width) throw FormatError("PPST: invalid thread collection size");
    return n;
}
std::vector<std::uint32_t> vector(PayloadReader &r) {
    std::vector<std::uint32_t> values(count(r, 4));
    for (auto &v : values) v = r.u32();
    return values;
}
void vector(PayloadWriter &w, const std::vector<std::uint32_t> &values) {
    w.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto v : values) w.u32(v);
}
void validate(const PspThread &t) {
    if (t.callbacks.size() > 4096 || t.pending_calls.size() > 4096 || t.pushed_stacks.size() > 4096 ||
        t.end_waiters.size() > 4096 || t.paused_waits.size() > 4096)
        throw FormatError("PPST: too many thread continuations");
    // Dormant thread CPU data may be uninitialized. Validate executable CPU
    // state when adapting it, rather than rejecting a lossless file roundtrip.
}
} // namespace

psprecomp::AllegrexContext PspThreadContext::context() const {
    if (gpr[0] != 0 || (pc & 3u) != 0 || fp_condition > 1) throw FormatError("PPST: invalid blocked thread context");
    PpstCpu c;
    c.gpr = gpr; c.fpr = fpr; c.vfpu = vfpu; c.vfpu_control = vfpu_control;
    c.pc = pc; c.lo = lo; c.hi = hi; c.fcr31 = fcr31; c.fp_condition = fp_condition;
    return c.context();
}
void PspThreadContext::set_context(const psprecomp::AllegrexContext &context) {
    if (context.in_delay_slot) throw FormatError("PPST: a blocked thread cannot retain a delayed branch");
    PpstCpu c; c.set_context(context);
    gpr = c.gpr; fpr = c.fpr; vfpu = c.vfpu; vfpu_control = c.vfpu_control;
    pc = c.pc; lo = c.lo; hi = c.hi; fcr31 = c.fcr31; fp_condition = c.fp_condition;
}

PspThread decode_psp_thread(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize) throw FormatError("PPST: thread input exceeds limit");
    PayloadReader r(bytes); (void)r.section("Thread", 5, 5);
    PspThread t;
    t.native_size = r.u32(); const auto name = r.bytes(t.name.size()); std::copy(name.begin(), name.end(), t.name.begin());
    words(r, t.attributes, t.status, t.entry, t.initial_stack, t.stack_size, t.gp, t.initial_priority, t.priority,
        t.wait_type, t.wait_id, t.wakeup_count, t.exit_status);
    t.run_clocks = r.u64(); words(r, t.interrupt_preempts, t.thread_preempts, t.releases);
    words(r, t.wait_value, t.timeout_address, t.module_uid); t.processing_callbacks = r.boolean();
    words(r, t.mips_call_id, t.callback_id);
    auto &c = t.cpu; array(r, c.gpr); array(r, c.fpr); array(r, c.vfpu); array(r, c.vfpu_control);
    words(r, c.pc, c.lo, c.hi, c.fcr31, c.fp_condition, c.reserved);
    t.callbacks = vector(r); t.pending_calls = vector(r);
    for (auto n = count(r, 8); n != 0; --n) {
        const auto address = r.u32(), size = r.u32(); t.pushed_stacks.emplace_back(address, size);
    }
    t.current_stack.first = r.u32(); t.current_stack.second = r.u32();
    t.end_waiters = vector(r);
    for (auto n = count(r, 12); n != 0; --n) {
        const auto key = r.u32(); const auto ticks = r.u64();
        if (!t.paused_waits.emplace(key, ticks).second) throw FormatError("PPST: duplicate paused thread wait");
    }
    r.end_section(); r.finish(); validate(t);
    return t;
}
std::vector<std::uint8_t> encode_psp_thread(const PspThread &t) {
    validate(t);
    PayloadWriter w; w.section("Thread", 5); w.u32(t.native_size); w.bytes(t.name);
    words(w, t.attributes, t.status, t.entry, t.initial_stack, t.stack_size, t.gp, t.initial_priority, t.priority,
        t.wait_type, t.wait_id, t.wakeup_count, t.exit_status);
    w.u64(t.run_clocks); words(w, t.interrupt_preempts, t.thread_preempts, t.releases);
    words(w, t.wait_value, t.timeout_address, t.module_uid); w.boolean(t.processing_callbacks);
    words(w, t.mips_call_id, t.callback_id);
    const auto &c = t.cpu; array(w, c.gpr); array(w, c.fpr); array(w, c.vfpu); array(w, c.vfpu_control);
    words(w, c.pc, c.lo, c.hi, c.fcr31, c.fp_condition, c.reserved);
    vector(w, t.callbacks); vector(w, t.pending_calls); w.u32(static_cast<std::uint32_t>(t.pushed_stacks.size()));
    for (const auto &[address, size] : t.pushed_stacks) { w.u32(address); w.u32(size); }
    w.u32(t.current_stack.first); w.u32(t.current_stack.second); vector(w, t.end_waiters);
    w.u32(static_cast<std::uint32_t>(t.paused_waits.size()));
    for (const auto &[key, ticks] : t.paused_waits) { w.u32(key); w.u64(ticks); }
    w.end_section(); return w.data();
}
} // namespace sarusaru::save_state
