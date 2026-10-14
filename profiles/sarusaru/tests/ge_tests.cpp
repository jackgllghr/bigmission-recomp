#include "ge/ge.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {
using namespace sarusaru::ge;
using sarusaru::kernel::EarlyKernel;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void write_list(psprecomp::Runtime &runtime, std::uint32_t address, std::initializer_list<std::uint32_t> words) {
    for (const auto word : words) { runtime.memory().store32(address, word); address += 4u; }
}

void call(psprecomp::Runtime &runtime, std::uint32_t nid, std::uint32_t return_address,
          std::initializer_list<std::uint32_t> arguments) {
    auto &cpu = runtime.cpu();
    std::uint32_t reg = 4u;
    for (const auto argument : arguments) cpu.set_gpr(reg++, argument);
    cpu.pc = 0x08A00000u;
    cpu.set_gpr(31u, return_address);
    runtime.invoke_import("sceGe_user", nid, cpu);
}

constexpr std::uint32_t kEnqueue = 0xAB49E76Au, kListSync = 0x03444EB4u, kDrawSync = 0xB287BD61u,
                        kUpdateStall = 0xE0D68148u, kSetCallback = 0xA4FC06A4u;
constexpr std::uint32_t kSignalHandler = 0x08806000u, kFinishHandler = 0x08806100u;
}

int main() {
    psprecomp::Runtime runtime;
    EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    cpu.pc = 0x08804000u;
    cpu.set_gpr(28u, 0x08900000u);
    kernel.begin_main_thread(cpu, "main", 0x20u);
    const auto main_uid = kernel.current_uid();
    Ge ge(kernel);
    register_ge(ge, kernel, runtime);
    sarusaru::kernel::register_early_threadman(kernel, runtime);

    call(runtime, 0xE47E40E4u, 0x08804010u, {});
    require(cpu.gpr[2] == kEdramAddress, "EDRAM address");
    write_list(runtime, 0x08807000u, {kSignalHandler, 0x11u, kFinishHandler, 0x22u});
    call(runtime, kSetCallback, 0x08804020u, {0x08807000u});
    require(cpu.gpr[2] == 0u, "callback registration");

    // BASE + CALL/RET, then FINISH: the finish handler runs as a GE interrupt
    // before the caller resumes with the queue ID.
    write_list(runtime, 0x08810000u, {0x10080000u, 0x0A810100u, 0x04000003u, 0x0F000007u, 0x0C000000u});
    write_list(runtime, 0x08810100u, {0x04000001u, 0x0B000000u});
    call(runtime, kEnqueue, 0x08804100u, {0x08810000u, 0u, 0u, 0u});
    require(kernel.in_interrupt() && cpu.pc == kFinishHandler && cpu.gpr[4] == 7u && cpu.gpr[5] == 0x22u &&
                cpu.gpr[28] == 0x08900000u && ge.stats().primitives == 2u && ge.stats().lists_completed == 1u,
            "finish interrupt not raised after call/return");
    kernel.guest_call_returned(cpu);
    const std::uint32_t first = cpu.gpr[2];
    require(kernel.current_uid() == main_uid && cpu.pc == 0x08804100u && first == kListIdMagic,
            "enqueue did not return the queue ID");
    call(runtime, kListSync, 0x08804110u, {first, 1u});
    require(cpu.gpr[2] == static_cast<std::uint32_t>(ListState::Completed), "completed list state");

    // A stalled list waits for another thread to move the stall address.
    write_list(runtime, 0x08811000u, {0x04000000u, 0x04000000u, 0x0F000000u, 0x0C000000u});
    call(runtime, kEnqueue, 0x08804200u, {0x08811000u, 0x08811004u, 0xFFFFFFFFu, 0u});
    const std::uint32_t stalled = cpu.gpr[2];
    require(ge.list_state(stalled) == ListState::Stalling && ge.stats().primitives == 3u, "list did not stall");
    call(runtime, kDrawSync, 0x08804210u, {1u});
    require(cpu.gpr[2] == static_cast<std::uint32_t>(ListState::Stalling), "draw-sync peek");
    const auto worker = kernel.create_thread("worker", 0x08805000u, 0x30u, 0x400u, 0u, 0u);
    require(kernel.start_thread(cpu, worker, 0u, 0u) == 0, "worker start");
    call(runtime, kListSync, 0x08804220u, {stalled, 0u});
    require(kernel.current_uid() == worker, "list sync did not block");
    call(runtime, kUpdateStall, 0x08805100u, {stalled, 0x08811010u});
    require(kernel.current_uid() == main_uid && cpu.pc == 0x08804220u && cpu.gpr[2] == 0u &&
                ge.stats().primitives == 4u && ge.queue_state() == ListState::Completed,
            "stall update did not complete the list and release the waiter");

    // OFFSET_ADDR-relative jump and a suspending SIGNAL: the list pauses until
    // the signal handler returns, then runs to its FINISH.
    write_list(runtime, 0x08812000u, {0x10000000u, 0x13088120u, 0x08000010u});
    write_list(runtime, 0x08812010u, {0x0E010005u, 0x0C000000u, 0x04000000u, 0x0F000009u, 0x0C000000u});
    call(runtime, kEnqueue, 0x08804300u, {0x08812000u, 0u, 0u, 0u});
    require(kernel.in_interrupt() && cpu.pc == kSignalHandler && cpu.gpr[4] == 5u && cpu.gpr[5] == 0x11u &&
                ge.queue_state() == ListState::Paused && ge.stats().primitives == 4u,
            "suspending signal did not pause the list");
    kernel.guest_call_returned(cpu);
    require(kernel.in_interrupt() && cpu.pc == kFinishHandler && cpu.gpr[4] == 9u && ge.stats().primitives == 5u,
            "list did not resume after signal handler");
    kernel.guest_call_returned(cpu);
    require(kernel.current_uid() == main_uid && cpu.pc == 0x08804300u, "caller did not resume");

    // Block transfers (TRXKICK) copy pixel rectangles in guest memory: 2x2
    // pixels of 4 bytes from (1,1) of a 4-wide source to (0,0) of a 2-wide destination.
    for (std::uint32_t i = 0; i < 16u; ++i) runtime.memory().store32(0x08814000u + i * 4u, 0x100u + i);
    write_list(runtime, 0x08815000u, {0xB2814000u, 0xB3080004u, 0xB4816000u, 0xB5080002u, 0xEB000401u,
                                      0xEC000000u, 0xEE000401u, 0xEA000001u, 0x0F000000u, 0x0C000000u});
    call(runtime, kEnqueue, 0x08804350u, {0x08815000u, 0u, 0xFFFFFFFFu, 0u});
    require(runtime.memory().load32(0x08816000u) == 0x105u && runtime.memory().load32(0x08816004u) == 0x106u &&
                runtime.memory().load32(0x08816008u) == 0x109u && runtime.memory().load32(0x0881600Cu) == 0x10Au,
            "block transfer copied the rectangle");

    call(runtime, kEnqueue, 0x08804400u, {0x08812002u, 0u, 0u, 0u});
    require(cpu.gpr[2] == kInvalidPointer, "unaligned list accepted");
    write_list(runtime, 0x08813000u, {0x04000000u, 0x0C000000u});
    call(runtime, kEnqueue, 0x08804500u, {0x08813000u, 0u, 0u, 0u});
    require(runtime.stopped() && runtime.stop_reason().find("END without") != std::string::npos,
            "bare END not reported");
    return 0;
}
