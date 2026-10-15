#include "psprecomp/interpreter.hpp"
#include "psprecomp/runtime.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>

using namespace psprecomp;
namespace {
int failures{};
constexpr std::uint32_t pc = 0x08800000u;
void check(bool value, const char *message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
constexpr std::uint32_t special(unsigned rs, unsigned rt, unsigned rd, unsigned shift, unsigned function) {
    return (rs << 21u) | (rt << 16u) | (rd << 11u) | (shift << 6u) | function;
}
constexpr std::uint32_t immediate(unsigned op, unsigned rs, unsigned rt, std::uint16_t value) {
    return (op << 26u) | (rs << 21u) | (rt << 16u) | value;
}
void step(Runtime &rt, std::uint32_t word) {
    rt.cpu().pc = pc;
    rt.memory().store32(pc, word);
    interpret_instruction(rt, rt.cpu());
}
void compiled(Runtime &rt, AllegrexContext &c) {
    c.set_gpr(2, 0x1234);
    rt.stop("compiled reached");
}

void branches() {
    auto runtime = std::make_unique<Runtime>();
    auto &rt = *runtime;
    auto &c = rt.cpu();
    c.set_gpr(1, 7);
    step(rt, immediate(4, 1, 1, 3)); // beq -> pc + 16
    check(c.pc == pc + 4 && c.in_delay_slot && c.next_pc == pc + 16, "taken branch records delay continuation");
    rt.memory().store32(pc + 4, immediate(9, 1, 2, 5));
    interpret_instruction(rt, c);
    check(c.pc == pc + 16 && !c.in_delay_slot && c.gpr[2] == 12, "delay slot runs before target");
    step(rt, immediate(0x14, 1, 2, 3)); // beql, not taken
    check(c.pc == pc + 8 && !c.in_delay_slot, "not-taken likely branch annuls slot");
    c.set_gpr(3, pc + 32);
    step(rt, special(3, 0, 3, 0, 9)); // jalr $3,$3
    check(c.next_pc == pc + 32 && c.gpr[3] == pc + 8, "JALR reads old target before writing link");
    rt.memory().store32(pc + 4, 0);
    interpret_instruction(rt, c);
    c.set_gpr(31, 55);
    c.set_gpr(3, pc + 64);
    step(rt, special(3, 0, 0, 0, 9));
    check(c.gpr[31] == 55 && c.gpr[0] == 0, "JALR rd=zero does not invent RA write");
    rt.memory().store32(pc + 4, 0);
    interpret_instruction(rt, c);
    c.in_delay_slot = true;
    c.next_pc = pc + 128;
    step(rt, immediate(9, 0, 2, 42));
    check(c.pc == pc + 128 && c.gpr[2] == 42 && !c.in_delay_slot, "restored delay-slot continuation resumes");
}

void integers_and_memory() {
    auto runtime = std::make_unique<Runtime>();
    auto &rt = *runtime;
    auto &c = rt.cpu();
    c.set_gpr(1, 0xFFFFFFFFu);
    step(rt, immediate(9, 1, 2, 2));
    check(c.gpr[2] == 1, "ADDIU wraps");
    step(rt, immediate(9, 1, 0, 2));
    check(c.gpr[0] == 0, "interpreter enforces zero");
    c.set_gpr(1, 0x80000000u);
    c.set_gpr(2, 0xFFFFFFFFu);
    step(rt, special(1, 2, 0, 0, 0x1A));
    check(c.lo == 0x80000000u && c.hi == 0, "signed division overflow is defined");
    c.set_gpr(2, 0);
    step(rt, special(1, 2, 0, 0, 0x1A));
    check(c.lo == 1 && c.hi == 0x80000000u, "negative divide by zero");
    c.set_gpr(1, 0x08801000u);
    rt.memory().store32(c.gpr[1], 123);
    step(rt, immediate(0x38, 1, 2, 0));
    check(c.gpr[2] == 0 && rt.memory().load32(c.gpr[1]) == 123, "SC without imported LLBit does not store");
    step(rt, immediate(0x30, 1, 2, 0));
    check(c.gpr[2] == 123 && c.load_linked, "LL reads memory and sets serializable LLBit");
    c.set_gpr(2, 124);
    step(rt, immediate(0x38, 1, 2, 0));
    check(c.gpr[2] == 1 && c.load_linked && rt.memory().load32(c.gpr[1]) == 124,
        "SC stores old target value and retains PPSSPP LLBit");
    step(rt, immediate(0x38, 1, 0, 0));
    check(c.gpr[0] == 0 && rt.memory().load32(c.gpr[1]) == 0, "SC with zero target still stores zero");
    c.load_linked = false;
    step(rt, immediate(0x30, 0, 0, 0)); // PPSSPP skips memory access for a zero target.
    check(c.load_linked && c.gpr[0] == 0, "LL zero target preserves zero and sets LLBit");
    c.set_gpr(2, 0xAABBCCDDu);
    step(rt, immediate(0x2B, 1, 2, 0));
    check(rt.memory().load32(c.gpr[1]) == 0xAABBCCDDu, "SW writes memory");
    step(rt, immediate(0x20, 1, 3, 0));
    check(c.gpr[3] == 0xFFFFFFDDu, "LB sign extends");
    step(rt, immediate(0x24, 1, 3, 0));
    check(c.gpr[3] == 0xDDu, "LBU zero extends");
    c.set_gpr(2, 0);
    step(rt, immediate(0x22, 1, 2, 3));
    check(c.gpr[2] == 0xAABBCCDDu, "unaligned merge uses little-endian layout");
    c.set_gpr(1, 0x7FFFFFFFu);
    c.set_gpr(2, 1);
    step(rt, special(1, 2, 3, 0, 0x20));
    check(rt.stopped() && c.pc == pc, "overflow stops at faulting instruction");
}

void floating_point() {
    auto runtime = std::make_unique<Runtime>();
    auto &rt = *runtime;
    auto &c = rt.cpu();
    c.fpr[1] = 1.5f;
    c.fpr[2] = 2.5f;
    step(rt, 0x46000000u | (2u << 16u) | (1u << 11u) | (3u << 6u));
    check(c.fpr[3] == 4.0f, "scalar FPU arithmetic");
    c.set_fpr_bits(1, 0xFFC12345u);
    step(rt, 0x46000006u | (1u << 11u) | (3u << 6u));
    check(c.fpr_bits(3) == 0xFFC12345u, "MOV.S preserves NaN payload");
    c.set_fpr_bits(1, 0x80000000u);
    step(rt, 0x46000005u | (1u << 11u) | (3u << 6u));
    check(c.fpr_bits(3) == 0, "ABS.S preserves zero semantics");
    c.set_vfpu_scalar_bits(0, std::bit_cast<std::uint32_t>(2.0f));
    c.set_vfpu_scalar_bits(1, std::bit_cast<std::uint32_t>(3.0f));
    step(rt, 0x60010002u); // vadd.s S020, S000, S010
    check(c.vfpu_scalar_bits(2) == std::bit_cast<std::uint32_t>(5.0f), "VFPU scalar addition");
    step(rt, 0xDF803C00u); // vfim.s S000, 1.0
    check(c.vfpu_scalar_bits(0) == std::bit_cast<std::uint32_t>(1.0f), "VFPU half immediate");
}

void dispatch_and_syscalls() {
    auto runtime = std::make_unique<Runtime>();
    auto &rt = *runtime;
    rt.memory().store32(pc, immediate(9, 0, 2, 77));
    rt.run(pc, 1);
    check(rt.cpu().gpr[2] == 0 && rt.stop_reason().find("No recompiled") != std::string::npos,
          "interpreter is opt-in");
    rt.enable_interpreter();
    rt.run(pc, 1);
    check(rt.cpu().gpr[2] == 77 && rt.cpu().pc == pc + 4, "uncompiled PC uses enabled fallback");
    rt.register_function(pc + 4, compiled, "compiled");
    rt.run(pc, 3);
    check(rt.stop_reason() == "compiled reached" && rt.cpu().gpr[2] == 0x1234,
          "fallback hands execution to compiled code");
    rt.cpu().in_delay_slot = true;
    rt.cpu().next_pc = pc + 16;
    rt.memory().store32(pc + 4, immediate(9, 0, 3, 88));
    rt.run(pc + 4, 1);
    check(rt.cpu().gpr[3] == 88 && rt.cpu().pc == pc + 16, "delay slot overrides compiled entry");
    bool called = false;
    rt.set_syscall_handler([&](Runtime &, AllegrexContext &c, std::uint32_t code) {
        called = code == 0x12345;
        c.set_gpr(2, 99);
    });
    rt.memory().store32(pc, (0x12345u << 6u) | 12u);
    rt.run(pc, 1);
    check(called && rt.cpu().gpr[2] == 99 && rt.cpu().pc == pc + 4, "syscall decoded and normal continuation applied");
    rt.set_syscall_handler([&](Runtime &, AllegrexContext &c, std::uint32_t) {
        set_runtime_thread_identity(20, "resumed");
        c.pc = pc + 64;
    });
    rt.run(pc, 1);
    check(rt.cpu().pc == pc + 64, "syscall preemption does not overwrite resumed thread PC");
    rt.set_syscall_handler({});
    rt.run(pc, 1);
    check(rt.stop_reason().find("unbound interpreter syscall") != std::string::npos,
          "unbound syscalls fail explicitly");
    rt.memory().store32(pc, 0xFFFFFFFFu);
    rt.run(pc, 1);
    check(rt.cpu().pc == pc + 4, "VFPU vflush decodes as architectural no-op");
    rt.memory().store32(pc, 0x0000000Du); // break, not supported by the runtime
    rt.run(pc, 1);
    check(rt.cpu().pc == pc && rt.stop_reason().find("not implemented") != std::string::npos,
          "unknown instruction fails without advancing PC");
}
} // namespace

int main() {
    branches();
    integers_and_memory();
    floating_point();
    dispatch_and_syscalls();
    std::cout << (failures ? "FAIL" : "PASS") << ": interpreter fallback (" << failures << " failures)\n";
    return failures != 0;
}
