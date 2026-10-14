#include "kernel/interrupts.hpp"
#include "kernel/sync.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sarusaru::kernel;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void call(psprecomp::Runtime &runtime, std::uint32_t nid, std::uint32_t return_address,
          std::initializer_list<std::uint32_t> arguments) {
    auto &cpu = runtime.cpu();
    std::uint32_t reg = 4u;
    for (const auto argument : arguments) cpu.set_gpr(reg++, argument);
    cpu.pc = 0x08A00000u;
    cpu.set_gpr(31u, return_address);
    runtime.invoke_import("ThreadManForUser", nid, cpu);
}

constexpr std::uint32_t kWaitEventFlag = 0x402FCF22u, kSetEventFlag = 0x1FB15A32u, kWaitSema = 0x4E3A1105u;
constexpr std::uint32_t kHandler0 = 0x08806000u, kHandler1 = 0x08806100u;
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
    SyncObjects sync(kernel);
    InterruptManager interrupts(kernel);
    register_early_threadman(kernel, runtime);
    register_sync(sync, runtime);
    register_interrupts(interrupts, runtime);

    require(static_cast<std::uint32_t>(interrupts.register_handler(kInterruptVblank, 16u, kHandler0, 0u, 0u)) ==
                kIllegalInterruptCode &&
                static_cast<std::uint32_t>(interrupts.enable(kInterruptVblank, 0u)) == kHandlerNotFound,
            "registration validation");
    cpu.set_gpr(4u, kInterruptVblank); cpu.set_gpr(5u, 1u); cpu.set_gpr(6u, kHandler1); cpu.set_gpr(7u, 0x66u);
    runtime.invoke_import("InterruptManager", 0xCA04A2B9u, cpu);
    require(cpu.gpr[2] == 0u, "HLE registration failed");
    require(interrupts.register_handler(kInterruptVblank, 0u, kHandler0, 0x55u, 0x08900000u) == 0 &&
                static_cast<std::uint32_t>(interrupts.register_handler(kInterruptVblank, 0u, kHandler0, 0u, 0u)) ==
                    kHandlerAlreadyRegistered,
            "duplicate registration accepted");
    require(interrupts.enable(kInterruptVblank, 1u) == 0 && interrupts.enable(kInterruptVblank, 0u) == 0, "enable");

    // Blocking with nothing runnable lets virtual time reach the next vblank;
    // its handlers run in interrupt context, lowest sub-interrupt first.
    const auto flag = sync.create_event_flag("vblank", 0u, 0u);
    call(runtime, kWaitEventFlag, 0x08804100u, {static_cast<std::uint32_t>(flag), 1u, 0u, 0u, 0u});
    require(kernel.in_interrupt() && kernel.now_us() == 16683u && cpu.pc == kHandler0 && cpu.gpr[4] == 0u &&
                cpu.gpr[5] == 0x55u && cpu.gpr[28] == 0x08900000u && cpu.gpr[31] == kGuestCallReturnStub,
            "vblank handler not entered");
    const auto *interrupt = kernel.thread(kernel.current_uid());
    require(cpu.gpr[29] > interrupt->stack_bottom && cpu.gpr[29] < interrupt->stack_bottom + interrupt->stack_size,
            "handler not on interrupt stack");

    // Handlers cannot block, but can release threads without being preempted.
    const auto semaphore = sync.create_semaphore("never", 0u, 0, 1);
    call(runtime, kWaitSema, 0x08806010u, {static_cast<std::uint32_t>(semaphore), 1u, 0u});
    require(kernel.in_interrupt() && cpu.gpr[2] == kCanNotWait, "wait inside interrupt accepted");
    call(runtime, kSetEventFlag, 0x08806020u, {static_cast<std::uint32_t>(flag), 1u});
    require(kernel.in_interrupt() && cpu.gpr[2] == 0u, "interrupt was preempted");

    cpu.set_gpr(2u, 0u);
    kernel.guest_call_returned(cpu);
    require(kernel.in_interrupt() && cpu.pc == kHandler1 && cpu.gpr[4] == 1u && cpu.gpr[5] == 0x66u,
            "second handler not run in order");
    kernel.guest_call_returned(cpu);
    require(!kernel.in_interrupt() && kernel.current_uid() == main_uid && cpu.pc == 0x08804100u && cpu.gpr[2] == 0u,
            "released thread did not resume after interrupt");

    // A stray return to the stub is reported rather than ignored.
    psprecomp::Runtime stray_runtime;
    EarlyKernel stray;
    stray.install(stray_runtime, 0x08804000u, 0x0A000000u);
    stray.guest_call_returned(stray_runtime.cpu());
    require(stray_runtime.stopped(), "stray guest-call return accepted");
    kernel.install(runtime, 0x08804000u, 0x0A000000u);  // restore the active kernel for the stub

    // Without enabled vblank handlers, a wait with no deadline is a deadlock.
    require(interrupts.release_handler(kInterruptVblank, 0u) == 0 && interrupts.release_handler(kInterruptVblank, 1u) == 0 &&
                static_cast<std::uint32_t>(interrupts.release_handler(kInterruptVblank, 1u)) == kHandlerNotFound,
            "release failed");
    return 0;
}
