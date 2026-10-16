#include "save_state/ppsspp_scheduler.hpp"

#include <algorithm>
#include <set>

namespace sarusaru::save_state {
namespace {
void section(PayloadReader &r, std::string_view name, std::uint32_t version = 1) {
    (void)r.section(name, version, version);
}
std::uint32_t count(PayloadReader &r, std::size_t width, std::uint32_t maximum = 4096) {
    const auto n = r.u32();
    if (n > maximum || n > r.remaining() / width) throw FormatError("PPST: invalid scheduler collection size");
    return n;
}
template<class Values> void words(PayloadReader &r, Values &values) {
    for (auto &value : values) value = r.u32();
}
template<class Values> void words(PayloadWriter &w, const Values &values) {
    for (const auto value : values) w.u32(value);
}
std::vector<std::uint32_t> vector(PayloadReader &r) {
    std::vector<std::uint32_t> values(count(r, 4)); words(r, values); return values;
}
void vector(PayloadWriter &w, const std::vector<std::uint32_t> &values) {
    w.u32(static_cast<std::uint32_t>(values.size())); words(w, values);
}
void debug_maps(PayloadReader &r) {
    section(r, "MemBlockInfo");
    for (unsigned i = 0; i < 4; ++i) {
        section(r, "MemSlabMap");
        const auto n = count(r, 45, 1'000'000);
        if (!n) throw FormatError("PPST: empty memory debug map");
        for (std::uint32_t j = 0; j < n; ++j) {
            const auto version = r.section("MemSlabMapSlab", 1, 3);
            (void)r.bytes(20); (void)r.boolean();
            if (version == 1) (void)r.string();
            else (void)r.bytes(version == 2 ? 32 : 128);
            r.end_section();
        }
        r.end_section();
    }
    r.end_section();
}
PspAllocator allocator(PayloadReader &r) {
    section(r, "BlockAllocator");
    PspAllocator a;
    const auto n = count(r, 65, 65536);
    if (!n) throw FormatError("PPST: empty block allocator");
    for (std::uint32_t i = 0; i < n; ++i) {
        section(r, "Block");
        PspAllocation b;
        b.address = r.u32(); b.size = r.u32(); b.allocated = r.boolean();
        const auto tag = r.bytes(b.tag.size()); std::copy(tag.begin(), tag.end(), b.tag.begin());
        r.end_section(); a.blocks.push_back(b);
    }
    a.address = r.u32(); a.size = r.u32(); a.grain = r.u32(); r.end_section();
    return a;
}
void allocator(PayloadWriter &w, const PspAllocator &a) {
    w.section("BlockAllocator", 1); w.u32(static_cast<std::uint32_t>(a.blocks.size()));
    for (const auto &b : a.blocks) {
        w.section("Block", 1); w.u32(b.address); w.u32(b.size); w.boolean(b.allocated); w.bytes(b.tag); w.end_section();
    }
    w.u32(a.address); w.u32(a.size); w.u32(a.grain); w.end_section();
}
void validate(const PspScheduler &s) {
    for (const auto &a : s.allocators) {
        if (a.blocks.empty() || a.blocks.size() > 65536 || a.grain == 0 || (a.grain & (a.grain - 1)) != 0 ||
            std::uint64_t(a.address) + a.size > 0x1'0000'0000ull)
            throw FormatError("PPST: invalid allocator range");
        std::uint64_t next = a.address;
        for (const auto &b : a.blocks) {
            if (b.address != next || !b.size) throw FormatError("PPST: allocator blocks do not partition range");
            next += b.size;
        }
        if (next != std::uint64_t(a.address) + a.size) throw FormatError("PPST: allocator size mismatch");
    }
    if (s.pending_interrupts.size() > 4096 || s.threads.size() > 4096 || s.tls_thread_checks.size() > 4096 ||
        s.paused_delays.size() > 4096 || s.thread_event_handlers.size() > 4096 || s.pending_delete_threads.size() > 4096 ||
        s.vtimers.size() > 4096 || s.loaded_modules.size() > 4096 || s.held_mutexes.size() > 4096)
        throw FormatError("PPST: too many scheduler records");
    for (const auto &[intr, sub] : s.pending_interrupts) {
        (void)sub; // PPSSPP also uses negative sentinels for non-sub interrupts.
        if (intr >= 67) throw FormatError("PPST: invalid pending interrupt");
    }
    std::set<std::uint32_t> ready;
    for (const auto &q : s.ready_queues) {
        if (q.capacity > 4096 || q.threads.size() > q.capacity) throw FormatError("PPST: invalid ready queue capacity");
        for (const auto id : q.threads)
            if (!ready.insert(id).second) throw FormatError("PPST: duplicate ready thread");
    }
    for (const auto &[id, handlers] : s.thread_event_handlers) {
        (void)id;
        if (handlers.size() > 4096) throw FormatError("PPST: too many thread event handlers");
    }
    PayloadReader debug(s.memory_debug_record); debug_maps(debug); debug.finish();
    if (!std::is_sorted(s.loaded_modules.begin(), s.loaded_modules.end()) ||
        std::adjacent_find(s.loaded_modules.begin(), s.loaded_modules.end()) != s.loaded_modules.end())
        throw FormatError("PPST: invalid loaded module set");
    PayloadReader tail(s.remainder); section(tail, "HLE Modules");
}
} // namespace

PspScheduler decode_psp_scheduler(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize) throw FormatError("PPST: scheduler input exceeds limit");
    PayloadReader r(bytes);
    PspScheduler s;
    section(r, "Kernel Modules");
    section(r, "sceKernelInterrupt");
    if (r.u32() != 67) throw FormatError("PPST: unsupported interrupt count");
    section(r, "InterruptState"); words(r, s.interrupt_context); r.end_section();
    for (auto n = count(r, 32); n != 0; --n) {
        section(r, "PendingInterrupt");
        const auto intr = r.u32(), sub = r.u32(); s.pending_interrupts.emplace_back(intr, sub);
        r.end_section();
    }
    s.interrupts_enabled = r.u32(); s.in_interrupt = r.boolean(); s.interrupted_thread = r.u32(); r.end_section();
    section(r, "sceKernelMemory", 3);
    for (auto &a : s.allocators) a = allocator(r);
    s.vpl_timer = r.u32(); s.fpl_timer = r.u32(); s.memory_flags = r.u32();
    s.sdk_version = r.u32(); s.compiler_version = r.u32();
    for (auto &used : s.tls_indexes) used = r.boolean();
    for (auto n = count(r, 8); n != 0; --n) {
        const auto thread = r.u32(), tls = r.u32(); s.tls_thread_checks.emplace_back(thread, tls);
    }
    const auto debug_begin = r.offset(); debug_maps(r);
    const auto debug = bytes.subspan(debug_begin, r.offset() - debug_begin);
    s.memory_debug_record.assign(debug.begin(), debug.end()); r.end_section();
    section(r, "sceKernelThread", 4);
    s.callback_depth = r.u32(); s.callback_thread = r.u32(); s.ready_callbacks = r.u32();
    words(r, s.return_stubs); s.current_thread = r.u32(); s.threads = vector(r);
    words(r, s.idle_threads); s.dispatch_enabled = r.boolean();
    section(r, "ThreadQueueList");
    if (r.u32() != s.ready_queues.size()) throw FormatError("PPST: unsupported ready queue count");
    for (auto &q : s.ready_queues) {
        const auto size = r.u32(); q.capacity = r.u32();
        if (q.capacity > 4096 || size > q.capacity || size > r.remaining() / 4)
            throw FormatError("PPST: invalid ready queue size");
        q.threads.resize(size); words(r, q.threads);
    }
    r.end_section();
    s.wakeup_event = r.u32(); s.thread_end_event = r.u32(); s.after_mips_action = r.u32(); s.after_callback_action = r.u32();
    for (auto n = count(r, 12); n != 0; --n) {
        const auto thread = r.u32(); const auto ticks = r.u64();
        if (!s.paused_delays.emplace(thread, ticks).second) throw FormatError("PPST: duplicate paused thread");
    }
    for (auto n = count(r, 8); n != 0; --n) {
        const auto thread = r.u32(); auto handlers = vector(r);
        if (!s.thread_event_handlers.emplace(thread, std::move(handlers)).second)
            throw FormatError("PPST: duplicate thread event owner");
    }
    s.pending_delete_threads = vector(r); r.end_section();
    section(r, "sceKernelAlarm"); s.alarm_timer = r.u32(); s.triggered_alarm = r.u32(); r.end_section();
    section(r, "sceKernelVTimer", 2); s.vtimer_timer = r.u32(); s.vtimers = vector(r); s.running_vtimer = r.u32(); r.end_section();
    section(r, "sceKernelEventFlag"); s.event_flag_timer = r.u32(); r.end_section();
    section(r, "sceKernelMbx"); s.mailbox_timer = r.u32(); r.end_section();
    section(r, "sceKernelModule", 2); s.after_module_action = r.u32(); s.loaded_modules = vector(r); r.end_section();
    section(r, "sceKernelMsgPipe"); s.message_pipe_timer = r.u32(); r.end_section();
    section(r, "sceKernelMutex"); s.mutex_timer = r.u32(); s.lightweight_mutex_timer = r.u32();
    for (auto n = count(r, 8); n != 0; --n) {
        const auto thread = r.u32(), mutex = r.u32(); s.held_mutexes.emplace_back(thread, mutex);
    }
    r.end_section();
    section(r, "sceKernelSema"); s.semaphore_timer = r.u32(); r.end_section();
    section(r, "sceKernelTime", 2); s.start_time = r.u64(); r.end_section();
    r.end_section(); // Kernel Modules
    const auto remaining = r.bytes(r.remaining()); s.remainder.assign(remaining.begin(), remaining.end());
    validate(s);
    return s;
}

