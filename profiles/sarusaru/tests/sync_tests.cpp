#include "kernel/sync.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
using namespace sarusaru::kernel;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct Harness {
    psprecomp::Runtime runtime;
    EarlyKernel kernel;
    SyncObjects sync{kernel};
    psprecomp::AllegrexContext &cpu = runtime.cpu();

    Harness() {
        kernel.install(runtime, 0x08804000u, 0x0A000000u);
        cpu.pc = 0x08804000u;
        kernel.begin_main_thread(cpu, "main", 0x20u);
        register_early_threadman(kernel, runtime);
        register_sync(sync, runtime);
    }
    // Calls a ThreadManForUser import as the current thread, returning to `return_address`.
    void call(std::uint32_t nid, std::uint32_t return_address, std::initializer_list<std::uint32_t> arguments = {}) {
        std::uint32_t reg = 4u;
        for (const auto argument : arguments) cpu.set_gpr(reg++, argument);
        cpu.pc = 0x08A00000u;
        cpu.set_gpr(31u, return_address);
        runtime.invoke_import("ThreadManForUser", nid, cpu);
    }
};

constexpr std::uint32_t kWaitEventFlag = 0x402FCF22u, kSetEventFlag = 0x1FB15A32u, kWaitSema = 0x4E3A1105u,
                        kSignalSema = 0x3F53E640u, kDeleteSema = 0x28B6489Cu, kDelay = 0xCEADEB47u,
                        kWaitThreadEnd = 0x278C0DF5u, kExitThread = 0xAA73C935u;
constexpr std::uint32_t kOut = 0x08807000u, kTimeout = 0x08807004u;
}

