#include "save_state/machine.hpp"

#include <algorithm>
#include <bit>
#include <iostream>

using namespace sarusaru::save_state;
namespace {
int failures{};
void check(bool value, const char *message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> void rejects(F f, const char *message) {
    try { f(); check(false, message); } catch (const FormatError &) {}
}

// Independent fixture layout: in particular LLBit is a four-byte integer,
// InDelaySlot is one byte, and each memory region has its own closing cookie.
std::vector<std::uint8_t> fixture() {
    PayloadWriter w;
    w.section("SaveStart", 3);
    w.u32(9);
    w.string("v1.20.4");
    w.u32(2);
    w.section("Memory", 3);
    w.u32(0);
    w.end_section();
    w.u32(32u * 1024u * 1024u);
    std::vector<std::uint8_t> ram(32u * 1024u * 1024u);
    const std::string decoy = "MIPSState";
    std::copy(decoy.begin(), decoy.end(), ram.begin() + 100);
    ram[0x804000] = 0x91;
    ram.back() = 0x29;
    w.bytes(ram);
    w.end_section();
    w.bytes(std::vector<std::uint8_t>(2u * 1024u * 1024u, 0x57));
    w.end_section();
    w.bytes(std::vector<std::uint8_t>(16u * 1024u, 0x16));
    w.end_section();
    w.end_section();
    w.section("CoreTiming", 3);
    w.u32(10);
    w.u8(1);
    w.u64(0x123456789ABCDEFu);
    w.u64(0xFEDCBA987654321u);
    w.u32(7);
    w.u8(0);
    w.u8(0);
    w.u32(222'000'000u);
    w.u32(1000);
    w.u64(1234567);
    w.u64(98765);
    w.u64(555555);
    w.u64(2502);
    w.end_section();
    w.section("MemoryStick", 5);
    w.u32(1);
    w.u32(1);
    w.u64(1024u * 1024u);
    w.u8(0);
    w.u64(222222);
    w.end_section();
    w.section("MIPSState", 4);
    w.section("Jit", 2);
    w.u8(0);
    w.u8(1);
    w.end_section();
    for (std::uint32_t i = 0; i < 32; ++i) w.u32(i * 13u);
    for (std::uint32_t i = 0; i < 32; ++i) w.u32(0x7FC10000u + i); // NaN payloads
    for (std::uint32_t i = 0; i < 128; ++i) w.u32(0x80000000u + i);
    for (std::uint32_t i = 0; i < 16; ++i) w.u32(i + 0x100u);
    w.u32(0x08801004u); // pc
    w.u32(0x08901000u); // pending branch target
    w.u32(1000); // downcount
    w.u32(0x11223344u); // HI, distinct from LO
    w.u32(0x55667788u);
    w.u32(1); // fpcond (separate from FCR31 in PPSSPP)
    w.u32(0);
    w.u8(1); // delay slot
    w.u32(1); // LLBit, NOT a bool byte
    w.u32(123); // debugCount
    w.end_section();
    w.section("HLE", 3);
    w.u32(7);
    w.end_section();
    w.end_section();
    return w.data();
}

void tests() {
    const auto wire = fixture();
    auto machine = decode_machine_prefix(wire);
    check(machine.ram.size() == 32u * 1024u * 1024u && machine.ram[0x804000] == 0x91 && machine.ram.back() == 0x29,
          "RAM region and boundaries are preserved");
    check(machine.vram.front() == 0x57 && machine.scratchpad.back() == 0x16,
          "VRAM and scratchpad use their own regions");
    check(machine.events == std::vector<TimingEvent>{{0x123456789ABCDEFu, 0xFEDCBA987654321u, 7}},
          "timing event has explicit unpadded fields");
    check(machine.cpu.hi == 0x11223344u && machine.cpu.lo == 0x55667788u && machine.cpu.load_linked == 1 &&
          machine.cpu.debug_count == 123 && machine.cpu.in_delay_slot, "CPU trailer field widths and order");
    check(encode_machine_prefix(machine) == wire, "machine schema round trips independent wire layout");
    auto cpu = machine.cpu.context();
    check(cpu.hi == machine.cpu.hi && cpu.lo == machine.cpu.lo && cpu.gpr[31] == 403 &&
          cpu.pc == 0x08801004u && cpu.next_pc == 0x08901000u && cpu.in_delay_slot && cpu.load_linked,
          "CPU adapter preserves guest execution continuation");
    check(cpu.fpu_condition(), "PPSSPP fpcond maps to runtime FCR31 condition");
    check(cpu.fpr_bits(7) == 0x7FC10007u && std::bit_cast<std::uint32_t>(cpu.vfpu[127]) == 0x8000007Fu,
          "FPU/VFPU bits preserve NaNs, signed zeros, and subnormals");
    machine.cpu.set_context(cpu);
    check(machine.cpu.fpr[7] == 0x7FC10007u && machine.cpu.vfpu[127] == 0x8000007Fu && machine.cpu.fp_condition == 1,
          "CPU adapter writes bits back without numeric conversions");
    const auto tail = machine.remainder;
    machine.cpu.pc += 4;
    check(decode_machine_prefix(encode_machine_prefix(machine)).remainder == tail, "prefix edits preserve opaque subsystem tail");
    machine.cpu.gpr[0] = 1;
    rejects([&] { (void)encode_machine_prefix(machine); }, "invalid zero register rejected");
    machine.cpu.gpr[0] = 0;
    machine.events[0].type = 10;
    rejects([&] { (void)encode_machine_prefix(machine); }, "event type must exist");
    machine.events[0].type = 7;
    machine.ram.resize(1024);
    rejects([&] { (void)encode_machine_prefix(machine); }, "unsupported RAM layout rejected");
    auto bad = wire;
    bad[16] = 4; // SaveStart version
    rejects([&] { (void)decode_machine_prefix(bad); }, "future SaveStart schema rejected");
    bad = wire;
    bad.back() = 1;
    rejects([&] { (void)decode_machine_prefix(bad); }, "outer closing cookie checked");
    for (const auto length : {0u, 20u, 40u, 1000u})
        rejects([&] { (void)decode_machine_prefix(std::span(wire).first(length)); }, "truncated machine records rejected");
}
} // namespace
int main() {
    tests();
    std::cout << (failures ? "FAIL" : "PASS") << ": PPSSPP machine records (" << failures << " failures)\n";
    return failures != 0;
}
