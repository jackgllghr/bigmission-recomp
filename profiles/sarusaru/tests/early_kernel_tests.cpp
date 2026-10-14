#include "kernel/early_kernel.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    constexpr std::uint32_t name_address = 0x08806000u;
    psprecomp::Runtime runtime;
    sarusaru::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    const auto invalid_entry = kernel.create_thread("bad", 3u, 0x20u, 0x200u, 0u, 0u);
    require(static_cast<std::uint32_t>(invalid_entry) == sarusaru::kernel::kIllegalEntry, "invalid entry accepted");
    require(static_cast<std::uint32_t>(kernel.create_thread("bad", 0x08805000u, 0u, 0x200u, 0u, 0u)) ==
        sarusaru::kernel::kIllegalPriority, "invalid priority accepted");
    require(static_cast<std::uint32_t>(kernel.create_thread("bad", 0x08805000u, 0x20u, 0x1FFu, 0u, 0u)) ==
        sarusaru::kernel::kIllegalStackSize, "undersized stack accepted");

    const auto uid = kernel.create_thread("worker", 0x08805000u, 0x20u, 0x201u, 0x123u, 0x08801234u);
    const auto *thread = kernel.thread(uid);
    require(uid >= 0x100 && thread != nullptr, "valid thread missing");
    require(thread->stack_size == 0x300u && thread->stack_bottom == 0x09FFFD00u, "stack allocation incorrect");
    require(thread->name == "worker" && thread->gp == 0x08801234u, "thread metadata lost");
    require(thread->control_block == 0x09FFFF00u && runtime.memory().load32(thread->control_block + 0xC0u) ==
        static_cast<std::uint32_t>(uid), "thread control block missing");

    const auto low = kernel.allocate_partition_memory("low", 3u, 0x101u, 0x1000u);
    require(low >= 0x100 && kernel.block_head_address(low) == 0x08804000u, "low aligned allocation incorrect");
    const auto high = kernel.allocate_partition_memory("high", 1u, 0x100u, 0u);
    require(high >= 0x100 && kernel.block_head_address(high) < 0x09FFFD00u, "high allocation collided with stack");
    require(kernel.free_partition_memory(low) == 0 && kernel.free_partition_memory(low) ==
        static_cast<std::int32_t>(sarusaru::kernel::kIllegalMemblock), "block free validation incorrect");

    auto &cpu = runtime.cpu();
    for (unsigned i = 0; i < 3; ++i) runtime.memory().store8(name_address + i, "arg"[i]);
    cpu.pc = 0x08804444u;
    cpu.set_gpr(28u, 0x08801234u);
    kernel.begin_main_thread(cpu, "main", 0x30u);
    const auto main_uid = kernel.current_uid();
    require(kernel.start_thread(cpu, uid, 3u, name_address) == 0, "thread start failed");
    thread = kernel.thread(uid);
    require(thread->state == sarusaru::kernel::EarlyKernel::ThreadState::Running && cpu.pc == 0x08805000u,
            "higher-priority thread did not run");
    require(cpu.gpr[4] == 3u && cpu.gpr[5] != 0u && cpu.gpr[26] == thread->control_block &&
                cpu.gpr[31] == sarusaru::kernel::kThreadExitStub,
            "thread context incorrect");

    sarusaru::kernel::register_early_threadman(kernel, runtime);
    for (unsigned i = 0; i < 7; ++i) runtime.memory().store8(name_address + i, "guest\0"[i]);
    cpu.set_gpr(4u, name_address); cpu.set_gpr(5u, 0x08807000u); cpu.set_gpr(6u, 0x21u);
    cpu.set_gpr(7u, 0x400u); cpu.set_gpr(8u, 0u); cpu.set_gpr(28u, 0x08801234u);
    runtime.invoke_import("ThreadManForUser", 0x446D8DE6u, cpu);
    const auto guest_uid = static_cast<sarusaru::kernel::ThreadUid>(cpu.gpr[2]);
    thread = kernel.thread(guest_uid);
    require(!runtime.stopped() && thread != nullptr && thread->name == "guest", "HLE thread creation failed");

    const auto callback_uid = kernel.create_callback("exit", 0x08809000u, 0xCAFEBABEu);
    const auto *callback = kernel.callback(callback_uid);
    require(callback != nullptr && callback->function == 0x08809000u && callback->common == 0xCAFEBABEu &&
                callback->owner == kernel.current_uid(),
            "callback creation lost state");
    require(static_cast<std::uint32_t>(kernel.create_callback("bad", 1u, 0u)) == sarusaru::kernel::kIllegalArgument,
            "unaligned callback entry accepted");
    require(kernel.register_exit_callback(callback_uid) == 0 && kernel.callback(callback_uid)->registered_as_exit,
            "exit callback registration failed");
    require(static_cast<std::uint32_t>(kernel.register_exit_callback(123)) == sarusaru::kernel::kIllegalArgument,
            "unknown exit callback accepted");
    require(kernel.register_power_callback(-1, callback_uid) == 0, "automatic power callback slot failed");
    require(kernel.register_power_callback(0, callback_uid) == static_cast<std::int32_t>(sarusaru::kernel::kPowerSlotsFull),
            "occupied power callback slot accepted");
    require(static_cast<std::uint32_t>(kernel.register_power_callback(16, callback_uid)) ==
                sarusaru::kernel::kPowerInvalidSlot,
            "invalid power callback slot accepted");
    require(static_cast<std::uint32_t>(kernel.register_power_callback(-1, 999)) == sarusaru::kernel::kPowerInvalidCallback,
            "unknown power callback accepted");
    require(static_cast<std::uint32_t>(kernel.register_umd_callback(999)) == sarusaru::kernel::kIllegalArgument &&
                kernel.register_umd_callback(callback_uid) == 0 && kernel.umd_callback() == callback_uid,
            "UMD callback registration incorrect");
    require(kernel.interrupts_enabled() && kernel.suspend_interrupts() == 1u && !kernel.interrupts_enabled(),
            "interrupt suspend state incorrect");
    require(kernel.suspend_interrupts() == 0u, "nested interrupt suspend state incorrect");
    kernel.resume_interrupts(true);
    require(kernel.interrupts_enabled(), "interrupt resume state incorrect");

    // Starting an equal-priority worker makes it ready. The current worker's
    // exit must hand execution to the earliest ready thread, never guest PC 0.
    const auto next_uid = kernel.create_thread("next", 0x08808000u, 0x20u, 0x200u, 0u, 0u);
    require(kernel.start_thread(cpu, next_uid, 0u, 0u) == 0, "ready worker start failed");
    kernel.exit_current_thread(cpu);
    thread = kernel.thread(next_uid);
    require(!runtime.stopped() && thread != nullptr && thread->state == sarusaru::kernel::EarlyKernel::ThreadState::Running &&
                cpu.pc == 0x08808000u,
            "thread exit did not schedule ready worker");

    // Sleeping inside an import hands over to the ready main thread; the
    // sleeper later resumes at its return address, not the import stub.
    using State = sarusaru::kernel::EarlyKernel::ThreadState;
    cpu.pc = 0x08808040u;
    cpu.set_gpr(31u, 0x08808100u);
    kernel.sleep_current_thread(cpu);
    require(!runtime.stopped() && kernel.current_uid() == main_uid && kernel.thread(next_uid)->state == State::Sleeping &&
                kernel.thread(next_uid)->context.pc == 0x08808100u,
            "sleep did not switch to ready thread");
    require(static_cast<std::uint32_t>(kernel.wakeup_thread(cpu, 999)) == sarusaru::kernel::kUnknownThread,
            "unknown thread wakeup accepted");
    require(static_cast<std::uint32_t>(kernel.wakeup_thread(cpu, guest_uid)) == sarusaru::kernel::kDormant,
            "dormant thread wakeup accepted");
    cpu.pc = 0x08804500u;
    cpu.set_gpr(31u, 0x08804600u);
    cpu.set_gpr(2u, 0xDEADBEEFu);
    require(kernel.wakeup_thread(cpu, next_uid) == 0 && kernel.current_uid() == next_uid && cpu.pc == 0x08808100u &&
                cpu.gpr[2] == 0u && kernel.thread(main_uid)->state == State::Ready &&
                kernel.thread(main_uid)->context.pc == 0x08804600u,
            "higher-priority wakeup did not preempt");

    // A pending wakeup makes the next sleep return at once.
    require(kernel.wakeup_thread(cpu, 0) == 0 && kernel.thread(next_uid)->wakeup_count == 1u, "self wakeup not counted");
    kernel.sleep_current_thread(cpu);
    require(kernel.current_uid() == next_uid && kernel.thread(next_uid)->wakeup_count == 0u && cpu.gpr[2] == 0u,
            "pending wakeup did not satisfy sleep");

    // With every thread asleep the kernel stops explicitly rather than idling.
    kernel.sleep_current_thread(cpu);
    require(kernel.current_uid() == main_uid, "sleep did not resume main thread");
    kernel.sleep_current_thread(cpu);
    require(runtime.stopped() && kernel.current_uid() == 0, "all-asleep state did not stop");
    return 0;
}