std::vector<std::uint8_t> encode_psp_scheduler(const PspScheduler &s) {
    validate(s);
    PayloadWriter w;
    w.section("Kernel Modules", 1); w.section("sceKernelInterrupt", 1); w.u32(67);
    w.section("InterruptState", 1); words(w, s.interrupt_context); w.end_section();
    w.u32(static_cast<std::uint32_t>(s.pending_interrupts.size()));
    for (const auto &[intr, sub] : s.pending_interrupts) {
        w.section("PendingInterrupt", 1); w.u32(intr); w.u32(sub); w.end_section();
    }
    w.u32(s.interrupts_enabled); w.boolean(s.in_interrupt); w.u32(s.interrupted_thread); w.end_section();
    w.section("sceKernelMemory", 3);
    for (const auto &a : s.allocators) allocator(w, a);
    w.u32(s.vpl_timer); w.u32(s.fpl_timer); w.u32(s.memory_flags); w.u32(s.sdk_version); w.u32(s.compiler_version);
    for (const auto used : s.tls_indexes) w.boolean(used);
    w.u32(static_cast<std::uint32_t>(s.tls_thread_checks.size()));
    for (const auto &[thread, tls] : s.tls_thread_checks) { w.u32(thread); w.u32(tls); }
    w.bytes(s.memory_debug_record); w.end_section();
    w.section("sceKernelThread", 4);
    w.u32(s.callback_depth); w.u32(s.callback_thread); w.u32(s.ready_callbacks); words(w, s.return_stubs);
    w.u32(s.current_thread); vector(w, s.threads); words(w, s.idle_threads); w.boolean(s.dispatch_enabled);
    w.section("ThreadQueueList", 1); w.u32(static_cast<std::uint32_t>(s.ready_queues.size()));
    for (const auto &q : s.ready_queues) {
        w.u32(static_cast<std::uint32_t>(q.threads.size())); w.u32(q.capacity); words(w, q.threads);
    }
    w.end_section();
    w.u32(s.wakeup_event); w.u32(s.thread_end_event); w.u32(s.after_mips_action); w.u32(s.after_callback_action);
    w.u32(static_cast<std::uint32_t>(s.paused_delays.size()));
    for (const auto &[thread, ticks] : s.paused_delays) { w.u32(thread); w.u64(ticks); }
    w.u32(static_cast<std::uint32_t>(s.thread_event_handlers.size()));
    for (const auto &[thread, handlers] : s.thread_event_handlers) { w.u32(thread); vector(w, handlers); }
    vector(w, s.pending_delete_threads); w.end_section();
    w.section("sceKernelAlarm", 1); w.u32(s.alarm_timer); w.u32(s.triggered_alarm); w.end_section();
    w.section("sceKernelVTimer", 2); w.u32(s.vtimer_timer); vector(w, s.vtimers); w.u32(s.running_vtimer); w.end_section();
    w.section("sceKernelEventFlag", 1); w.u32(s.event_flag_timer); w.end_section();
    w.section("sceKernelMbx", 1); w.u32(s.mailbox_timer); w.end_section();
    w.section("sceKernelModule", 2); w.u32(s.after_module_action); vector(w, s.loaded_modules); w.end_section();
    w.section("sceKernelMsgPipe", 1); w.u32(s.message_pipe_timer); w.end_section();
    w.section("sceKernelMutex", 1); w.u32(s.mutex_timer); w.u32(s.lightweight_mutex_timer);
    w.u32(static_cast<std::uint32_t>(s.held_mutexes.size()));
    for (const auto &[thread, mutex] : s.held_mutexes) { w.u32(thread); w.u32(mutex); }
    w.end_section();
    w.section("sceKernelSema", 1); w.u32(s.semaphore_timer); w.end_section();
    w.section("sceKernelTime", 2); w.u64(s.start_time); w.end_section(); w.end_section();
    w.bytes(s.remainder);
    if (w.data().size() > kMaxPpstSize) throw FormatError("PPST: scheduler output exceeds limit");
    return w.data();
}
} // namespace sarusaru::save_state
