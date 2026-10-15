#include "save_state/kernel_snapshot.hpp"
#include "save_state/kernel_state.hpp"
#include "psprecomp/common.hpp"

#include <algorithm>
#include <bit>
#include <set>

namespace sarusaru::save_state {
using Kernel = kernel::EarlyKernel;
struct KernelSnapshot::Data {
    Kernel::State scheduler;
    decltype(kernel::SyncObjects::semaphores_) semaphores;
    decltype(kernel::SyncObjects::event_flags_) events;
    decltype(kernel::InterruptManager::handlers_) handlers;
};
namespace {
std::uint32_t count(PayloadReader &r, std::uint32_t maximum, std::size_t width) {
    const auto n = r.u32();
    if (n > maximum || n > r.remaining() / width) throw FormatError("Kernel snapshot: invalid object count");
    return n;
}
template<class Map, class Key, class Value> void insert(Map &m, Key key, Value value) {
    if (!m.emplace(key, std::move(value)).second) throw FormatError("Kernel snapshot: duplicate object key");
}
std::int32_t signed_word(PayloadReader &r) { return std::bit_cast<std::int32_t>(r.u32()); }
void signed_word(PayloadWriter &w, std::int32_t value) { w.u32(std::bit_cast<std::uint32_t>(value)); }
} // namespace

KernelSnapshot::KernelSnapshot(std::unique_ptr<Data> data) : data_(std::move(data)) {}
KernelSnapshot::~KernelSnapshot() = default;
KernelSnapshot::KernelSnapshot(KernelSnapshot &&) noexcept = default;
KernelSnapshot &KernelSnapshot::operator=(KernelSnapshot &&) noexcept = default;

KernelSnapshot KernelSnapshot::capture(const Kernel &kernel, const kernel::SyncObjects &sync,
                                       const kernel::InterruptManager &interrupts) {
    if (sync.kernel_ != &kernel || interrupts.kernel_ != &kernel) throw FormatError("Kernel snapshot: mismatched module bindings");
    auto data = std::make_unique<Data>();
    data->scheduler = kernel.snapshot();
    data->semaphores = sync.semaphores_;
    data->events = sync.event_flags_;
    data->handlers = interrupts.handlers_;
    KernelSnapshot result(std::move(data));
    result.validate(kernel);
    return result;
}

void KernelSnapshot::validate(const Kernel &kernel) const {
    if (!data_) throw FormatError("Kernel snapshot has been consumed");
    const auto &d = *data_;
    kernel.validate_state(d.scheduler);
    if (d.semaphores.size() + d.events.size() > 4096 || d.handlers.size() > 67u * kernel::kSubInterruptCount)
        throw FormatError("Kernel snapshot: too many objects");
    std::set<std::int32_t> uids;
    for (const auto &[id, value] : d.scheduler.threads) { (void)value; uids.insert(id); }
    for (const auto &[id, value] : d.scheduler.callbacks) { (void)value; uids.insert(id); }
    for (const auto &[id, value] : d.scheduler.blocks) { (void)value; uids.insert(id); }
    const auto object_id = [&](std::int32_t uid) {
        if (uid <= 0 || uid >= d.scheduler.next_uid || !uids.insert(uid).second)
            throw FormatError("Kernel snapshot: invalid synchronization UID");
    };
    const auto waiters = [&](const auto &values, bool event) {
        if (values.size() > 4096) throw FormatError("Kernel snapshot: too many waiters");
        std::set<std::int32_t> seen;
        for (const auto &waiter : values) {
            if (waiter.thread <= 0 || waiter.thread >= d.scheduler.next_uid || !seen.insert(waiter.thread).second)
                throw FormatError("Kernel snapshot: invalid waiter UID");
            // Terminated/deleted waiters can remain until the next object call;
            // order_waiters() drops those entries without accessing the guest.
            if (event && ((waiter.mode & ~(kernel::kEventFlagWaitOr | kernel::kEventFlagClearAll | kernel::kEventFlagClearPattern)) != 0 ||
                ((waiter.mode & kernel::kEventFlagClearAll) && (waiter.mode & kernel::kEventFlagClearPattern))))
                throw FormatError("Kernel snapshot: invalid event wait mode");
            if (waiter.out_address != 0 && !kernel.runtime_->memory().contains(waiter.out_address, 4))
                throw FormatError("Kernel snapshot: invalid waiter output address");
        }
    };
    for (const auto &[uid, semaphore] : d.semaphores) {
        object_id(uid);
        if (semaphore.name.size() > 1024 || (semaphore.attributes & ~kernel::kAttrPriorityQueue) != 0 ||
            semaphore.maximum <= 0 || semaphore.count < 0 || semaphore.count > semaphore.maximum)
            throw FormatError("Kernel snapshot: invalid semaphore");
        waiters(semaphore.waiters, false);
        for (const auto &waiter : semaphore.waiters)
            if (waiter.value == 0 || waiter.value > static_cast<std::uint32_t>(semaphore.maximum))
                throw FormatError("Kernel snapshot: invalid semaphore wait count");
    }
    for (const auto &[uid, flag] : d.events) {
        object_id(uid);
        if (flag.name.size() > 1024 || (flag.attributes & ~(kernel::kAttrPriorityQueue | kernel::kEventFlagWaitMultiple)) != 0)
            throw FormatError("Kernel snapshot: invalid event flag");
        waiters(flag.waiters, true);
    }
    const auto check_action = [&](const Kernel::Action &action) {
        if (action.kind == Kernel::ActionKind::SemaphoreTimeout &&
            !d.semaphores.contains(static_cast<std::int32_t>(action.arguments[0])))
            throw FormatError("Kernel snapshot: continuation references missing semaphore");
        if (action.kind == Kernel::ActionKind::EventFlagTimeout &&
            !d.events.contains(static_cast<std::int32_t>(action.arguments[0])))
            throw FormatError("Kernel snapshot: continuation references missing event flag");
    };
    for (const auto &[id, thread] : d.scheduler.threads) {
        (void)id;
        check_action(thread.on_wake);
        if (thread.resume_wait) check_action(thread.resume_wait->on_timeout);
    }
    for (const auto &[key, handler] : d.handlers)
        if (key.first >= 67 || key.second >= kernel::kSubInterruptCount || (handler.address & 3u) != 0)
            throw FormatError("Kernel snapshot: invalid interrupt handler");
}

std::vector<std::uint8_t> KernelSnapshot::encode() const {
    if (!data_) throw FormatError("Kernel snapshot has been consumed");
    const auto &d = *data_;
    PayloadWriter w;
    w.section("RecompKernelSet", 1);
    const auto scheduler = encode_kernel_state(d.scheduler);
    w.u32(static_cast<std::uint32_t>(scheduler.size())); w.bytes(scheduler);
    const auto waiters = [&](const auto &values) {
        w.u32(static_cast<std::uint32_t>(values.size()));
        for (const auto &waiter : values) {
            signed_word(w, waiter.thread); w.u32(waiter.value); w.u32(waiter.mode); w.u32(waiter.out_address);
        }
    };
    w.u32(static_cast<std::uint32_t>(d.semaphores.size()));
    for (const auto &[uid, value] : d.semaphores) {
        signed_word(w, uid); w.string(value.name); w.u32(value.attributes); signed_word(w, value.count); signed_word(w, value.maximum);
        waiters(value.waiters);
    }
    w.u32(static_cast<std::uint32_t>(d.events.size()));
    for (const auto &[uid, value] : d.events) {
        signed_word(w, uid); w.string(value.name); w.u32(value.attributes); w.u32(value.pattern); waiters(value.waiters);
    }
    w.u32(static_cast<std::uint32_t>(d.handlers.size()));
    for (const auto &[key, value] : d.handlers) {
        w.u32(key.first); w.u32(key.second); w.u32(value.address); w.u32(value.argument); w.u32(value.gp); w.boolean(value.enabled);
    }
    w.end_section();
    return w.data();
}

KernelSnapshot KernelSnapshot::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize) throw FormatError("Kernel snapshot: size exceeds limit");
    PayloadReader r(bytes);
    (void)r.section("RecompKernelSet", 1, 1);
    auto d = std::make_unique<Data>();
    const auto scheduler_size = r.u32();
    d->scheduler = decode_kernel_state(r.bytes(scheduler_size));
    const auto waiters = [&](auto &values) {
        for (auto n = count(r, 4096, 16); n != 0; --n)
            values.push_back({signed_word(r), r.u32(), r.u32(), r.u32()});
    };
    for (auto n = count(r, 4096, 25); n != 0; --n) {
        const auto uid = signed_word(r);
        kernel::SyncObjects::Semaphore value;
        value.name = r.string(); value.attributes = r.u32(); value.count = signed_word(r); value.maximum = signed_word(r);
        waiters(value.waiters); insert(d->semaphores, uid, std::move(value));
    }
    for (auto n = count(r, 4096, 21); n != 0; --n) {
        const auto uid = signed_word(r);
        kernel::SyncObjects::EventFlag value;
        value.name = r.string(); value.attributes = r.u32(); value.pattern = r.u32();
        waiters(value.waiters); insert(d->events, uid, std::move(value));
    }
    for (auto n = count(r, 67u * kernel::kSubInterruptCount, 21); n != 0; --n) {
        const auto interrupt = r.u32(), sub = r.u32();
        kernel::InterruptManager::Handler value{r.u32(), r.u32(), r.u32(), r.boolean()};
        insert(d->handlers, std::pair{interrupt, sub}, value);
    }
    r.end_section(); r.finish();
    return KernelSnapshot(std::move(d));
}

void KernelSnapshot::restore(Kernel &kernel, kernel::SyncObjects &sync, kernel::InterruptManager &interrupts) && {
    if (!kernel.runtime_ || !kernel.runtime_->can_snapshot()) throw FormatError("Kernel restore requires an outer dispatch boundary");
    if (sync.kernel_ != &kernel || interrupts.kernel_ != &kernel) throw FormatError("Kernel snapshot: mismatched destination bindings");
    validate(kernel);
    // From here on every operation is non-allocating and non-throwing. No guest
    // code or restored continuation runs until the whole set has been installed.
    sync.semaphores_.swap(data_->semaphores);
    sync.event_flags_.swap(data_->events);
    interrupts.handlers_.swap(data_->handlers);
    kernel.apply_state(data_->scheduler);
    data_.reset();
}
} // namespace sarusaru::save_state
