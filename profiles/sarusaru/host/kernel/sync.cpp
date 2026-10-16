#include "kernel/sync.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <optional>

namespace sarusaru::kernel {
namespace {
template <typename Object>
void remove_waiter(std::map<std::int32_t, Object> &objects, std::int32_t uid, ThreadUid thread) {
    const auto found = objects.find(uid);
    if (found == objects.end()) return;
    std::erase_if(found->second.waiters, [thread](const auto &waiter) { return waiter.thread == thread; });
}
}

SyncObjects::SyncObjects(EarlyKernel &kernel) : kernel_(&kernel) {
    kernel.set_action_handler(EarlyKernel::ActionKind::SemaphoreTimeout, [this](const EarlyKernel::Action &action) {
        remove_waiter(semaphores_, static_cast<std::int32_t>(action.arguments[0]), static_cast<ThreadUid>(action.arguments[1]));
        return 0u;
    });
    kernel.set_action_handler(EarlyKernel::ActionKind::EventFlagTimeout, [this](const EarlyKernel::Action &action) {
        const auto uid = static_cast<std::int32_t>(action.arguments[0]);
        const auto found = event_flags_.find(uid);
        if (found != event_flags_.end()) {
            if (action.arguments[2] != 0u) kernel_->runtime().memory().store32(action.arguments[2], found->second.pattern);
            remove_waiter(event_flags_, uid, static_cast<ThreadUid>(action.arguments[1]));
        }
        return 0u;
    });
}

void SyncObjects::order_waiters(std::deque<Waiter> &waiters, std::uint32_t attributes, EarlyKernel::WaitType type, std::int32_t uid) const {
    std::erase_if(waiters, [this, type, uid](const Waiter &waiter) {
        const auto *thread = kernel_->thread(waiter.thread);
        return thread == nullptr || thread->state != EarlyKernel::ThreadState::Waiting ||
            thread->wait_identity.type != type || thread->wait_identity.object != static_cast<std::uint32_t>(uid);
    });
    if ((attributes & kAttrPriorityQueue) != 0u) {
        std::ranges::stable_sort(waiters, [this](const Waiter &left, const Waiter &right) {
            return kernel_->thread(left.thread)->priority < kernel_->thread(right.thread)->priority;
        });
    }
}

std::int32_t SyncObjects::create_semaphore(std::string name, std::uint32_t attributes, std::int32_t initial,
                                           std::int32_t maximum) {
    if ((attributes & ~kAttrPriorityQueue) != 0u) return static_cast<std::int32_t>(kIllegalAttribute);
    if (initial < 0 || maximum <= 0 || initial > maximum) return static_cast<std::int32_t>(kIllegalCount);
    const auto uid = kernel_->allocate_uid();
    semaphores_.emplace(uid, Semaphore{std::move(name), attributes, initial, maximum, {}});
    return uid;
}

void SyncObjects::delete_semaphore(psprecomp::AllegrexContext &context, std::int32_t uid) {
    const auto found = semaphores_.find(uid);
    if (found == semaphores_.end()) { context.set_gpr(2u, kUnknownSemaphore); return; }
    const auto waiters = std::move(found->second.waiters);
    semaphores_.erase(found);
    for (const auto &waiter : waiters) kernel_->release_wait(waiter.thread, kWaitDelete);
    kernel_->return_with_preemption(context, 0u);
}

void SyncObjects::signal_semaphore(psprecomp::AllegrexContext &context, std::int32_t uid, std::int32_t count) {
    const auto found = semaphores_.find(uid);
    if (found == semaphores_.end()) { context.set_gpr(2u, kUnknownSemaphore); return; }
    Semaphore &semaphore = found->second;
    if (count <= 0) { context.set_gpr(2u, kIllegalCount); return; }
    if (static_cast<std::int64_t>(semaphore.count) + count > semaphore.maximum) {
        context.set_gpr(2u, kSemaphoreOverflow);
        return;
    }
    semaphore.count += count;
    order_waiters(semaphore.waiters, semaphore.attributes, EarlyKernel::WaitType::Semaphore, uid);
    // Waiters are served strictly in queue order: an unsatisfiable head blocks the rest.
    while (!semaphore.waiters.empty() && static_cast<std::int32_t>(semaphore.waiters.front().value) <= semaphore.count) {
        semaphore.count -= static_cast<std::int32_t>(semaphore.waiters.front().value);
        kernel_->release_wait(semaphore.waiters.front().thread, 0u);
        semaphore.waiters.pop_front();
    }
    kernel_->return_with_preemption(context, 0u);
}

void SyncObjects::wait_semaphore(psprecomp::AllegrexContext &context, std::int32_t uid, std::int32_t count,
                                 std::uint32_t timeout_address) {
    const auto found = semaphores_.find(uid);
    if (found == semaphores_.end()) { context.set_gpr(2u, kUnknownSemaphore); return; }
    Semaphore &semaphore = found->second;
    if (count <= 0 || count > semaphore.maximum) { context.set_gpr(2u, kIllegalCount); return; }
    order_waiters(semaphore.waiters, semaphore.attributes, EarlyKernel::WaitType::Semaphore, uid);
    if (semaphore.waiters.empty() && semaphore.count >= count) {
        semaphore.count -= count;
        context.set_gpr(2u, 0u);
        return;
    }
    const ThreadUid thread = kernel_->current_uid();
    semaphore.waiters.push_back({thread, static_cast<std::uint32_t>(count), 0u, 0u});
    auto wait = kernel_->object_wait("semaphore " + semaphore.name, timeout_address,
        {EarlyKernel::ActionKind::SemaphoreTimeout, {static_cast<std::uint32_t>(uid), static_cast<std::uint32_t>(thread)}});
    wait.identity = {EarlyKernel::WaitType::Semaphore, static_cast<std::uint32_t>(uid), static_cast<std::uint32_t>(count)};
    kernel_->block_current(context, std::move(wait));
}

std::int32_t SyncObjects::semaphore_count(std::int32_t uid) const noexcept {
    const auto found = semaphores_.find(uid);
    return found == semaphores_.end() ? static_cast<std::int32_t>(kUnknownSemaphore) : found->second.count;
}

std::int32_t SyncObjects::create_event_flag(std::string name, std::uint32_t attributes, std::uint32_t pattern) {
    if ((attributes & ~(kAttrPriorityQueue | kEventFlagWaitMultiple)) != 0u)
        return static_cast<std::int32_t>(kIllegalAttribute);
    const auto uid = kernel_->allocate_uid();
    event_flags_.emplace(uid, EventFlag{std::move(name), attributes, pattern, {}});
    return uid;
}

void SyncObjects::delete_event_flag(psprecomp::AllegrexContext &context, std::int32_t uid) {
    const auto found = event_flags_.find(uid);
    if (found == event_flags_.end()) { context.set_gpr(2u, kUnknownEventFlag); return; }
    const auto waiters = std::move(found->second.waiters);
    event_flags_.erase(found);
    for (const auto &waiter : waiters) kernel_->release_wait(waiter.thread, kWaitDelete);
    kernel_->return_with_preemption(context, 0u);
}

bool SyncObjects::satisfied(std::uint32_t pattern, std::uint32_t bits, std::uint32_t mode) noexcept {
    return (mode & kEventFlagWaitOr) != 0u ? (pattern & bits) != 0u : (pattern & bits) == bits;
}

void SyncObjects::finish_event_wait(EventFlag &flag, const Waiter &waiter) {
    if (waiter.out_address != 0u) kernel_->runtime().memory().store32(waiter.out_address, flag.pattern);
    if ((waiter.mode & kEventFlagClearAll) != 0u) flag.pattern = 0u;
    else if ((waiter.mode & kEventFlagClearPattern) != 0u) flag.pattern &= ~waiter.value;
}

void SyncObjects::set_event_flag(psprecomp::AllegrexContext &context, std::int32_t uid, std::uint32_t bits) {
    const auto found = event_flags_.find(uid);
    if (found == event_flags_.end()) { context.set_gpr(2u, kUnknownEventFlag); return; }
    EventFlag &flag = found->second;
    flag.pattern |= bits;
    order_waiters(flag.waiters, flag.attributes, EarlyKernel::WaitType::EventFlag, uid);
    // Every satisfied waiter is released in order; earlier clears can unsatisfy later ones.
    for (auto it = flag.waiters.begin(); it != flag.waiters.end();) {
        if (!satisfied(flag.pattern, it->value, it->mode)) { ++it; continue; }
        finish_event_wait(flag, *it);
        kernel_->release_wait(it->thread, 0u);
        it = flag.waiters.erase(it);
    }
    kernel_->return_with_preemption(context, 0u);
}

std::int32_t SyncObjects::clear_event_flag(std::int32_t uid, std::uint32_t bits) noexcept {
    const auto found = event_flags_.find(uid);
    if (found == event_flags_.end()) return static_cast<std::int32_t>(kUnknownEventFlag);
    found->second.pattern &= bits;
    return 0;
}

void SyncObjects::wait_event_flag(psprecomp::AllegrexContext &context, std::int32_t uid, std::uint32_t bits,
                                  std::uint32_t mode, std::uint32_t out_address, std::uint32_t timeout_address) {
    if ((mode & ~(kEventFlagWaitOr | kEventFlagClearAll | kEventFlagClearPattern)) != 0u ||
        (mode & (kEventFlagClearAll | kEventFlagClearPattern)) == (kEventFlagClearAll | kEventFlagClearPattern)) {
        context.set_gpr(2u, kIllegalMode);
        return;
    }
    if (bits == 0u) { context.set_gpr(2u, kEventFlagIllegalPattern); return; }
    const auto found = event_flags_.find(uid);
    if (found == event_flags_.end()) { context.set_gpr(2u, kUnknownEventFlag); return; }
    EventFlag &flag = found->second;
    order_waiters(flag.waiters, flag.attributes, EarlyKernel::WaitType::EventFlag, uid);
    if ((flag.attributes & kEventFlagWaitMultiple) == 0u && !flag.waiters.empty()) {
        context.set_gpr(2u, kEventFlagMultipleWaiters);
        return;
    }
    const Waiter waiter{kernel_->current_uid(), bits, mode, out_address};
    if (satisfied(flag.pattern, bits, mode)) {
        finish_event_wait(flag, waiter);
        context.set_gpr(2u, 0u);
        return;
    }
    flag.waiters.push_back(waiter);
    auto wait = kernel_->object_wait("event flag " + flag.name, timeout_address,
        {EarlyKernel::ActionKind::EventFlagTimeout,
         {static_cast<std::uint32_t>(uid), static_cast<std::uint32_t>(waiter.thread), waiter.out_address}});
    wait.identity = {EarlyKernel::WaitType::EventFlag, static_cast<std::uint32_t>(uid), bits};
    kernel_->block_current(context, std::move(wait));
}

std::uint32_t SyncObjects::event_flag_pattern(std::int32_t uid) const noexcept {
    const auto found = event_flags_.find(uid);
    return found == event_flags_.end() ? 0u : found->second.pattern;
}

void register_sync(SyncObjects &sync, psprecomp::Runtime &runtime) {
    const auto read_name = [](psprecomp::Runtime &rt, std::uint32_t address) -> std::optional<std::string> {
        try { return rt.memory().read_c_string(address, 32u); }
        catch (const psprecomp::Error &) { return std::nullopt; }
    };
    runtime.register_hle("ThreadManForUser", 0xD6DA4BA1u,  // sceKernelCreateSema
        [&sync, read_name](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto name = read_name(rt, ctx.gpr[4]);
            if (!name) { ctx.set_gpr(2u, kIllegalArgument); return; }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(sync.create_semaphore(*name, ctx.gpr[5],
                static_cast<std::int32_t>(ctx.gpr[6]), static_cast<std::int32_t>(ctx.gpr[7]))));
        });
    runtime.register_hle("ThreadManForUser", 0x28B6489Cu,  // sceKernelDeleteSema
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            sync.delete_semaphore(ctx, static_cast<std::int32_t>(ctx.gpr[4]));
        });
    runtime.register_hle("ThreadManForUser", 0x3F53E640u,  // sceKernelSignalSema
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            sync.signal_semaphore(ctx, static_cast<std::int32_t>(ctx.gpr[4]), static_cast<std::int32_t>(ctx.gpr[5]));
        });
    runtime.register_hle("ThreadManForUser", 0x4E3A1105u,  // sceKernelWaitSema
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            sync.wait_semaphore(ctx, static_cast<std::int32_t>(ctx.gpr[4]), static_cast<std::int32_t>(ctx.gpr[5]), ctx.gpr[6]);
        });
    runtime.register_hle("ThreadManForUser", 0x55C20A00u,  // sceKernelCreateEventFlag
        [&sync, read_name](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto name = read_name(rt, ctx.gpr[4]);
            if (!name) { ctx.set_gpr(2u, kIllegalArgument); return; }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(sync.create_event_flag(*name, ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("ThreadManForUser", 0xEF9E4C70u,  // sceKernelDeleteEventFlag
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            sync.delete_event_flag(ctx, static_cast<std::int32_t>(ctx.gpr[4]));
        });
    runtime.register_hle("ThreadManForUser", 0x1FB15A32u,  // sceKernelSetEventFlag
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            sync.set_event_flag(ctx, static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5]);
        });
    runtime.register_hle("ThreadManForUser", 0x812346E4u,  // sceKernelClearEventFlag
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(sync.clear_event_flag(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5])));
        });
    runtime.register_hle("ThreadManForUser", 0x402FCF22u,  // sceKernelWaitEventFlag
        [&sync](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            sync.wait_event_flag(ctx, static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[8]);
        });
}

} // namespace sarusaru::kernel
