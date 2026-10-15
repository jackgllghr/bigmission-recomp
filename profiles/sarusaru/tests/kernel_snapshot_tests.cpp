#include "kernel/early_kernel.hpp"
#include "save_state/kernel_state.hpp"
#include "save_state/kernel_snapshot.hpp"
#include "psprecomp/common.hpp"

#include <bit>
#include <iostream>
#include <memory>
#include <optional>

namespace {
using Kernel = sarusaru::kernel::EarlyKernel;
using sarusaru::save_state::encode_kernel_state;
using sarusaru::save_state::decode_kernel_state;
constexpr std::uint32_t entry = 0x08804444u;
int failures{};
void check(bool value, const char *message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> void rejects(F f, const char *message) {
    try { f(); check(false, message); } catch (const std::runtime_error &) {}
}
struct Harness {
    psprecomp::Runtime runtime;
    Kernel kernel;
    Harness() {
        kernel.install(runtime, 0x08804000u, 0x09FF6000u);
        runtime.cpu().pc = entry;
        runtime.cpu().set_gpr(29u, 0x09FFE000u);
        kernel.begin_main_thread(runtime.cpu(), "main", 0x20u);
    }
    void call(std::uint32_t return_pc) {
        runtime.cpu().pc = 0x08805000u;
        runtime.cpu().set_gpr(31u, return_pc);
    }
    std::int32_t worker() {
        const auto id = kernel.create_thread("worker", 0x08806000u, 0x30u, 0x400u, 0u, 0u);
        check(id > 0 && kernel.start_thread(runtime.cpu(), id, 0u, 0u) == 0, "worker starts");
        return id;
    }
};

Kernel::State roundtrip(const Kernel::State &state) {
    const auto bytes = encode_kernel_state(state);
    auto result = decode_kernel_state(bytes);
    check(encode_kernel_state(result) == bytes, "complete detached kernel state round trips");
    return result;
}

void timed_continuation() {
    Kernel::State saved;
    std::int32_t main_id{}, worker_id{};
    {
        auto source = std::make_unique<Harness>();
        auto &k = source->kernel;
        main_id = k.current_uid();
        worker_id = source->worker();
        source->runtime.cpu().set_fpr_bits(7u, 0xFFC12345u);
        source->runtime.cpu().vfpu[127] = std::bit_cast<float>(0x80000000u);
        k.set_action_handler(Kernel::ActionKind::ControllerRead, [](const Kernel::Action &) { return 999u; });
        source->call(0x08804500u);
        k.wait_current_until(source->runtime.cpu(), 500u,
            {Kernel::ActionKind::ControllerRead, {0x08810000u, 1u}});
        check(k.current_uid() == worker_id, "snapshot has another thread waiting");
        saved = roundtrip(k.snapshot());
    }
    auto destination = std::make_unique<Harness>();
    auto &k = destination->kernel;
    int invoked = 0, observed_frames = 0;
    k.set_action_handler(Kernel::ActionKind::ControllerRead, [&](const Kernel::Action &a) {
        ++invoked;
        destination->runtime.memory().store32(a.arguments[0], 321u);
        return 321u;
    });
    k.set_frame_observer([&] { ++observed_frames; });
    k.restore(saved);
    destination->call(0x08806100u);
    k.delay_current_thread(destination->runtime.cpu(), 1000u);
    check(k.current_uid() == main_id && k.now_us() == 500u && invoked == 1 &&
        destination->runtime.cpu().pc == 0x08804500u && destination->runtime.cpu().gpr[2] == 321u &&
        destination->runtime.memory().load32(0x08810000u) == 321u,
        "restored timed action uses destination handler after source destruction");
    check(destination->runtime.cpu().fpr_bits(7u) == 0xFFC12345u &&
        std::bit_cast<std::uint32_t>(destination->runtime.cpu().vfpu[127]) == 0x80000000u,
        "blocked contexts preserve FPU and VFPU payload bits");
    // Host observers survive restoration; they are never serialized from source.
    destination->call(0x08804510u);
    k.delay_current_thread(destination->runtime.cpu(), 20'000u);
    destination->call(0x08806110u);
    k.delay_current_thread(destination->runtime.cpu(), 30'000u);
    check(observed_frames != 0, "destination frame observer remains attached");
}

void callback_continuations() {
    for (const bool sleeping : {false, true}) {
        Kernel::State saved;
        std::int32_t main_id{}, worker_id{};
        {
            auto source = std::make_unique<Harness>();
            main_id = source->kernel.current_uid();
            const auto cb = source->kernel.create_callback("notification", 0x08807000u, 17u);
            worker_id = source->worker();
            source->call(0x08804500u);
            if (sleeping) source->kernel.sleep_current_thread(source->runtime.cpu(), true);
            else source->kernel.delay_current_thread(source->runtime.cpu(), 1000u, true);
            source->call(0x08806100u);
            source->kernel.notify_callback(cb, 42u);
            source->kernel.return_with_preemption(source->runtime.cpu(), 0u);
            check(source->kernel.current_uid() == main_id && source->runtime.cpu().pc == 0x08807000u,
                "snapshot interrupts a callback-enabled wait");
            saved = roundtrip(source->kernel.snapshot());
        }
        auto destination = std::make_unique<Harness>();
        auto &k = destination->kernel;
        auto &cpu = destination->runtime.cpu();
        k.restore(saved);
        cpu.set_gpr(2u, 0u);
        k.guest_call_returned(cpu);
        check(k.current_uid() == worker_id && k.thread(main_id)->state ==
            (sleeping ? Kernel::ThreadState::Sleeping : Kernel::ThreadState::Waiting),
            "restored callback returns to its original wait");
        destination->call(0x08806110u);
        if (sleeping) check(k.wakeup_thread(cpu, main_id) == 0, "sleeping callback owner wakes");
        else k.delay_current_thread(cpu, 2000u);
        check(k.current_uid() == main_id && cpu.pc == 0x08804500u,
            "thread resumes after restored callback and wait complete");
    }
}

void interrupts_and_validation() {
    Kernel::State saved;
    std::int32_t main_id{};
    {
        auto source = std::make_unique<Harness>();
        main_id = source->kernel.current_uid();
        source->kernel.set_action_handler(Kernel::ActionKind::GeResumeList, [](const Kernel::Action &) { return 7u; });
        source->kernel.queue_interrupt({0x08807000u, {3u, 4u, 5u, 6u}, 0x08820000u, "GE signal",
            {Kernel::ActionKind::GeResumeList, {3u}}});
        source->call(0x08804500u);
        source->kernel.return_with_preemption(source->runtime.cpu(), 0u);
        check(source->kernel.in_interrupt(), "snapshot contains active guest interrupt");
        saved = roundtrip(source->kernel.snapshot());
    }
    auto destination = std::make_unique<Harness>();
    int finished = 0;
    destination->kernel.set_action_handler(Kernel::ActionKind::GeResumeList, [&](const Kernel::Action &a) {
        finished += static_cast<int>(a.arguments[0]); return 0u;
    });
    destination->kernel.restore(saved);
    destination->kernel.guest_call_returned(destination->runtime.cpu());
    check(finished == 3 && destination->kernel.current_uid() == main_id &&
        destination->runtime.cpu().pc == 0x08804500u, "restored interrupt completion binds destination GE action");
    const auto baseline = encode_kernel_state(destination->kernel.snapshot());
    auto broken = destination->kernel.snapshot();
    broken.current_uid = 1;
    rejects([&] { destination->kernel.restore(broken); }, "missing current thread rejected");
    check(encode_kernel_state(destination->kernel.snapshot()) == baseline, "failed restore leaves live kernel untouched");
    broken = destination->kernel.snapshot();
    broken.free_ranges.push_back(broken.free_ranges.front());
    rejects([&] { destination->kernel.restore(broken); }, "overlapping free memory rejected");
    broken = destination->kernel.snapshot();
    broken.threads.at(main_id).on_wake.kind = static_cast<Kernel::ActionKind>(1000u);
    rejects([&] { destination->kernel.restore(broken); }, "unknown action rejected");
    auto bytes = baseline;
    bytes[16] = 3;
    rejects([&] { (void)decode_kernel_state(bytes); }, "future internal state version rejected");
    bytes = baseline;
    bytes.push_back(0);
    rejects([&] { (void)decode_kernel_state(bytes); }, "trailing internal state bytes rejected");
    for (const auto size : {0u, 20u, 100u, 500u})
        rejects([&] { (void)decode_kernel_state(std::span(baseline).first(size)); }, "truncated internal state rejected");
}

void synchronized_objects() {
    using sarusaru::save_state::KernelSnapshot;
    using namespace sarusaru::kernel;
    for (const bool event : {false, true}) {
        std::vector<std::uint8_t> bytes;
        std::int32_t main_id{}, object_id{};
        constexpr auto timeout = 0x08810000u, output = timeout + 4u;
        {
            auto source = std::make_unique<Harness>();
            SyncObjects sync(source->kernel);
            InterruptManager interrupts(source->kernel);
            main_id = source->kernel.current_uid();
            source->worker();
            object_id = event ? sync.create_event_flag("flag", 0, 0x10) : sync.create_semaphore("sema", 0, 0, 3);
            check(interrupts.register_handler(kInterruptGe, 2, 0x08807000, 99, 0x08820000) == 0 &&
                interrupts.enable(kInterruptGe, 2) == 0, "interrupt handler installed before capture");
            source->runtime.memory().store32(timeout, 500);
            source->call(0x08804500);
            if (event) sync.wait_event_flag(source->runtime.cpu(), object_id, 1, 0, output, timeout);
            else sync.wait_semaphore(source->runtime.cpu(), object_id, 2, timeout);
            const auto identity = source->kernel.thread(main_id)->wait_identity;
            check(identity.type == (event ? Kernel::WaitType::EventFlag : Kernel::WaitType::Semaphore) &&
                identity.object == static_cast<std::uint32_t>(object_id) && identity.value == (event ? 1u : 2u),
                "blocked synchronization threads retain typed object identity");
            bytes = KernelSnapshot::capture(source->kernel, sync, interrupts).encode();
            check(KernelSnapshot::decode(bytes).encode() == bytes, "joint kernel records round trip exactly");
        }
        auto destination = std::make_unique<Harness>();
        SyncObjects sync(destination->kernel);
        InterruptManager interrupts(destination->kernel);
        destination->runtime.memory().store32(timeout, 500);
        destination->runtime.memory().store32(output, 0xDEADBEEF);
        KernelSnapshot::decode(bytes).restore(destination->kernel, sync, interrupts);
        destination->call(0x08806100);
        destination->kernel.delay_current_thread(destination->runtime.cpu(), 1000);
        check(destination->kernel.current_uid() == main_id && destination->runtime.cpu().gpr[2] == kWaitTimeout &&
            destination->runtime.memory().load32(timeout) == 0, "restored synchronization timeout completes on destination");
        destination->call(0x08804510);
        if (event) {
            check(destination->runtime.memory().load32(output) == 0x10, "event timeout writes restored pattern");
            sync.set_event_flag(destination->runtime.cpu(), object_id, 1);
            sync.wait_event_flag(destination->runtime.cpu(), object_id, 1, kEventFlagClearPattern, output, 0);
            check(destination->runtime.cpu().gpr[2] == 0 && sync.event_flag_pattern(object_id) == 0x10,
                "event timeout removes waiter and restored event can be consumed");
            sync.delete_event_flag(destination->runtime.cpu(), object_id);
        } else {
            sync.signal_semaphore(destination->runtime.cpu(), object_id, 2);
            check(sync.semaphore_count(object_id) == 2, "semaphore timeout removes restored waiter");
            sync.delete_semaphore(destination->runtime.cpu(), object_id);
        }
        // Completed waits must not retain references to subsequently deleted objects.
        auto baseline = KernelSnapshot::capture(destination->kernel, sync, interrupts).encode();
        check(!baseline.empty(), "completed wait has no stale object continuation");
        check(interrupts.raise(kInterruptGe) == 1, "restored handler registration raises interrupt");
        destination->kernel.return_with_preemption(destination->runtime.cpu(), 0);
        check(destination->kernel.in_interrupt() && destination->runtime.cpu().pc == 0x08807000 &&
            destination->runtime.cpu().gpr[4] == 2 && destination->runtime.cpu().gpr[5] == 99 &&
            destination->runtime.cpu().gpr[28] == 0x08820000, "restored handler preserves argument and GP");
        destination->kernel.guest_call_returned(destination->runtime.cpu());

        // Corrupt a valid detached scheduler's current UID while preserving the
        // surrounding synchronization records, then verify transactional restore.
        auto invalid = bytes;
        const auto scheduler_size = static_cast<std::uint32_t>(invalid[20]) |
            (static_cast<std::uint32_t>(invalid[21]) << 8) | (static_cast<std::uint32_t>(invalid[22]) << 16) |
            (static_cast<std::uint32_t>(invalid[23]) << 24);
        auto state = decode_kernel_state(std::span(invalid).subspan(24, scheduler_size));
        state.current_uid = 1;
        const auto broken = encode_kernel_state(state);
        std::copy(broken.begin(), broken.end(), invalid.begin() + 24);
        baseline = KernelSnapshot::capture(destination->kernel, sync, interrupts).encode();
        rejects([&] { KernelSnapshot::decode(invalid).restore(destination->kernel, sync, interrupts); },
            "invalid joint restore rejected");
        check(KernelSnapshot::capture(destination->kernel, sync, interrupts).encode() == baseline,
            "failed joint restore preserves all destination modules");
    }
}
void distinct_wait_objects() {
    using sarusaru::save_state::KernelSnapshot;
    using namespace sarusaru::kernel;
    std::vector<std::uint8_t> bytes;
    std::int32_t main_id{}, first{}, second{};
    {
        auto source = std::make_unique<Harness>();
        SyncObjects sync(source->kernel); InterruptManager interrupts(source->kernel);
        main_id = source->kernel.current_uid(); source->worker();
        first = sync.create_semaphore("same label", 0, 0, 2); second = sync.create_semaphore("same label", 0, 0, 2);
        source->call(0x08804500); sync.wait_semaphore(source->runtime.cpu(), first, 1, 0);
        bytes = KernelSnapshot::capture(source->kernel, sync, interrupts).encode();
    }
    auto dest = std::make_unique<Harness>(); SyncObjects sync(dest->kernel); InterruptManager interrupts(dest->kernel);
    KernelSnapshot::decode(bytes).restore(dest->kernel, sync, interrupts);
    check(dest->kernel.release_wait(main_id, 0), "external wait release succeeds");
    dest->call(0x08806100); dest->kernel.return_with_preemption(dest->runtime.cpu(), 0);
    dest->call(0x08804510); sync.wait_semaphore(dest->runtime.cpu(), second, 1, 0);
    dest->call(0x08806110); sync.signal_semaphore(dest->runtime.cpu(), first, 1);
    check(dest->kernel.thread(main_id)->state == Kernel::ThreadState::Waiting && sync.semaphore_count(first) == 1 &&
        dest->kernel.thread(main_id)->wait_identity.object == static_cast<std::uint32_t>(second),
        "stale waiter on identically named object cannot release a different wait");
    sync.signal_semaphore(dest->runtime.cpu(), second, 1);
    check(dest->kernel.current_uid() == main_id && sync.semaphore_count(second) == 0,
        "matching restored object releases the new wait");
}

void imported_call(psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) { rt.invoke_import("snapshot-test", 1u, cpu); }
void finish(psprecomp::Runtime &rt, psprecomp::AllegrexContext &) { rt.stop("finished"); }
void dispatch_boundary() {
    auto h = std::make_unique<Harness>();
    std::optional<Kernel::State> saved;
    h->runtime.register_function(entry, imported_call, "snapshot request");
    h->runtime.register_function(entry + 4u, finish, "finish");
    h->runtime.register_hle("snapshot-test", 1u, [&](psprecomp::Runtime &rt, psprecomp::AllegrexContext &cpu) {
        check(!rt.can_snapshot(), "live native/HLE frame is not a snapshot point");
        rejects([&] { (void)h->kernel.snapshot(); }, "capture during guest dispatch rejected");
        cpu.pc = entry + 4u;
        cpu.set_gpr(17u, 123u);
        rt.set_dispatch_boundary_hook([&](psprecomp::Runtime &boundary) {
            check(boundary.can_snapshot(), "outer dispatch boundary permits snapshot");
            saved = h->kernel.snapshot();
            h->kernel.restore(*saved);
            boundary.set_dispatch_boundary_hook({});
        });
    });
    h->runtime.run(entry, 3);
    check(h->runtime.stop_reason() == "finished" && saved && saved->threads.at(saved->current_uid).context.pc == entry + 4u &&
        saved->threads.at(saved->current_uid).context.gpr[17] == 123u,
        "request captures and restores completed dispatch context");
}
void isolated_dispatch() {
    auto h = std::make_unique<Harness>();
    h->runtime.register_function(entry, imported_call, "isolated snapshot request");
    h->runtime.register_hle("snapshot-test", 1, [&](psprecomp::Runtime &rt, psprecomp::AllegrexContext &) {
        check(!rt.can_snapshot(), "isolated native frame is not a snapshot point outside Runtime::run");
        rejects([&] { (void)h->kernel.snapshot(); }, "capture inside isolated AOT rejected");
        rejects([&] { rt.run(entry, 1); }, "nested run inside isolated AOT rejected");
        throw std::runtime_error("isolated guest call failed");
    });
    rejects([&] { (void)h->runtime.invoke_isolated_aot(entry, h->runtime.cpu()); }, "isolated exception propagated");
    check(h->runtime.can_snapshot(), "isolated exception restores snapshot availability after unwind");
    (void)h->kernel.snapshot();
}
} // namespace

int main() {
    timed_continuation(); callback_continuations(); interrupts_and_validation(); dispatch_boundary();
    synchronized_objects();
    distinct_wait_objects();
    isolated_dispatch();
    std::cout << (failures ? "FAIL" : "PASS") << ": kernel snapshot continuations (" << failures << " failures)\n";
    return failures != 0;
}
