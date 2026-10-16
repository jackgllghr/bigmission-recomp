#include "save_state/ppsspp_objects.hpp"
#include "save_state/ppsspp_scheduler.hpp"
#include "save_state/ppsspp_thread.hpp"
#include "save_state/ppsspp_syscalls.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <memory>

namespace {
using namespace sarusaru::save_state;
int failures{};
void check(bool result, const char *message) {
    if (!result) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> void rejects(F f, const char *message) {
    try { f(); check(false, message); } catch (const FormatError &) {}
}
void zeros(PayloadWriter &w, std::size_t size) { w.bytes(std::vector<std::uint8_t>(size)); }
void put(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
}
PspKernelObjects objects_fixture() {
    PspKernelObjects objects;
    PayloadWriter hle; hle.section("HLE", 2); hle.u32(7); hle.u32(2);
    hle.u32(0xFFFFFFFF); hle.u32(0xFFFFFFFF); hle.end_section(); objects.hle_record = hle.data();
    objects.running = true; objects.next_pool_index = 32;
    const auto add = [&](std::uint32_t uid, PspObjectType type, std::string_view title, std::uint32_t version,
                         std::size_t native_size, std::size_t legacy_size) {
        PayloadWriter w; w.section(title, version);
        std::vector<std::uint8_t> native(native_size);
        put(native, 0, static_cast<std::uint32_t>(native_size));
        const std::string name = "synthetic";
        std::copy(name.begin(), name.end(), native.begin() + 4);
        w.bytes(native); zeros(w, legacy_size); w.end_section();
        objects.objects.emplace(uid, PspObjectRecord{type, {}, w.data()});
    };
    add(0x110, PspObjectType::Callback, "Callback", 1, 56, 20);
    add(0x112, PspObjectType::Semaphore, "Semaphore", 1, 56, 8);
    add(0x113, PspObjectType::EventFlag, "EventFlag", 1, 52, 8);
    // Empty collections use their own count words; the context is 214 words,
    // not a host AllegrexContext or the top-level MIPSState representation.
    add(0x114, PspObjectType::Thread, "Thread", 5, 104, 12 + 1 + 8 + 214 * 4 + 3 * 4 + 8 + 2 * 4);
    add(0x115, PspObjectType::PartitionMemory, "PMB", 2, 40, 0);
    PayloadWriter dir; dir.section("DirListing", 1); dir.string("ms0:/"); dir.u32(0); dir.u32(0); dir.end_section();
    objects.objects.emplace(0x11F, PspObjectRecord{PspObjectType::Directory, {}, dir.data()});
    PayloadWriter tail; tail.section("Kernel Modules", 1); tail.end_section(); tail.end_section();
    objects.remainder = tail.data();
    return objects;
}
void object_records() {
    const auto source = objects_fixture();
    const auto bytes = encode_psp_objects(source);
    const auto parsed = decode_psp_objects(bytes);
    check(parsed.objects.size() == 6 && parsed.objects.at(0x110).name == "synthetic" &&
        parsed.objects.at(0x114).name == "synthetic" && parsed.objects.at(0x11F).name == "ms0:/",
        "kernel bitmap implies UIDs and object schemas locate names");
    check(encode_psp_objects(parsed) == bytes, "kernel object records preserve complete wire data");
    auto broken = source;
    broken.objects.at(0x110).type = static_cast<PspObjectType>(999);
    rejects([&] { (void)encode_psp_objects(broken); }, "unknown object type rejected");
    broken = source; broken.objects.at(0x114).encoded[16] = 6;
    rejects([&] { (void)encode_psp_objects(broken); }, "future thread schema rejected");
    broken = source; broken.objects.at(0x113).encoded.back() = 1;
    rejects([&] { (void)encode_psp_objects(broken); }, "object cookie checked");
    broken = source; broken.objects.emplace(0x1100, source.objects.begin()->second);
    rejects([&] { (void)encode_psp_objects(broken); }, "object UID outside PPSSPP pool rejected");
    broken = source; put(broken.hle_record, 28, 0);
    rejects([&] { (void)encode_psp_objects(broken); }, "active unknown HLE action rejected");
    for (const auto size : {0u, 20u, 70u, 4200u})
        rejects([&] { (void)decode_psp_objects(std::span(bytes).first(size)); }, "truncated object records rejected");
    auto bad_wire = bytes;
    // HLE, Kernel header/bool, Pool header/count/index, bitmap.
    bad_wire.at(source.hle_record.size() + 21 + 28) = 2;
    rejects([&] { (void)decode_psp_objects(bad_wire); }, "noncanonical bitmap bool rejected");
}
PspScheduler scheduler_fixture() {
    PspScheduler s;
    for (std::size_t i = 0; i < s.interrupt_context.size(); ++i) s.interrupt_context[i] = 0x80000000u + i;
    s.pending_interrupts = {{25, 0xFFFFFFFF}, {30, 2}}; s.interrupts_enabled = 1; s.interrupted_thread = 0x114;
    for (std::size_t i = 0; i < s.allocators.size(); ++i) {
        auto &a = s.allocators[i]; a.address = 0x08000000 + i * 0x10000; a.size = 0x10000; a.grain = 256;
        a.blocks = {{a.address, 0x1000, true, {}}, {a.address + 0x1000, 0xF000, false, {}}};
        a.blocks[0].tag[0] = 'x';
    }
    s.vpl_timer = 4; s.fpl_timer = 5; s.sdk_version = 0x03050010; s.compiler_version = 0x12345678;
    s.tls_indexes[3] = true; s.tls_thread_checks = {{0x114, 0x118}, {0x114, 0x119}};
    PayloadWriter debug; debug.section("MemBlockInfo", 1);
    for (unsigned i = 0; i < 4; ++i) {
        debug.section("MemSlabMap", 1); debug.u32(1); debug.section("MemSlabMapSlab", 3);
        debug.u32(0); debug.u32(0x40000000); debug.u64(0x123456789abcdef0ull); debug.u32(0);
        debug.boolean(false); zeros(debug, 128); debug.end_section(); debug.end_section();
    }
    debug.end_section(); s.memory_debug_record = debug.data();
    s.return_stubs = {0x08000000, 0x08000004, 0x08000008, 0x0800000C, 0x08000010, 0x08000014, 0x08000018};
    s.current_thread = 0x114; s.threads = {0x110, 0x111, 0x114}; s.idle_threads = {0x110, 0x111}; s.dispatch_enabled = true;
    s.ready_queues[32] = {32, {0x115, 0x116}};
    s.wakeup_event = 6; s.thread_end_event = 7; s.after_mips_action = 1; s.after_callback_action = 2;
    s.paused_delays.emplace(0x114, 0x123456789ABCDEF0ull);
    s.thread_event_handlers.emplace(0x114, std::vector<std::uint32_t>{0x120, 0x121}); s.pending_delete_threads = {0x122};
    s.alarm_timer = 8; s.triggered_alarm = 0x123; s.vtimer_timer = 9; s.running_vtimer = 0x125;
    s.vtimers = {0x125, 0x126}; s.event_flag_timer = 10; s.mailbox_timer = 11; s.after_module_action = 3;
    s.loaded_modules = {0x112, 0x113}; s.message_pipe_timer = 12; s.mutex_timer = 13; s.lightweight_mutex_timer = 14;
    s.held_mutexes = {{0x114, 0x127}, {0x114, 0x128}}; s.semaphore_timer = 15; s.start_time = 0x123456789ABCDEF0ull;
    PayloadWriter tail; tail.section("HLE Modules", 1);
    tail.end_section(); tail.end_section(); s.remainder = tail.data();
    return s;
}
void scheduler_records() {
    const auto source = scheduler_fixture();
    const auto bytes = encode_psp_scheduler(source);
    check(std::equal(bytes.begin() + 20, bytes.begin() + 36, std::string_view("sceKernelInterru").begin()),
        "PPSSPP truncates section titles to all sixteen wire bytes");
    const auto parsed = decode_psp_scheduler(bytes);
    check(parsed.interrupt_context == source.interrupt_context && parsed.pending_interrupts == source.pending_interrupts &&
        parsed.return_stubs == source.return_stubs && parsed.current_thread == source.current_thread &&
        parsed.ready_queues[32].threads == source.ready_queues[32].threads && parsed.paused_delays == source.paused_delays &&
        parsed.thread_event_handlers == source.thread_event_handlers && parsed.tls_thread_checks == source.tls_thread_checks &&
        parsed.held_mutexes == source.held_mutexes && parsed.loaded_modules == source.loaded_modules &&
        parsed.vtimers == source.vtimers && parsed.start_time == source.start_time && parsed.semaphore_timer == 15,
        "scheduler fields retain wire order and bit patterns");
    check(encode_psp_scheduler(parsed) == bytes, "complete scheduler prefix round trips exactly");
    auto broken = source; broken.allocators[0].blocks[1].address += 256;
    rejects([&] { (void)encode_psp_scheduler(broken); }, "overlapping allocator range rejected");
    broken = source; broken.ready_queues[33] = {32, {0x115}};
    rejects([&] { (void)encode_psp_scheduler(broken); }, "duplicate ready thread rejected");
    broken = source; broken.ready_queues[32].capacity = 1;
    rejects([&] { (void)encode_psp_scheduler(broken); }, "queue size exceeding capacity rejected");
    broken = source; broken.memory_debug_record.pop_back();
    rejects([&] { (void)encode_psp_scheduler(broken); }, "truncated debug records rejected");
    auto wire = bytes; put(wire, 40, 68);
    rejects([&] { (void)decode_psp_scheduler(wire); }, "incompatible interrupt count rejected");
    for (const auto size : {0u, 20u, 500u, 1100u, 2000u})
        rejects([&] { (void)decode_psp_scheduler(std::span(bytes).first(size)); }, "truncated scheduler rejected");
    broken = source; broken.current_thread = 0x116; broken.ready_queues[32].threads = {0x114, 0x115};
    const auto edited = decode_psp_scheduler(encode_psp_scheduler(broken));
    check(edited.current_thread == 0x116 && edited.ready_queues[32].threads.front() == 0x114 &&
        edited.remainder == source.remainder, "scheduler edits rewrite fields without changing later subsystem records");
}
void thread_records() {
    PspThread t;
    t.name[0] = 't'; t.name[31] = 0xA5; // Preserve bytes beyond a name's terminator.
    t.attributes = 0x80004000; t.status = 4; t.entry = 0x08801000;
    t.initial_stack = 0x09FFE000; t.stack_size = 0x2000; t.gp = 0x08810000;
    t.initial_priority = 32; t.priority = 40; t.wait_type = 3; t.wait_id = 0x123;
    t.wakeup_count = 2; t.exit_status = 0x800201A8; t.run_clocks = 0x123456789ABCDEF0ull;
    t.interrupt_preempts = 5; t.thread_preempts = 6; t.releases = 7;
    t.wait_value = 8; t.timeout_address = 0x08812000; t.module_uid = 0x112;
    t.processing_callbacks = true; t.mips_call_id = 11; t.callback_id = 0x124;
    for (std::size_t i = 1; i < t.cpu.gpr.size(); ++i) t.cpu.gpr[i] = 0x1000 + i;
    t.cpu.pc = 0x08802000; t.cpu.lo = 0x11111111; t.cpu.hi = 0x22222222;
    t.cpu.fcr31 = 0; t.cpu.fp_condition = 1; t.cpu.reserved = 0xDEADBEEF;
    t.cpu.fpr[7] = 0xFFC12345; t.cpu.vfpu[127] = 0x80000000;
    t.callbacks = {0x124, 0x125}; t.pending_calls = {12, 13}; t.pushed_stacks = {{0x09FF0000, 0x1000}};
    t.current_stack = {0x09FFD000, 0x2000}; t.end_waiters = {0x126}; t.paused_waits = {{0x124, 0xFEDCBA9876543210ull}};
    const auto bytes = encode_psp_thread(t);
    // Assert independent documented offsets, rather than only testing mutually
    // inverse serializers: native struct + bool + context order are ABI traps.
    check(bytes[20] == 104 && bytes[55] == 0xA5 && bytes[56] == 0 && bytes[59] == 0x80 &&
        bytes[84] == 40 && bytes[104] == 0xF0 && bytes[136] == 1,
        "thread native, clock, and bool fields occupy PPSSPP wire offsets");
    const auto parsed = decode_psp_thread(bytes);
    check(encode_psp_thread(parsed) == bytes && parsed.cpu.lo == 0x11111111 && parsed.cpu.hi == 0x22222222 &&
        parsed.cpu.reserved == 0xDEADBEEF && parsed.current_stack == t.current_stack && parsed.paused_waits == t.paused_waits,
        "typed thread records retain registers, padding word, stacks, and continuations");
    const auto cpu = parsed.cpu.context();
    check(cpu.pc == t.cpu.pc && cpu.lo == t.cpu.lo && cpu.hi == t.cpu.hi && cpu.fpu_condition() &&
        cpu.fpr_bits(7) == 0xFFC12345 && std::bit_cast<std::uint32_t>(cpu.vfpu[127]) == 0x80000000,
        "blocked CPU adapter preserves FPU condition and payload bits");
    PspThreadContext exported; exported.reserved = 42; exported.set_context(cpu);
    check(exported.pc == t.cpu.pc && exported.fp_condition == 1 && exported.reserved == 42 &&
        exported.fpr == t.cpu.fpr && exported.vfpu == t.cpu.vfpu, "blocked CPU exports preserve reserved metadata");
    auto delayed = cpu; delayed.in_delay_slot = true;
    rejects([&] { exported.set_context(delayed); }, "unrepresentable blocked delay slot rejected");
    auto invalid = t.cpu; invalid.pc |= 1;
    rejects([&] { (void)invalid.context(); }, "unaligned restored blocked CPU rejected");
    auto wire = bytes; wire[136] = 2;
    rejects([&] { (void)decode_psp_thread(wire); }, "invalid callback-processing bool rejected");
    wire = bytes; wire.push_back(0);
    rejects([&] { (void)decode_psp_thread(wire); }, "trailing typed thread data rejected");
    for (const auto size : {0u, 20u, 136u, 900u})
        rejects([&] { (void)decode_psp_thread(std::span(bytes).first(size)); }, "truncated typed thread rejected");
}
void synchronization_records() {
    PspSemaphore s;
    s.name[0] = 's'; s.attributes = 0x100; s.initial_count = 3; s.count = 2; s.maximum = 10; s.waiter_count = 2;
    s.waiters = {0x114, 0x115}; s.paused_waits = {{0x116, 0x123456789ABCDEF0ull}};
    const auto sema = encode_psp_semaphore(s);
    auto parsed_s = decode_psp_semaphore(sema);
    check(sema[20] == 56 && sema[60] == 3 && sema[64] == 2 && sema[68] == 10 && sema[76] == 2 &&
        parsed_s.waiters == s.waiters && parsed_s.paused_waits == s.paused_waits && encode_psp_semaphore(parsed_s) == sema,
        "typed semaphore fields and timeout map preserve PPSSPP layout");
    PspEventFlag f;
    f.name[0] = 'e'; f.attributes = 0x200; f.initial_pattern = 0x42; f.pattern = 0x55; f.waiter_count = 1;
    f.waiters = {{0x114, 0x10, 0x21, 0x08812000, 0x123456789ABCDEF0ull}};
    f.paused_waits = {{0x116, {0x115, 0x20, 0x10, 0x08812004, 0xFEDCBA9876543210ull}}};
    const auto event = encode_psp_event_flag(f);
    auto parsed_f = decode_psp_event_flag(event);
    check(event[20] == 52 && event[60] == 0x42 && event[64] == 0x55 && event[72] == 1 &&
        parsed_f.waiters == f.waiters && parsed_f.paused_waits == f.paused_waits && encode_psp_event_flag(parsed_f) == event,
        "typed event flags preserve waiter POD and explicit paused-map keys");
    PspCallback c;
    c.name[0] = 'c'; c.owner = 0x114; c.entry = 0x08807000; c.argument = 42;
    c.notify_count = 3; c.notify_argument = 0xFFFFFFFE; c.legacy_registers = {1, 2, 3, 4, 5};
    const auto callback = encode_psp_callback(c);
    const auto parsed_c = decode_psp_callback(callback);
    check(callback[56] == 0x14 && callback[64] == 42 && callback[68] == 3 && callback[72] == 0xFE &&
        parsed_c.owner == c.owner && parsed_c.entry == c.entry && parsed_c.legacy_registers == c.legacy_registers &&
        encode_psp_callback(parsed_c) == callback, "typed callbacks preserve ownership, notification, and legacy registers");
    auto wire = event; put(wire, 72, 0xFFFFFFFF);
    rejects([&] { (void)decode_psp_event_flag(wire); }, "oversized waiter count rejected before allocation");
    wire = event;
    // Append an identical map entry, adjust its count, and retain the cookie.
    constexpr auto map_count = 72 + 4 + 24;
    put(wire, map_count, 2);
    wire.insert(wire.end() - 4, event.begin() + map_count + 4, event.end() - 4);
    rejects([&] { (void)decode_psp_event_flag(wire); }, "duplicate paused event key rejected");
    wire = callback; wire.pop_back();
    rejects([&] { (void)decode_psp_callback(wire); }, "truncated callback rejected");
    parsed_s.waiters.resize(4097);
    rejects([&] { (void)encode_psp_semaphore(parsed_s); }, "too many exported semaphore waiters rejected");
    parsed_f.paused_waits.clear(); parsed_f.waiters.clear(); parsed_f.pattern = 0x80;
    check(decode_psp_event_flag(encode_psp_event_flag(parsed_f)).pattern == 0x80,
        "event object edits produce a structurally valid record");
}
PspObjectRecord import_module(std::string_view library, std::uint32_t address, std::uint32_t nid) {
    PayloadWriter w; w.section("Module", 6); zeros(w, 196 + 12); w.boolean(false); zeros(w, 20);
    w.u32(0); w.u32(0); // No waiters/exports.
    w.u32(1); std::array<std::uint8_t, 32> name{}; std::copy(library.begin(), library.end(), name.begin());
    w.bytes(name); w.u32(address); w.u32(nid); w.u32(0); w.u32(0); // No variable exports/imports.
    w.section("Plugins", 1); w.boolean(false); w.end_section(); w.end_section();
    return {PspObjectType::Module, {}, w.data()};
}
void compiled_finish(psprecomp::Runtime &rt, psprecomp::AllegrexContext &) { rt.stop("compiled continuation reached"); }
void syscall_bindings() {
    constexpr auto base = psprecomp::GuestMemory::kPhysicalBase, stub = base + 0x100, target = base + 0x400;
    constexpr auto syscall_code = 0x12345u, nid = 0xABCDEF12u;
    MachinePrefix machine; machine.ram.resize(0x1000);
    put(machine.ram, stub - base, 0x03E00008); put(machine.ram, stub + 4 - base, (syscall_code << 6) | 12);
    PspKernelObjects objects;
    objects.objects.emplace(0x112, import_module("test-library", stub, nid));
    const auto table = PspSyscallTable::decode(machine, objects);
    check(table.imports().size() == 1 && table.imports().at(syscall_code) == PspImportBinding{"test-library", nid},
        "syscall numbers resolved from module name, stub address, and NID wire fields");
    auto rt = std::make_unique<psprecomp::Runtime>();
    rt->memory().store32(stub, 0x03E00008); rt->memory().store32(stub + 4, (syscall_code << 6) | 12);
    rt->cpu().set_gpr(31, target); rt->enable_interpreter(); rt->register_function(target, compiled_finish, "finish");
    unsigned calls = 0;
    rt->register_hle("test-library", nid, [&](psprecomp::Runtime &, psprecomp::AllegrexContext &cpu) { ++calls; cpu.set_gpr(2, 42); });
    table.install(*rt); rt->run(stub, 4);
    check(calls == 1 && rt->cpu().gpr[2] == 42 && rt->stop_reason() == "compiled continuation reached",
        "interpreted PPSSPP import calls native HLE in JR delay slot and returns to compiled code");
    objects.objects.emplace(0x113, import_module("test-library", stub, nid));
    check(PspSyscallTable::decode(machine, objects).imports().size() == 1, "duplicate identical syscall binding accepted");
    objects.objects.at(0x113) = import_module("other-library", stub, nid);
    rejects([&] { (void)PspSyscallTable::decode(machine, objects); }, "conflicting symbols for a syscall code rejected");
    objects.objects.erase(0x113);
    auto invalid = machine; put(invalid.ram, stub - base, (2u << 26) | ((base + 0x800) >> 2));
    check(PspSyscallTable::decode(invalid, objects).imports().empty(), "guest-linked import remains guest code");
    invalid = machine; put(invalid.ram, stub + 4 - base, (0xFFFFFu << 6) | 12);
    check(PspSyscallTable::decode(invalid, objects).imports().empty(), "unresolved import sentinel remains unbound");
    objects.objects.at(0x112) = import_module("test-library", stub | 1, nid);
    rejects([&] { (void)PspSyscallTable::decode(machine, objects); }, "unaligned import stub rejected");
    rt->memory().store32(stub + 4, (0xFFFFFu << 6) | 12); rt->cpu().set_gpr(31, target); rt->run(stub, 4);
    check(calls == 1 && rt->stop_reason().find("Unmapped PPSSPP syscall") == 0,
        "unknown syscall stops before calling an arbitrary native import");
}
} // namespace
int main() {
    object_records(); scheduler_records(); thread_records(); synchronization_records();
    syscall_bindings();
    std::cout << (failures ? "FAIL" : "PASS") << ": PPSSPP kernel schemas (" << failures << " failures)\n";
    return failures != 0;
}
