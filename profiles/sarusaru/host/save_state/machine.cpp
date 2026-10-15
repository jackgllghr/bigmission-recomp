#include "save_state/machine.hpp"

#include <bit>

namespace sarusaru::save_state {
namespace {
constexpr std::size_t kRam32 = 32u * 1024u * 1024u;
constexpr std::size_t kVram = 2u * 1024u * 1024u;
constexpr std::size_t kScratchpad = 16u * 1024u;

void exact_section(PayloadReader &r, std::string_view name, std::uint32_t version) {
    (void)r.section(name, version, version);
}

template<std::size_t N> void words(PayloadReader &r, std::array<std::uint32_t, N> &values) {
    for (auto &value : values) value = r.u32();
}
template<std::size_t N> void words(PayloadWriter &w, const std::array<std::uint32_t, N> &values) {
    for (const auto value : values) w.u32(value);
}
void region(PayloadReader &r, std::vector<std::uint8_t> &destination, std::size_t size) {
    const auto data = r.bytes(size);
    destination.assign(data.begin(), data.end());
    r.end_section();
}
void region(PayloadWriter &w, const std::vector<std::uint8_t> &bytes) {
    w.bytes(bytes);
    w.end_section();
}
void validate(const MachinePrefix &m) {
    if ((m.ram.size() != kRam32 && m.ram.size() != 2u * kRam32) || m.psp_model > 1u ||
        m.vram.size() != kVram || m.scratchpad.size() != kScratchpad)
        throw FormatError("PPST: unsupported PSP memory layout");
    if (m.event_type_count > 4096 || m.events.size() > 65536 || m.cpu_hz == 0 || m.cpu_hz > 1'000'000'000u)
        throw FormatError("PPST: invalid core timing data");
    for (const auto &event : m.events)
        if (event.type >= m.event_type_count) throw FormatError("PPST: unregistered timing event type");
    if ((m.memory_stick_state != 1 && m.memory_stick_state != 2) || m.memory_stick_fat_state > 1)
        throw FormatError("PPST: invalid memory stick state");
    if (m.cpu.gpr[0] != 0 || m.cpu.fp_condition > 1 || m.cpu.load_linked > 1 || (m.cpu.pc & 3u) != 0 ||
        (m.cpu.in_delay_slot && (m.cpu.next_pc & 3u) != 0))
        throw FormatError("PPST: invalid CPU state");
    PayloadReader tail(m.remainder);
    (void)tail.section("HLE", 1, 3);
    if (m.remainder.size() < 24) throw FormatError("PPST: missing subsystem records");
    PayloadReader ending(std::span(m.remainder).last(4));
    ending.end_section();
}
} // namespace

psprecomp::AllegrexContext PpstCpu::context() const {
    psprecomp::AllegrexContext c;
    c.gpr = gpr;
    c.hi = hi;
    c.lo = lo;
    c.pc = pc;
    c.next_pc = next_pc;
    c.in_delay_slot = in_delay_slot;
    c.load_linked = load_linked != 0;
    c.fcr31 = fcr31;
    c.set_fpu_condition(fp_condition != 0);
    c.vfpu_ctrl = vfpu_control;
    // PPSSPP >= MIPSState v3 and this runtime both store contiguous matrices.
    for (std::size_t i = 0; i < fpr.size(); ++i) c.fpr[i] = std::bit_cast<float>(fpr[i]);
    for (std::size_t i = 0; i < vfpu.size(); ++i) c.vfpu[i] = std::bit_cast<float>(vfpu[i]);
    return c;
}
void PpstCpu::set_context(const psprecomp::AllegrexContext &c) {
    gpr = c.gpr;
    hi = c.hi;
    lo = c.lo;
    pc = c.pc;
    next_pc = c.next_pc;
    in_delay_slot = c.in_delay_slot;
    load_linked = c.load_linked ? 1u : 0u;
    fcr31 = c.fcr31;
    fp_condition = c.fpu_condition() ? 1u : 0u;
    vfpu_control = c.vfpu_ctrl;
    for (std::size_t i = 0; i < fpr.size(); ++i) fpr[i] = std::bit_cast<std::uint32_t>(c.fpr[i]);
    for (std::size_t i = 0; i < vfpu.size(); ++i) vfpu[i] = std::bit_cast<std::uint32_t>(c.vfpu[i]);
}

MachinePrefix decode_machine_prefix(std::span<const std::uint8_t> payload) {
    if (payload.size() > kMaxPpstSize) throw FormatError("PPST: machine payload exceeds limit");
    PayloadReader r(payload);
    MachinePrefix m;
    exact_section(r, "SaveStart", 3);
    m.state_generation = r.u32();
    m.initial_creator = r.string();
    m.savedata_generation = r.u32();
    exact_section(r, "Memory", 3);
    m.psp_model = r.u32();
    r.end_section(); // PSPModel cookie
    const auto ram_size = r.u32();
    if (ram_size != kRam32 && ram_size != kRam32 * 2u) throw FormatError("PPST: unsupported RAM size");
    region(r, m.ram, ram_size);
    region(r, m.vram, kVram);
    region(r, m.scratchpad, kScratchpad);
    r.end_section(); // Memory cookie, separate from ScratchPad cookie
    exact_section(r, "CoreTiming", 3);
    m.event_type_count = r.u32();
    if (m.event_type_count > 4096) throw FormatError("PPST: too many timing event types");
    while (r.boolean()) {
        if (m.events.size() == 65536) throw FormatError("PPST: too many pending timing events");
        m.events.push_back({r.u64(), r.u64(), r.u32()});
    }
    if (r.boolean()) throw FormatError("PPST: obsolete thread-safe timing queue is nonempty");
    m.cpu_hz = r.u32();
    m.slice_length = r.u32();
    m.global_ticks = r.u64();
    m.idle_ticks = r.u64();
    m.last_time_ticks = r.u64();
    m.last_time_us = r.u64();
    r.end_section();
    exact_section(r, "MemoryStick", 5);
    m.memory_stick_state = r.u32();
    m.memory_stick_fat_state = r.u32();
    m.memory_stick_initial_free = r.u64();
    m.memory_stick_needs_assign = r.boolean();
    m.memory_stick_inserted_at = r.u64();
    r.end_section();
    exact_section(r, "MIPSState", 4);
    exact_section(r, "Jit", 2);
    m.cpu.jit_start_default = r.boolean();
    m.cpu.jit_has_set_rounding = r.boolean();
    r.end_section();
    words(r, m.cpu.gpr);
    words(r, m.cpu.fpr);
    words(r, m.cpu.vfpu);
    words(r, m.cpu.vfpu_control);
    m.cpu.pc = r.u32();
    m.cpu.next_pc = r.u32();
    m.cpu.downcount = r.u32();
    m.cpu.hi = r.u32();
    m.cpu.lo = r.u32();
    m.cpu.fp_condition = r.u32();
    m.cpu.fcr31 = r.u32();
    m.cpu.in_delay_slot = r.boolean();
    m.cpu.load_linked = r.u32();
    m.cpu.debug_count = r.u32();
    r.end_section();
    const auto tail = r.bytes(r.remaining());
    m.remainder.assign(tail.begin(), tail.end());
    validate(m);
    return m;
}

std::vector<std::uint8_t> encode_machine_prefix(const MachinePrefix &m) {
    validate(m);
    PayloadWriter w;
    w.section("SaveStart", 3);
    w.u32(m.state_generation);
    w.string(m.initial_creator);
    w.u32(m.savedata_generation);
    w.section("Memory", 3);
    w.u32(m.psp_model);
    w.end_section();
    w.u32(static_cast<std::uint32_t>(m.ram.size()));
    region(w, m.ram);
    region(w, m.vram);
    region(w, m.scratchpad);
    w.end_section();
    w.section("CoreTiming", 3);
    w.u32(m.event_type_count);
    for (const auto &event : m.events) {
        w.boolean(true);
        w.u64(event.ticks);
        w.u64(event.userdata);
        w.u32(event.type);
    }
    w.boolean(false);
    w.boolean(false);
    w.u32(m.cpu_hz);
    w.u32(m.slice_length);
    w.u64(m.global_ticks);
    w.u64(m.idle_ticks);
    w.u64(m.last_time_ticks);
    w.u64(m.last_time_us);
    w.end_section();
    w.section("MemoryStick", 5);
    w.u32(m.memory_stick_state);
    w.u32(m.memory_stick_fat_state);
    w.u64(m.memory_stick_initial_free);
    w.boolean(m.memory_stick_needs_assign);
    w.u64(m.memory_stick_inserted_at);
    w.end_section();
    w.section("MIPSState", 4);
    w.section("Jit", 2);
    w.boolean(m.cpu.jit_start_default);
    w.boolean(m.cpu.jit_has_set_rounding);
    w.end_section();
    words(w, m.cpu.gpr);
    words(w, m.cpu.fpr);
    words(w, m.cpu.vfpu);
    words(w, m.cpu.vfpu_control);
    w.u32(m.cpu.pc);
    w.u32(m.cpu.next_pc);
    w.u32(m.cpu.downcount);
    w.u32(m.cpu.hi);
    w.u32(m.cpu.lo);
    w.u32(m.cpu.fp_condition);
    w.u32(m.cpu.fcr31);
    w.boolean(m.cpu.in_delay_slot);
    w.u32(m.cpu.load_linked);
    w.u32(m.cpu.debug_count);
    w.end_section();
    w.bytes(m.remainder);
    return w.data();
}
} // namespace sarusaru::save_state
