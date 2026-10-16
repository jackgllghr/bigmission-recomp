#include "save_state/kernel_state.hpp"

#include <bit>

namespace sarusaru::save_state {
namespace {
using Kernel = kernel::EarlyKernel;
using Resume = Kernel::ThreadInfo::CallbackResume;

template<std::size_t N> void words(PayloadWriter &w, const std::array<std::uint32_t, N> &values) {
    for (const auto value : values) w.u32(value);
}
template<std::size_t N> void words(PayloadReader &r, std::array<std::uint32_t, N> &values) {
    for (auto &value : values) value = r.u32();
}
void signed_word(PayloadWriter &w, std::int32_t value) { w.u32(std::bit_cast<std::uint32_t>(value)); }
std::int32_t signed_word(PayloadReader &r) { return std::bit_cast<std::int32_t>(r.u32()); }
void count(PayloadWriter &w, std::size_t value, std::uint32_t maximum) {
    if (value > maximum) throw FormatError("Kernel state: collection exceeds limit");
    w.u32(static_cast<std::uint32_t>(value));
}
std::uint32_t count(PayloadReader &r, std::uint32_t maximum) {
    const auto value = r.u32();
    if (value > maximum || value > r.remaining()) throw FormatError("Kernel state: invalid collection size");
    return value;
}
void text_field(PayloadWriter &w, const std::string &value) {
    if (value.size() > 1024) throw FormatError("Kernel state: oversized name or label");
    w.string(value);
}
std::string text_field(PayloadReader &r) {
    auto value = r.string();
    if (value.size() > 1024) throw FormatError("Kernel state: oversized name or label");
    return value;
}

void cpu(PayloadWriter &w, const psprecomp::AllegrexContext &c) {
    words(w, c.gpr);
    w.u32(c.hi); w.u32(c.lo); w.u32(c.pc);
    for (const auto value : c.fpr) w.u32(std::bit_cast<std::uint32_t>(value));
    w.u32(c.fcr31);
    for (const auto value : c.vfpu) w.u32(std::bit_cast<std::uint32_t>(value));
    words(w, c.vfpu_ctrl);
    w.u32(c.next_pc); w.boolean(c.in_delay_slot); w.boolean(c.load_linked);
}
psprecomp::AllegrexContext cpu(PayloadReader &r) {
    psprecomp::AllegrexContext c;
    words(r, c.gpr);
    c.hi = r.u32(); c.lo = r.u32(); c.pc = r.u32();
    for (auto &value : c.fpr) value = std::bit_cast<float>(r.u32());
    c.fcr31 = r.u32();
    for (auto &value : c.vfpu) value = std::bit_cast<float>(r.u32());
    words(r, c.vfpu_ctrl);
    c.next_pc = r.u32(); c.in_delay_slot = r.boolean(); c.load_linked = r.boolean();
    return c;
}
void action(PayloadWriter &w, const Kernel::Action &a) {
    if (a.kind > Kernel::ActionKind::EventFlagTimeout) throw FormatError("Kernel state: unsupported action");
    w.u32(static_cast<std::uint32_t>(a.kind));
    words(w, a.arguments);
}
Kernel::Action action(PayloadReader &r) {
    Kernel::Action a;
    const auto kind = r.u32();
    if (kind > static_cast<std::uint32_t>(Kernel::ActionKind::EventFlagTimeout)) throw FormatError("Kernel state: unsupported action");
    a.kind = static_cast<Kernel::ActionKind>(kind);
    words(r, a.arguments);
    return a;
}
void identity(PayloadWriter &w, const Kernel::WaitIdentity &v) {
    w.u32(static_cast<std::uint32_t>(v.type)); w.u32(v.object); w.u32(v.value);
}
Kernel::WaitIdentity identity(PayloadReader &r) {
    return {static_cast<Kernel::WaitType>(r.u32()), r.u32(), r.u32()};
}
void wait(PayloadWriter &w, const Kernel::WaitRequest &v) {
    text_field(w, v.label); w.u64(v.deadline_us); w.u32(v.timeout_address);
    action(w, v.on_timeout); signed_word(w, v.end_of); w.boolean(v.callbacks);
    identity(w, v.identity);
}
Kernel::WaitRequest wait(PayloadReader &r) {
    Kernel::WaitRequest v;
    v.label = text_field(r); v.deadline_us = r.u64(); v.timeout_address = r.u32();
    v.on_timeout = action(r); v.end_of = signed_word(r); v.callbacks = r.boolean();
    v.identity = identity(r);
    return v;
}

void thread(PayloadWriter &w, const Kernel::ThreadInfo &v) {
    text_field(w, v.name);
    w.u32(v.entry); w.u32(v.priority); w.u32(v.stack_bottom); w.u32(v.stack_size);
    w.u32(v.attributes); w.u32(v.gp); w.u32(v.control_block);
    cpu(w, v.context);
    w.u32(static_cast<std::uint32_t>(v.state)); w.u64(v.ready_sequence); w.u32(v.wakeup_count);
    w.u64(v.wake_at_us); w.u64(v.wait_sequence); action(w, v.on_wake);
    w.boolean(v.callback_saved.has_value());
    if (v.callback_saved) cpu(w, *v.callback_saved);
    count(w, v.callback_queue.size(), 4096);
    for (const auto uid : v.callback_queue) signed_word(w, uid);
    w.u32(static_cast<std::uint32_t>(v.after_callbacks));
    w.boolean(v.resume_wait.has_value());
    if (v.resume_wait) wait(w, *v.resume_wait);
    w.boolean(v.resume_sleep); w.u32(v.exit_status); signed_word(w, v.stack_block);
    w.u32(v.timeout_address); signed_word(w, v.end_of); text_field(w, v.wait_label);
    identity(w, v.wait_identity);
}
Kernel::ThreadInfo thread(PayloadReader &r, kernel::ThreadUid uid) {
    Kernel::ThreadInfo v;
    v.uid = uid; v.name = text_field(r);
    v.entry = r.u32(); v.priority = r.u32(); v.stack_bottom = r.u32(); v.stack_size = r.u32();
    v.attributes = r.u32(); v.gp = r.u32(); v.control_block = r.u32();
    v.context = cpu(r);
    const auto state = r.u32();
    if (state > static_cast<std::uint32_t>(Kernel::ThreadState::Waiting)) throw FormatError("Kernel state: invalid thread status");
    v.state = static_cast<Kernel::ThreadState>(state);
    v.ready_sequence = r.u64(); v.wakeup_count = r.u32(); v.wake_at_us = r.u64(); v.wait_sequence = r.u64();
    v.on_wake = action(r);
    if (r.boolean()) v.callback_saved = cpu(r);
    for (auto n = count(r, 4096); n != 0; --n) v.callback_queue.push_back(signed_word(r));
    const auto resume = r.u32();
    if (resume > static_cast<std::uint32_t>(Resume::Sleep)) throw FormatError("Kernel state: invalid callback continuation");
    v.after_callbacks = static_cast<Resume>(resume);
    if (r.boolean()) v.resume_wait = wait(r);
    v.resume_sleep = r.boolean(); v.exit_status = r.u32(); v.stack_block = signed_word(r);
    v.timeout_address = r.u32(); v.end_of = signed_word(r); v.wait_label = text_field(r);
    v.wait_identity = identity(r);
    return v;
}
template<class Map, class Value> void insert(Map &map, std::int32_t uid, Value value) {
    if (!map.emplace(uid, std::move(value)).second) throw FormatError("Kernel state: duplicate UID");
}
} // namespace

std::vector<std::uint8_t> encode_kernel_state(const Kernel::State &s) {
    PayloadWriter w;
    w.section("RecompKernel", 2);
    w.u32(s.next_stack_top); signed_word(w, s.next_uid); signed_word(w, s.current_uid); signed_word(w, s.interrupt_uid);
    w.u64(s.next_ready_sequence); w.u64(s.now_us);
    count(w, s.interrupt_calls.size(), 65536);
    for (const auto &call : s.interrupt_calls) {
        w.u32(call.address); words(w, call.arguments); w.u32(call.gp); text_field(w, call.label); action(w, call.on_return);
    }
    count(w, s.threads.size(), 4096);
    for (const auto &[uid, value] : s.threads) { signed_word(w, uid); thread(w, value); }
    count(w, s.free_ranges.size(), 65536);
    for (const auto &range : s.free_ranges) { w.u32(range.address); w.u32(range.size); }
    count(w, s.blocks.size(), 65536);
    for (const auto &[uid, block] : s.blocks) {
        signed_word(w, uid); text_field(w, block.name); w.u32(block.address); w.u32(block.size);
    }
    count(w, s.callbacks.size(), 4096);
    for (const auto &[uid, value] : s.callbacks) {
        signed_word(w, uid); text_field(w, value.name); w.u32(value.function); w.u32(value.common); signed_word(w, value.owner);
        w.boolean(value.registered_as_exit); w.u32(value.notify_count); w.u32(value.notify_argument);
    }
    signed_word(w, s.exit_callback_uid); signed_word(w, s.umd_callback_uid);
    for (const auto uid : s.power_callback_slots) signed_word(w, uid);
    w.boolean(s.interrupts_enabled); w.boolean(s.dispatch_enabled); w.boolean(s.vblank_active);
    w.end_section();
    return w.data();
}

Kernel::State decode_kernel_state(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize) throw FormatError("Kernel state: size exceeds limit");
    PayloadReader r(bytes);
    (void)r.section("RecompKernel", 2, 2);
    Kernel::State s;
    s.next_stack_top = r.u32(); s.next_uid = signed_word(r); s.current_uid = signed_word(r); s.interrupt_uid = signed_word(r);
    s.next_ready_sequence = r.u64(); s.now_us = r.u64();
    for (auto n = count(r, 65536); n != 0; --n) {
        Kernel::GuestCall call;
        call.address = r.u32(); words(r, call.arguments); call.gp = r.u32(); call.label = text_field(r); call.on_return = action(r);
        s.interrupt_calls.push_back(std::move(call));
    }
    for (auto n = count(r, 4096); n != 0; --n) {
        const auto uid = signed_word(r);
        insert(s.threads, uid, thread(r, uid));
    }
    for (auto n = count(r, 65536); n != 0; --n) s.free_ranges.push_back({r.u32(), r.u32()});
    for (auto n = count(r, 65536); n != 0; --n) {
        const auto uid = signed_word(r);
        Kernel::MemoryBlock block;
        block.name = text_field(r); block.address = r.u32(); block.size = r.u32();
        insert(s.blocks, uid, std::move(block));
    }
    for (auto n = count(r, 4096); n != 0; --n) {
        const auto uid = signed_word(r);
        Kernel::CallbackInfo value;
        value.name = text_field(r); value.function = r.u32(); value.common = r.u32(); value.owner = signed_word(r);
        value.registered_as_exit = r.boolean(); value.notify_count = r.u32(); value.notify_argument = r.u32();
        insert(s.callbacks, uid, std::move(value));
    }
    s.exit_callback_uid = signed_word(r); s.umd_callback_uid = signed_word(r);
    for (auto &uid : s.power_callback_slots) uid = signed_word(r);
    s.interrupts_enabled = r.boolean(); s.dispatch_enabled = r.boolean(); s.vblank_active = r.boolean();
    r.end_section(); r.finish();
    return s;
}
} // namespace sarusaru::save_state