int main() {
    Harness h;
    auto &memory = h.runtime.memory();
    const auto main_uid = h.kernel.current_uid();
    const auto worker = h.kernel.create_thread("worker", 0x08805000u, 0x30u, 0x400u, 0u, 0u);
    require(h.kernel.start_thread(h.cpu, worker, 0u, 0u) == 0 && h.kernel.current_uid() == main_uid, "worker start");

    require(static_cast<std::uint32_t>(h.sync.create_semaphore("bad", 0x200u, 0, 1)) == kIllegalAttribute &&
                static_cast<std::uint32_t>(h.sync.create_semaphore("bad", 0u, 2, 1)) == kIllegalCount &&
                static_cast<std::uint32_t>(h.sync.create_event_flag("bad", 0x400u, 0u)) == kIllegalAttribute,
            "object creation validation");

    // Event flag: AND wait released by the second Set, with clear-all, out bits and time left.
    const auto flag = h.sync.create_event_flag("flag", 0u, 0u);
    memory.store32(kTimeout, 10000u);
    h.call(kWaitEventFlag, 0x08804100u, {static_cast<std::uint32_t>(flag), 3u, kEventFlagClearAll, kOut, kTimeout});
    require(h.kernel.current_uid() == worker && h.cpu.pc == 0x08805000u, "event wait did not block");
    h.call(kDelay, 0x08805100u, {5000u});
    require(h.kernel.current_uid() == worker && h.kernel.now_us() == 5000u, "worker delay");
    h.call(kSetEventFlag, 0x08805200u, {static_cast<std::uint32_t>(flag), 1u});
    require(h.kernel.current_uid() == worker && h.cpu.gpr[2] == 0u, "partial pattern released AND waiter");
    h.call(kSetEventFlag, 0x08805300u, {static_cast<std::uint32_t>(flag), 2u});
    require(h.kernel.current_uid() == main_uid && h.cpu.pc == 0x08804100u && h.cpu.gpr[2] == 0u &&
                memory.load32(kOut) == 3u && memory.load32(kTimeout) == 5000u && h.sync.event_flag_pattern(flag) == 0u,
            "event wait release incorrect");

    // A second waiter without WAITMULTIPLE fails; the first times out with out bits.
    memory.store32(kTimeout, 100u);
    h.call(kWaitEventFlag, 0x08804200u, {static_cast<std::uint32_t>(flag), 4u, kEventFlagWaitOr, kOut, kTimeout});
    require(h.kernel.current_uid() == worker && h.cpu.pc == 0x08805300u, "OR wait did not block");
    h.call(kWaitEventFlag, 0x08805310u, {static_cast<std::uint32_t>(flag), 8u, kEventFlagWaitOr, 0u, 0u});
    require(h.cpu.gpr[2] == kEventFlagMultipleWaiters, "second waiter accepted");
    h.call(kWaitEventFlag, 0x08805320u, {static_cast<std::uint32_t>(flag), 0u, 0u, 0u, 0u});
    require(h.cpu.gpr[2] == kEventFlagIllegalPattern, "empty pattern accepted");
    h.call(kDelay, 0x08805400u, {1000u});
    require(h.kernel.current_uid() == main_uid && h.kernel.now_us() == 5100u && h.cpu.pc == 0x08804200u &&
                h.cpu.gpr[2] == kWaitTimeout && memory.load32(kTimeout) == 0u && memory.load32(kOut) == 0u,
            "event wait timeout incorrect");

    // Semaphore: a signal from a lower-priority thread preempts in favour of the waiter.
    const auto semaphore = h.sync.create_semaphore("sema", 0u, 0, 2);
    h.call(kWaitSema, 0x08804300u, {static_cast<std::uint32_t>(semaphore), 1u, 0u});
    require(h.kernel.current_uid() == worker && h.kernel.now_us() == 6000u && h.cpu.pc == 0x08805400u, "sema wait");
    h.call(kSignalSema, 0x08805500u, {static_cast<std::uint32_t>(semaphore), 1u});
    require(h.kernel.current_uid() == main_uid && h.cpu.pc == 0x08804300u && h.cpu.gpr[2] == 0u &&
                h.sync.semaphore_count(semaphore) == 0 && h.kernel.thread(worker)->context.gpr[2] == 0u,
            "sema release did not preempt");
    h.call(kSignalSema, 0x08804310u, {static_cast<std::uint32_t>(semaphore), 3u});
    require(h.cpu.gpr[2] == kSemaphoreOverflow, "sema overflow accepted");
    h.call(kSignalSema, 0x08804320u, {static_cast<std::uint32_t>(semaphore), 2u});
    h.call(kWaitSema, 0x08804330u, {static_cast<std::uint32_t>(semaphore), 3u, 0u});
    require(h.cpu.gpr[2] == kIllegalCount, "wait above maximum accepted");
    h.call(kWaitSema, 0x08804340u, {static_cast<std::uint32_t>(semaphore), 2u, 0u});
    require(h.cpu.gpr[2] == 0u && h.kernel.current_uid() == main_uid && h.sync.semaphore_count(semaphore) == 0,
            "available sema did not return at once");

    // Deleting an object releases its waiters with WAIT_DELETE.
    h.call(kWaitSema, 0x08804400u, {static_cast<std::uint32_t>(semaphore), 1u, 0u});
    require(h.kernel.current_uid() == worker && h.cpu.pc == 0x08805500u, "sema wait before delete");
    h.call(kDeleteSema, 0x08805600u, {static_cast<std::uint32_t>(semaphore)});
    require(h.kernel.current_uid() == main_uid && h.cpu.pc == 0x08804400u && h.cpu.gpr[2] == kWaitDelete &&
                static_cast<std::uint32_t>(h.sync.semaphore_count(semaphore)) == kUnknownSemaphore,
            "delete did not release waiter");

    // Thread end: the waiter receives the exit status; the thread can then be deleted.
    h.call(kWaitThreadEnd, 0x08804500u, {static_cast<std::uint32_t>(worker), 0u});
    require(h.kernel.current_uid() == worker && h.cpu.pc == 0x08805600u, "thread-end wait");
    h.call(kExitThread, 0x08805700u, {0x1234u});
    require(h.kernel.current_uid() == main_uid && h.cpu.pc == 0x08804500u && h.cpu.gpr[2] == 0x1234u &&
                h.kernel.thread_exit_status(worker) == 0x1234,
            "exit status not delivered");
    h.call(kWaitThreadEnd, 0x08804510u, {static_cast<std::uint32_t>(worker), 0u});
    require(h.cpu.gpr[2] == 0x1234u, "wait on dormant thread did not return its status");
    require(h.kernel.delete_thread(worker) == 0 &&
                static_cast<std::uint32_t>(h.kernel.thread_exit_status(worker)) == kUnknownThread,
            "thread delete failed");

    // Terminate, attributes and time queries.
    const auto victim = h.kernel.create_thread("victim", 0x08806000u, 0x30u, 0x400u, 0u, 0u);
    require(h.kernel.start_thread(h.cpu, victim, 0u, 0u) == 0, "victim start");
    require(static_cast<std::uint32_t>(h.kernel.terminate_thread(h.cpu, main_uid)) == kIllegalThread &&
                h.kernel.terminate_thread(h.cpu, victim) == 0 &&
                static_cast<std::uint32_t>(h.kernel.thread_exit_status(victim)) == kThreadTerminated &&
                static_cast<std::uint32_t>(h.kernel.terminate_thread(h.cpu, victim)) == kDormant,
            "terminate incorrect");
    require(h.kernel.change_current_thread_attributes(0u, kThreadAttrVfpu) == 0 &&
                h.kernel.thread(main_uid)->attributes == kThreadAttrVfpu &&
                static_cast<std::uint32_t>(h.kernel.change_current_thread_attributes(0u, 1u)) == kIllegalAttribute,
            "thread attribute change incorrect");
    h.call(0x369ED59Du, 0x08804600u);
    require(h.cpu.gpr[2] == 6000u, "system time incorrect");

    // Suspended dispatch: no preemption, and waits fail instead of blocking.
    const auto eager = h.kernel.create_thread("eager", 0x08806100u, 0x10u, 0x400u, 0u, 0u);
    h.call(0x3AD58B8Cu, 0x08804610u);
    require(h.cpu.gpr[2] == 1u && !h.kernel.dispatch_enabled(), "dispatch suspend");
    require(h.kernel.start_thread(h.cpu, eager, 0u, 0u) == 0 && h.kernel.current_uid() == main_uid,
            "higher-priority thread preempted with dispatch suspended");
    h.call(kDelay, 0x08804620u, {100u});
    require(h.kernel.current_uid() == main_uid && h.cpu.gpr[2] == kCanNotWait, "wait accepted with dispatch suspended");
    h.call(0x27E22EC2u, 0x08804630u, {1u});
    require(h.kernel.dispatch_enabled() && h.kernel.current_uid() == eager &&
                h.kernel.thread(main_uid)->context.pc == 0x08804630u,
            "resume did not let the ready thread preempt");
    h.call(kExitThread, 0x08806110u, {0u});
    require(h.kernel.current_uid() == main_uid, "main did not resume");
    h.kernel.suspend_interrupts();
    require(static_cast<std::uint32_t>(h.kernel.suspend_dispatch()) == kInterruptsDisabled, "suspend with interrupts off");
    h.kernel.resume_interrupts(true);

    // With no deadline and nothing runnable, execution stops and names the wait.
    const auto lonely = h.sync.create_semaphore("lonely", 0u, 0, 1);
    h.call(kWaitSema, 0x08804700u, {static_cast<std::uint32_t>(lonely), 1u, 0u});
    require(h.runtime.stopped() && h.runtime.stop_reason().find("semaphore lonely") != std::string::npos,
            "deadlock not reported");
    return 0;
}
