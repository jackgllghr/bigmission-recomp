#include "kernel/early_kernel.hpp"

#include "kernel/guest_clock.hpp"
#include "psprecomp/common.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>

namespace sarusaru::kernel {
namespace {
constexpr std::uint32_t kMinimumStackSize = 0x200u;
constexpr std::uint32_t kStackAlignment = 0x100u;
constexpr std::uint32_t kArgumentHome = 0x40u;
constexpr std::uint32_t kThreadControlBlockSize = 0x100u;

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment = kStackAlignment) noexcept {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

void thread_exit_without_scheduler(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &) {
    runtime.stop("A guest thread exited before the early kernel was installed");
}

EarlyKernel *active_kernel = nullptr;
void thread_exit(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &context) {
    if (active_kernel == nullptr) {
        thread_exit_without_scheduler(runtime, context);
        return;
    }
    active_kernel->exit_current_thread(context, context.gpr[2]);
}

void guest_call_return(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &context) {
    if (active_kernel == nullptr) {
        runtime.stop("A guest call returned before the early kernel was installed");
        return;
    }
    active_kernel->guest_call_returned(context);
}

constexpr std::uint32_t kInterruptStackSize = 0x4000u;
constexpr std::uint32_t kMaxIdleVblanks = 600u;
}

void EarlyKernel::install(psprecomp::Runtime &runtime, std::uint32_t user_memory_start, std::uint32_t user_memory_top) {
    runtime_ = &runtime;
    const auto start = align_up(user_memory_start);
    next_stack_top_ = user_memory_top & ~(kStackAlignment - 1u);
    if (start >= next_stack_top_ || !runtime.memory().contains(start, next_stack_top_ - start))
        throw psprecomp::Error("Invalid early-kernel user memory range");
    threads_.clear();
    blocks_.clear();
    callbacks_.clear();
    exit_callback_uid_ = 0;
    power_callback_slots_.fill(0);
    umd_callback_uid_ = 0;
    interrupts_enabled_ = true;
    dispatch_enabled_ = true;
    free_ranges_ = {{start, next_stack_top_ - start}};
    next_uid_ = 0x100;
    current_uid_ = 0;
    next_ready_sequence_ = 1u;
    now_us_ = 0u;
    interrupt_uid_ = 0;
    interrupt_calls_.clear();
    vblank_handler_ = nullptr;
    vblank_active_ = false;
    active_kernel = this;
    runtime.register_function(kThreadExitStub, &thread_exit, "sarusaru_thread_exit");
    runtime.register_function(kGuestCallReturnStub, &guest_call_return, "sarusaru_guest_call_return");
}

void EarlyKernel::begin_main_thread(psprecomp::AllegrexContext &context, std::string name,
                                    std::uint32_t priority) {
    const ThreadUid uid = next_uid_++;
    ThreadInfo main{uid, std::move(name), context.pc, priority, 0u, 0u, 0u, context.gpr[28], 0u, context,
                    ThreadState::Running, 0u};
    threads_.emplace(uid, std::move(main));
    current_uid_ = uid;
    psprecomp::set_runtime_thread_identity(uid, threads_.at(uid).name);
}

void EarlyKernel::make_ready(ThreadInfo &thread) noexcept {
    thread.state = ThreadState::Ready;
    thread.ready_sequence = next_ready_sequence_++;
}

EarlyKernel::ThreadInfo *EarlyKernel::best_ready_thread() noexcept {
    ThreadInfo *best = nullptr;
    for (auto &[uid, candidate] : threads_) {
        (void)uid;
        if (candidate.state != ThreadState::Ready) continue;
        if (best == nullptr || candidate.priority < best->priority ||
            (candidate.priority == best->priority && candidate.ready_sequence < best->ready_sequence)) {
            best = &candidate;
        }
    }
    return best;
}

void EarlyKernel::switch_to(psprecomp::AllegrexContext &context, ThreadUid uid, ThreadInfo &thread) {
    // SARUSARU_TRACE_SCHED=1 logs every switch to stderr for diagnosis.
    static const bool trace = std::getenv("SARUSARU_TRACE_SCHED") != nullptr;
    if (trace) {
        std::cerr << "[sched " << now_us_ << "us] -> " << thread.name << " (" << uid << ") pc="
                  << psprecomp::hex32(thread.context.pc) << " gp=" << psprecomp::hex32(thread.context.gpr[28])
                  << " sp=" << psprecomp::hex32(thread.context.gpr[29]) << '\n';
    }
    thread.state = ThreadState::Running;
    current_uid_ = uid;
    context = thread.context;
    psprecomp::set_runtime_thread_identity(uid, thread.name);
}

void EarlyKernel::save_blocked_context(ThreadInfo &thread, const psprecomp::AllegrexContext &context,
                                       std::uint32_t result) noexcept {
    // Inside an import the PC is still the import stub; resume after the call.
    thread.context = context;
    thread.context.pc = context.gpr[31];
    thread.context.set_gpr(2u, result);
}

void EarlyKernel::wake_due_threads() {
    std::vector<ThreadInfo *> due;
    for (auto &[uid, thread] : threads_) {
        (void)uid;
        if (thread.state == ThreadState::Waiting && thread.wake_at_us <= now_us_) due.push_back(&thread);
    }
    std::ranges::sort(due, [](const ThreadInfo *left, const ThreadInfo *right) {
        return left->wake_at_us != right->wake_at_us ? left->wake_at_us < right->wake_at_us
                                                     : left->wait_sequence < right->wait_sequence;
    });
    for (ThreadInfo *thread : due) {
        if (thread->on_wake) thread->context.set_gpr(2u, thread->on_wake());
        thread->on_wake = nullptr;
        thread->timeout_address = 0u;
        thread->end_of = 0;
        thread->wait_label.clear();
        make_ready(*thread);
    }
}

void EarlyKernel::run_next_ready(psprecomp::AllegrexContext &context, const char *idle_reason) {
    wake_due_threads();
    ThreadInfo *next = best_ready_thread();
    // Idle: advance virtual time to the next thread deadline or vblank event.
    // Vblanks alone cannot keep an otherwise stuck guest alive forever.
    std::uint32_t idle_vblanks = 0u;
    while (next == nullptr && !runtime_->stopped()) {
        std::optional<std::uint64_t> earliest;
        for (const auto &[uid, thread] : threads_) {
            (void)uid;
            if (thread.state == ThreadState::Waiting && thread.wake_at_us != kNoDeadline &&
                (!earliest || thread.wake_at_us < *earliest))
                earliest = thread.wake_at_us;
        }
        const bool vblanks = vblank_active_ && vblank_handler_ && idle_vblanks < kMaxIdleVblanks;
        const std::uint64_t vblank = vblank_time_us(vblanks_by(now_us_) + 1u);
        if (!earliest && !vblanks) break;
        if (vblanks && (!earliest || vblank <= *earliest)) {
            now_us_ = vblank;
            ++idle_vblanks;
            if (interrupts_enabled_) vblank_handler_();
        } else {
            now_us_ = std::max(now_us_, *earliest);
        }
        wake_due_threads();
        next = best_ready_thread();
    }
    if (next != nullptr) {
        switch_to(context, next->uid, *next);
        return;
    }
    current_uid_ = 0;
    psprecomp::set_runtime_thread_identity(-1, "idle");
    if (!runtime_->stopped()) runtime_->stop(std::string(idle_reason) + describe_blocked());
}

void EarlyKernel::queue_interrupt(GuestCall call) {
    if (interrupt_uid_ == 0) {
        const auto block = allocate_block("interrupt stack", 1u, kInterruptStackSize, 0u);
        if (block < 0) {
            runtime_->stop("Cannot allocate the interrupt stack");
            return;
        }
        interrupt_uid_ = next_uid_++;
        ThreadInfo thread{interrupt_uid_, "interrupt", 0u, 0u, block_head_address(block), kInterruptStackSize};
        thread.stack_block = block;
        threads_.emplace(interrupt_uid_, std::move(thread));
    }
    interrupt_calls_.push_back(std::move(call));
    ThreadInfo &thread = threads_.at(interrupt_uid_);
    if (thread.state == ThreadState::Dormant) {
        start_interrupt_call(thread);
        make_ready(thread);
    }
}

void EarlyKernel::start_interrupt_call(ThreadInfo &thread) {
    const GuestCall &call = interrupt_calls_.front();
    thread.context = psprecomp::AllegrexContext{};
    thread.context.eat_vfpu_prefixes();
    for (std::uint32_t i = 0; i < 4u; ++i) thread.context.set_gpr(4u + i, call.arguments[i]);
    thread.context.set_gpr(28u, call.gp);
    thread.context.set_gpr(29u, thread.stack_bottom + thread.stack_size - kArgumentHome);
    thread.context.set_gpr(31u, kGuestCallReturnStub);
    thread.context.pc = call.address;
    thread.wait_label = call.label;
}

void EarlyKernel::guest_call_returned(psprecomp::AllegrexContext &context) {
    if (!in_interrupt()) {
        const auto current = threads_.find(current_uid_);
        if (current == threads_.end() || !current->second.callback_saved || current->second.callback_queue.empty()) {
            runtime_->stop("Guest call return outside a kernel-initiated call");
            return;
        }
        ThreadInfo &thread = current->second;
        // A nonzero return deletes the callback.
        if (context.gpr[2] != 0u) (void)delete_callback(thread.callback_queue.front());
        thread.callback_queue.pop_front();
        if (!thread.callback_queue.empty()) {
            start_next_callback(thread);
            context = thread.context;
            return;
        }
        context = *thread.callback_saved;
        thread.callback_saved.reset();
        const auto after = std::move(thread.after_callbacks);
        thread.after_callbacks = nullptr;
        after(context);
        return;
    }
    if (interrupt_calls_.empty()) {
        runtime_->stop("Guest call return outside a kernel-initiated call");
        return;
    }
    const auto on_return = std::move(interrupt_calls_.front().on_return);
    interrupt_calls_.pop_front();
    if (on_return) on_return(context.gpr[2]);
    ThreadInfo &thread = threads_.at(interrupt_uid_);
    if (!interrupt_calls_.empty()) {
        start_interrupt_call(thread);
        context = thread.context;
        return;
    }
    thread.state = ThreadState::Dormant;
    thread.wait_label.clear();
    run_next_ready(context, "Every early-kernel guest thread has ended or is blocked with no deadline");
}

bool EarlyKernel::has_pending_callbacks(ThreadUid owner) const noexcept {
    return std::ranges::any_of(callbacks_, [owner](const auto &entry) {
        return entry.second.owner == owner && entry.second.notify_count != 0u;
    });
}

void EarlyKernel::start_callbacks(ThreadInfo &thread, const psprecomp::AllegrexContext &saved,
                                  std::function<void(psprecomp::AllegrexContext &)> after) {
    thread.callback_saved = saved;
    thread.after_callbacks = std::move(after);
    thread.callback_queue.clear();
    for (const auto &[uid, callback] : callbacks_) {
        if (callback.owner == thread.uid && callback.notify_count != 0u) thread.callback_queue.push_back(uid);
    }
    start_next_callback(thread);
}

void EarlyKernel::start_next_callback(ThreadInfo &thread) {
    CallbackInfo &callback = callbacks_.at(thread.callback_queue.front());
    // callback(notify count, notify argument, common), run below the interrupted frame.
    thread.context = *thread.callback_saved;
    thread.context.set_gpr(4u, callback.notify_count);
    thread.context.set_gpr(5u, callback.notify_argument);
    thread.context.set_gpr(6u, callback.common);
    thread.context.set_gpr(29u, (thread.callback_saved->gpr[29] - 0x100u) & ~0xFu);
    thread.context.set_gpr(31u, kGuestCallReturnStub);
    thread.context.pc = callback.function;
    callback.notify_count = 0u;
    callback.notify_argument = 0u;
}

void EarlyKernel::notify_callback(std::int32_t uid, std::uint32_t argument) {
    const auto found = callbacks_.find(uid);
    if (found == callbacks_.end()) return;
    ++found->second.notify_count;
    found->second.notify_argument = argument;
    const auto owner = threads_.find(found->second.owner);
    if (owner == threads_.end()) return;
    ThreadInfo &thread = owner->second;
    std::function<void(psprecomp::AllegrexContext &)> resume;
    if (thread.state == ThreadState::Waiting && thread.resume_wait) {
        resume = [this, request = *thread.resume_wait](psprecomp::AllegrexContext &context) {
            block_current(context, request);
        };
    } else if (thread.state == ThreadState::Sleeping && thread.resume_sleep) {
        resume = [this](psprecomp::AllegrexContext &context) { sleep_current_thread(context, true); };
    } else {
        return;
    }
    // Interrupt the *CB wait: run the callbacks, then wait again as before.
    thread.resume_wait.reset();
    thread.resume_sleep = false;
    thread.on_wake = nullptr;
    thread.timeout_address = 0u;
    thread.end_of = 0;
    thread.wait_label.clear();
    start_callbacks(thread, thread.context, std::move(resume));
    make_ready(thread);
}

void EarlyKernel::check_callbacks(psprecomp::AllegrexContext &context) {
    const auto current = threads_.find(current_uid_);
    if (in_interrupt() || current == threads_.end() || !has_pending_callbacks(current_uid_)) {
        context.set_gpr(2u, 0u);
        return;
    }
    psprecomp::AllegrexContext saved = context;
    saved.pc = context.gpr[31];
    start_callbacks(current->second, saved, [](psprecomp::AllegrexContext &resumed) { resumed.set_gpr(2u, 1u); });
    context = current->second.context;
}

std::int32_t EarlyKernel::delete_callback(std::int32_t uid) {
    if (callbacks_.erase(uid) == 0u) return static_cast<std::int32_t>(kUnknownCallback);
    if (exit_callback_uid_ == uid) exit_callback_uid_ = 0;
    if (umd_callback_uid_ == uid) umd_callback_uid_ = 0;
    for (auto &slot : power_callback_slots_) if (slot == uid) slot = 0;
    return 0;
}

std::int32_t EarlyKernel::suspend_dispatch() noexcept {
    if (!interrupts_enabled_) return static_cast<std::int32_t>(kInterruptsDisabled);
    const std::int32_t previous = dispatch_enabled_ ? 1 : 0;
    dispatch_enabled_ = false;
    return previous;
}

void EarlyKernel::resume_dispatch(psprecomp::AllegrexContext &context, std::uint32_t state) {
    if (!interrupts_enabled_) {
        context.set_gpr(2u, kInterruptsDisabled);
        return;
    }
    dispatch_enabled_ = state != 0u;
    return_with_preemption(context, 0u);
}

bool EarlyKernel::refuse_wait_in_interrupt(psprecomp::AllegrexContext &context) {
    if (!in_interrupt() && dispatch_enabled_) return false;
    context.set_gpr(2u, kCanNotWait);
    return true;
}

std::string EarlyKernel::describe_blocked() const {
    std::string result;
    for (const auto &[uid, thread] : threads_) {
        if (thread.state != ThreadState::Sleeping && thread.state != ThreadState::Waiting) continue;
        result += result.empty() ? " [blocked: " : ", ";
        result += thread.name + " (" + std::to_string(uid) + ") " +
                  (thread.state == ThreadState::Sleeping ? std::string("sleeping") : thread.wait_label);
    }
    return result.empty() ? result : result + "]";
}

void EarlyKernel::prepare_thread_stack(ThreadInfo &thread) {
    const std::uint32_t control_block = thread.stack_bottom + thread.stack_size - kThreadControlBlockSize;
    for (std::uint32_t offset = 0u; offset < kThreadControlBlockSize; offset += 4u)
        runtime_->memory().store32(control_block + offset, 0u);
    runtime_->memory().store32(control_block + 0xC0u, static_cast<std::uint32_t>(thread.uid));
    runtime_->memory().store32(control_block + 0xC8u, thread.stack_bottom);
    runtime_->memory().store32(control_block + 0xF8u, 0xFFFFFFFFu);
    runtime_->memory().store32(control_block + 0xFCu, 0xFFFFFFFFu);
    runtime_->memory().store32(thread.stack_bottom, static_cast<std::uint32_t>(thread.uid));
    thread.control_block = control_block;
}

ThreadUid EarlyKernel::create_thread(const std::string &name, std::uint32_t entry,
                                     std::uint32_t priority, std::uint32_t stack_size,
                                     std::uint32_t attributes, std::uint32_t gp) {
    if (runtime_ == nullptr) return static_cast<ThreadUid>(kNoMemory);
    if (entry == 0u || (entry & 3u) != 0u) return static_cast<ThreadUid>(kIllegalEntry);
    if (priority == 0u || priority > 0x7Fu) return static_cast<ThreadUid>(kIllegalPriority);
    if (stack_size < kMinimumStackSize || stack_size > std::numeric_limits<std::uint32_t>::max() - (kStackAlignment - 1u))
        return static_cast<ThreadUid>(kIllegalStackSize);
    const std::uint32_t aligned_size = align_up(stack_size);
    const auto block = allocate_block("stack:" + name, 1u, aligned_size, 0u);
    if (block < 0) return block;
    const std::uint32_t stack_bottom = block_head_address(block);
    const ThreadUid uid = next_uid_++;
    auto [it, inserted] = threads_.emplace(uid, ThreadInfo{uid, name, entry, priority, stack_bottom, aligned_size, attributes, gp});
    (void)inserted;
    it->second.stack_block = block;
    prepare_thread_stack(it->second);
    return uid;
}

std::int32_t EarlyKernel::allocate_partition_memory(const std::string &name, std::uint32_t type,
                                                    std::uint32_t size, std::uint32_t address_or_alignment) {
    return allocate_block(name, type, size, address_or_alignment);
}

std::int32_t EarlyKernel::allocate_block(const std::string &name, std::uint32_t type,
                                         std::uint32_t size, std::uint32_t address_or_alignment) {
    if (size == 0u || size > std::numeric_limits<std::uint32_t>::max() - (kStackAlignment - 1u))
        return static_cast<std::int32_t>(kIllegalArgument);
    const std::uint32_t aligned_size = align_up(size);
    std::uint32_t alignment = kStackAlignment;
    if ((type == 3u || type == 4u) && address_or_alignment != 0u) {
        if ((address_or_alignment & (address_or_alignment - 1u)) != 0u) return static_cast<std::int32_t>(kIllegalArgument);
        alignment = std::max(alignment, address_or_alignment);
    }
    std::optional<std::uint32_t> address;
    if (type == 0u || type == 3u) {
        for (const auto &range : free_ranges_) {
            const auto candidate = align_up(range.address, alignment);
            if (candidate >= range.address && candidate - range.address <= range.size && aligned_size <= range.size - (candidate - range.address)) {
                address = candidate; break;
            }
        }
    } else if (type == 1u || type == 4u) {
        for (auto it = free_ranges_.rbegin(); it != free_ranges_.rend(); ++it) {
            const std::uint64_t end = static_cast<std::uint64_t>(it->address) + it->size;
            if (end < aligned_size) continue;
            const auto candidate = static_cast<std::uint32_t>(end - aligned_size) & ~(alignment - 1u);
            if (candidate >= it->address) { address = candidate; break; }
        }
    } else {
        return static_cast<std::int32_t>(kIllegalMemblockType);
    }
    if (!address) return static_cast<std::int32_t>(kNoMemory);
    for (auto it = free_ranges_.begin(); it != free_ranges_.end(); ++it) {
        if (*address < it->address || *address - it->address > it->size || aligned_size > it->size - (*address - it->address)) continue;
        const FreeRange before{it->address, *address - it->address};
        const FreeRange after{*address + aligned_size, it->size - before.size - aligned_size};
        it = free_ranges_.erase(it);
        if (after.size != 0u) it = free_ranges_.insert(it, after);
        if (before.size != 0u) free_ranges_.insert(it, before);
        break;
    }
    const auto uid = next_uid_++;
    blocks_.emplace(uid, MemoryBlock{name, *address, aligned_size});
    return uid;
}

std::int32_t EarlyKernel::free_partition_memory(std::int32_t uid) {
    const auto found = blocks_.find(uid);
    if (found == blocks_.end()) return static_cast<std::int32_t>(kIllegalMemblock);
    const FreeRange released{found->second.address, found->second.size};
    blocks_.erase(found);
    auto it = std::lower_bound(free_ranges_.begin(), free_ranges_.end(), released,
        [](const FreeRange &left, const FreeRange &right) { return left.address < right.address; });
    it = free_ranges_.insert(it, released);
    if (it != free_ranges_.begin() && (it - 1)->address + (it - 1)->size == it->address) {
        (it - 1)->size += it->size;
        it = free_ranges_.erase(it) - 1;
    }
    if (it + 1 != free_ranges_.end() && it->address + it->size == (it + 1)->address) {
        it->size += (it + 1)->size;
        free_ranges_.erase(it + 1);
    }
    return 0;
}

std::uint32_t EarlyKernel::block_head_address(std::int32_t uid) const noexcept {
    const auto found = blocks_.find(uid);
    return found == blocks_.end() ? 0u : found->second.address;
}

std::int32_t EarlyKernel::create_callback(const std::string &name, std::uint32_t function, std::uint32_t common) {
    if (function == 0u || (function & 3u) != 0u || current_uid_ == 0) return static_cast<std::int32_t>(kIllegalArgument);
    const auto uid = next_uid_++;
    callbacks_.emplace(uid, CallbackInfo{name, function, common, current_uid_});
    return uid;
}

const EarlyKernel::CallbackInfo *EarlyKernel::callback(std::int32_t uid) const noexcept {
    const auto found = callbacks_.find(uid);
    return found == callbacks_.end() ? nullptr : &found->second;
}

std::int32_t EarlyKernel::register_exit_callback(std::int32_t uid) {
    const auto found = callbacks_.find(uid);
    if (found == callbacks_.end()) return static_cast<std::int32_t>(kIllegalArgument);
    if (exit_callback_uid_ != 0) {
        const auto old = callbacks_.find(exit_callback_uid_);
        if (old != callbacks_.end()) old->second.registered_as_exit = false;
    }
    found->second.registered_as_exit = true;
    exit_callback_uid_ = uid;
    return 0;
}

std::int32_t EarlyKernel::register_power_callback(std::int32_t slot, std::int32_t callback_uid) {
    if (!callbacks_.contains(callback_uid)) return static_cast<std::int32_t>(kPowerInvalidCallback);
    if (slot == -1) {
        for (std::size_t index = 0; index < power_callback_slots_.size(); ++index) {
            if (power_callback_slots_[index] == 0) {
                power_callback_slots_[index] = callback_uid;
                return static_cast<std::int32_t>(index);
            }
        }
        return static_cast<std::int32_t>(kPowerSlotsFull);
    }
    if (slot < 0 || static_cast<std::size_t>(slot) >= power_callback_slots_.size())
        return static_cast<std::int32_t>(kPowerInvalidSlot);
    if (power_callback_slots_[static_cast<std::size_t>(slot)] != 0) return static_cast<std::int32_t>(kPowerSlotsFull);
    power_callback_slots_[static_cast<std::size_t>(slot)] = callback_uid;
    return 0;
}

std::int32_t EarlyKernel::register_umd_callback(std::int32_t callback_uid) {
    if (!callbacks_.contains(callback_uid)) return static_cast<std::int32_t>(kIllegalArgument);
    umd_callback_uid_ = callback_uid;
    return 0;
}

std::uint32_t EarlyKernel::suspend_interrupts() noexcept {
    const std::uint32_t previous = interrupts_enabled_ ? 1u : 0u;
    interrupts_enabled_ = false;
    return previous;
}

std::int32_t EarlyKernel::start_thread(psprecomp::AllegrexContext &context, ThreadUid uid,
                                       std::uint32_t argument_size, std::uint32_t argument_address) {
    const auto found = threads_.find(uid);
    if (found == threads_.end()) return static_cast<std::int32_t>(kIllegalEntry);
    ThreadInfo &thread = found->second;
    if (thread.state != ThreadState::Dormant) return static_cast<std::int32_t>(kNotDormant);
    if (argument_size != 0u && (argument_address == 0u || !runtime_->memory().contains(argument_address, argument_size)))
        return static_cast<std::int32_t>(kIllegalEntry);

    std::uint32_t stack_pointer = thread.control_block;
    if (argument_size != 0u) {
        const std::uint32_t copy_size = (argument_size + 15u) & ~15u;
        if (copy_size < argument_size || stack_pointer < thread.stack_bottom + copy_size) return static_cast<std::int32_t>(kNoMemory);
        stack_pointer -= copy_size;
        for (std::uint32_t offset = 0u; offset < argument_size; ++offset)
            runtime_->memory().store8(stack_pointer + offset, runtime_->memory().load8(argument_address + offset));
    }
    thread.context = psprecomp::AllegrexContext{};
    thread.context.eat_vfpu_prefixes();
    thread.context.set_gpr(4u, argument_size);
    thread.context.set_gpr(5u, argument_size == 0u ? 0u : stack_pointer);
    thread.context.set_gpr(28u, thread.gp);
    thread.context.set_gpr(26u, thread.control_block);
    thread.context.set_gpr(29u, stack_pointer - kArgumentHome);
    thread.context.set_gpr(31u, kThreadExitStub);
    thread.context.pc = thread.entry;
    make_ready(thread);
    const auto current = threads_.find(current_uid_);
    if (dispatch_enabled_ && current != threads_.end() && thread.priority < current->second.priority) {
        save_blocked_context(current->second, context, 0u);
        make_ready(current->second);
        switch_to(context, uid, thread);
    } else {
        context.set_gpr(2u, 0u);
    }
    return 0;
}

void EarlyKernel::end_thread(ThreadInfo &thread, std::uint32_t status) {
    thread.state = ThreadState::Dormant;
    thread.exit_status = status;
    thread.on_wake = nullptr;
    for (auto &[uid, waiter] : threads_) {
        if (waiter.state == ThreadState::Waiting && waiter.end_of == thread.uid) release_wait(uid, status);
    }
}

void EarlyKernel::exit_current_thread(psprecomp::AllegrexContext &context, std::uint32_t status, bool delete_thread) {
    const auto current = threads_.find(current_uid_);
    if (current == threads_.end()) {
        runtime_->stop("Thread exit with no current early-kernel thread");
        return;
    }
    const ThreadUid exiting = current_uid_;
    end_thread(current->second, status);
    run_next_ready(context, "Every early-kernel guest thread has ended or is blocked with no deadline");
    if (delete_thread) {
        const auto block = threads_.at(exiting).stack_block;
        if (block != 0) (void)free_partition_memory(block);
        threads_.erase(exiting);
    }
}

std::int32_t EarlyKernel::delete_thread(ThreadUid uid) {
    const auto found = threads_.find(uid);
    if (found == threads_.end()) return static_cast<std::int32_t>(kUnknownThread);
    if (found->second.state != ThreadState::Dormant) return static_cast<std::int32_t>(kNotDormant);
    if (found->second.stack_block != 0) (void)free_partition_memory(found->second.stack_block);
    threads_.erase(found);
    return 0;
}

std::int32_t EarlyKernel::terminate_thread(psprecomp::AllegrexContext &context, ThreadUid uid) {
    if (uid == 0 || uid == current_uid_) return static_cast<std::int32_t>(kIllegalThread);
    const auto found = threads_.find(uid);
    if (found == threads_.end()) return static_cast<std::int32_t>(kUnknownThread);
    if (found->second.state == ThreadState::Dormant) return static_cast<std::int32_t>(kDormant);
    end_thread(found->second, kThreadTerminated);
    return_with_preemption(context, 0u);
    return 0;
}

std::int32_t EarlyKernel::thread_exit_status(ThreadUid uid) const noexcept {
    const auto found = threads_.find(uid);
    if (found == threads_.end()) return static_cast<std::int32_t>(kUnknownThread);
    if (found->second.state != ThreadState::Dormant) return static_cast<std::int32_t>(kNotDormant);
    return static_cast<std::int32_t>(found->second.exit_status);
}

void EarlyKernel::wait_thread_end(psprecomp::AllegrexContext &context, ThreadUid uid, std::uint32_t timeout_address) {
    const auto found = threads_.find(uid);
    if (found == threads_.end()) { context.set_gpr(2u, kUnknownThread); return; }
    if (uid == current_uid_) { context.set_gpr(2u, kIllegalThread); return; }
    if (found->second.state == ThreadState::Dormant) { context.set_gpr(2u, found->second.exit_status); return; }
    auto request = object_wait("end of thread " + found->second.name, timeout_address);
    request.end_of = uid;
    block_current(context, std::move(request));
}

std::int32_t EarlyKernel::change_current_thread_attributes(std::uint32_t clear, std::uint32_t set) noexcept {
    if (((clear | set) & ~kThreadAttrVfpu) != 0u) return static_cast<std::int32_t>(kIllegalAttribute);
    const auto current = threads_.find(current_uid_);
    if (current == threads_.end()) return static_cast<std::int32_t>(kIllegalThread);
    current->second.attributes = (current->second.attributes & ~clear) | set;
    return 0;
}

EarlyKernel::WaitRequest EarlyKernel::object_wait(std::string label, std::uint32_t timeout_address,
                                                  std::function<void()> on_timeout) const {
    WaitRequest request;
    request.label = std::move(label);
    if (timeout_address != 0u) {
        request.timeout_address = timeout_address;
        request.deadline_us = now_us_ + runtime_->memory().load32(timeout_address);
        request.on_timeout = [this, timeout_address, cleanup = std::move(on_timeout)] {
            runtime_->memory().store32(timeout_address, 0u);
            if (cleanup) cleanup();
            return kWaitTimeout;
        };
    }
    return request;
}

void EarlyKernel::block_current(psprecomp::AllegrexContext &context, WaitRequest request) {
    if (refuse_wait_in_interrupt(context)) return;
    const auto current = threads_.find(current_uid_);
    if (current == threads_.end()) {
        runtime_->stop("Wait with no current early-kernel thread");
        return;
    }
    ThreadInfo &thread = current->second;
    save_blocked_context(thread, context, 0u);
    if (request.callbacks && has_pending_callbacks(current_uid_)) {
        start_callbacks(thread, thread.context, [this, request](psprecomp::AllegrexContext &resumed) {
            block_current(resumed, request);
        });
        context = thread.context;
        return;
    }
    thread.resume_wait.reset();
    if (request.callbacks) thread.resume_wait = request;
    thread.state = ThreadState::Waiting;
    thread.wake_at_us = request.deadline_us;
    thread.wait_sequence = next_ready_sequence_++;
    thread.on_wake = std::move(request.on_timeout);
    thread.timeout_address = request.timeout_address;
    thread.end_of = request.end_of;
    thread.wait_label = std::move(request.label);
    run_next_ready(context, "Every early-kernel guest thread has ended or is blocked with no deadline");
}

bool EarlyKernel::release_wait(ThreadUid uid, std::uint32_t result) {
    const auto found = threads_.find(uid);
    if (found == threads_.end() || found->second.state != ThreadState::Waiting) return false;
    ThreadInfo &thread = found->second;
    if (thread.timeout_address != 0u) {
        const auto left = thread.wake_at_us > now_us_ ? thread.wake_at_us - now_us_ : 0u;
        runtime_->memory().store32(thread.timeout_address, static_cast<std::uint32_t>(left));
    }
    thread.context.set_gpr(2u, result);
    thread.on_wake = nullptr;
    thread.timeout_address = 0u;
    thread.end_of = 0;
    thread.wait_label.clear();
    make_ready(thread);
    return true;
}

void EarlyKernel::return_with_preemption(psprecomp::AllegrexContext &context, std::uint32_t result) {
    const auto current = threads_.find(current_uid_);
    ThreadInfo *next = best_ready_thread();
    if (dispatch_enabled_ && current != threads_.end() && next != nullptr && next->priority < current->second.priority) {
        save_blocked_context(current->second, context, result);
        make_ready(current->second);
        switch_to(context, next->uid, *next);
        return;
    }
    context.set_gpr(2u, result);
}

void EarlyKernel::sleep_current_thread(psprecomp::AllegrexContext &context, bool callbacks) {
    if (refuse_wait_in_interrupt(context)) return;
    const auto current = threads_.find(current_uid_);
    if (current == threads_.end()) {
        runtime_->stop("Thread sleep with no current early-kernel thread");
        return;
    }
    ThreadInfo &thread = current->second;
    if (thread.wakeup_count != 0u) {
        --thread.wakeup_count;
        context.set_gpr(2u, 0u);
        return;
    }
    save_blocked_context(thread, context, 0u);
    if (callbacks && has_pending_callbacks(current_uid_)) {
        start_callbacks(thread, thread.context, [this](psprecomp::AllegrexContext &resumed) {
            sleep_current_thread(resumed, true);
        });
        context = thread.context;
        return;
    }
    thread.resume_sleep = callbacks;
    thread.state = ThreadState::Sleeping;
    run_next_ready(context, "Every early-kernel guest thread has ended or is blocked with no deadline");
}

void EarlyKernel::wait_current_until(psprecomp::AllegrexContext &context, std::uint64_t deadline_us,
                                     WakeHandler on_wake) {
    block_current(context, WaitRequest{"timed wait", deadline_us, 0u, std::move(on_wake), 0});
}

void EarlyKernel::delay_current_thread(psprecomp::AllegrexContext &context, std::uint32_t micros, bool callbacks) {
    // A zero delay still yields to every other ready thread.
    block_current(context, WaitRequest{"delay", now_us_ + micros, 0u, nullptr, 0, callbacks});
}

std::int32_t EarlyKernel::wakeup_thread(psprecomp::AllegrexContext &context, ThreadUid uid) {
    if (uid == 0) uid = current_uid_;
    const auto found = threads_.find(uid);
    if (found == threads_.end()) return static_cast<std::int32_t>(kUnknownThread);
    ThreadInfo &thread = found->second;
    if (thread.state == ThreadState::Dormant) return static_cast<std::int32_t>(kDormant);
    if (thread.state != ThreadState::Sleeping) {
        ++thread.wakeup_count;
        return 0;
    }
    make_ready(thread);
    const auto current = threads_.find(current_uid_);
    if (dispatch_enabled_ && current != threads_.end() && thread.priority < current->second.priority) {
        save_blocked_context(current->second, context, 0u);
        make_ready(current->second);
        switch_to(context, uid, thread);
    }
    return 0;
}

const EarlyKernel::ThreadInfo *EarlyKernel::thread(ThreadUid uid) const noexcept {
    const auto found = threads_.find(uid);
    return found == threads_.end() ? nullptr : &found->second;
}

void register_early_threadman(EarlyKernel &kernel, psprecomp::Runtime &runtime) {
    runtime.register_hle("ThreadManForUser", 0x446D8DE6u,
        [&kernel](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::string name;
            try {
                name = rt.memory().read_c_string(ctx.gpr[4], 32u);
            } catch (const psprecomp::Error &) {
                ctx.set_gpr(2u, kIllegalEntry);
                return;
            }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.create_thread(
                name, ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[8], ctx.gpr[28])));
        });
    runtime.register_hle("ThreadManForUser", 0xF475845Du,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto caller = kernel.current_uid();
            const auto result = kernel.start_thread(ctx, static_cast<ThreadUid>(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6]);
            if (kernel.current_uid() == caller) ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
        });
    runtime.register_hle("SysMemUserForUser", 0x237DBD4Fu,
        [&kernel](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::string name;
            try { name = rt.memory().read_c_string(ctx.gpr[5], 32u); }
            catch (const psprecomp::Error &) { ctx.set_gpr(2u, kIllegalArgument); return; }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.allocate_partition_memory(
                name, ctx.gpr[6], ctx.gpr[7], ctx.gpr[8])));
        });
    runtime.register_hle("SysMemUserForUser", 0x9D9A5BA1u,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, kernel.block_head_address(static_cast<std::int32_t>(ctx.gpr[4])));
        });
    runtime.register_hle("SysMemUserForUser", 0xB6D61D02u,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.free_partition_memory(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("ThreadManForUser", 0xE81CAF8Fu,
        [&kernel](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::string name;
            try { name = rt.memory().read_c_string(ctx.gpr[4], 32u); }
            catch (const psprecomp::Error &) { ctx.set_gpr(2u, kIllegalArgument); return; }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.create_callback(name, ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("LoadExecForUser", 0x4AC57943u,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.register_exit_callback(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("scePower", 0x04B7766Eu,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.register_power_callback(
                static_cast<std::int32_t>(ctx.gpr[4]), static_cast<std::int32_t>(ctx.gpr[5]))));
        });
    runtime.register_hle("ThreadManForUser", 0x9ACE131Eu,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.sleep_current_thread(ctx); });
    runtime.register_hle("ThreadManForUser", 0xAA73C935u,  // sceKernelExitThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.exit_current_thread(ctx, ctx.gpr[4]); });
    runtime.register_hle("ThreadManForUser", 0x809CE29Bu,  // sceKernelExitDeleteThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.exit_current_thread(ctx, ctx.gpr[4], true); });
    runtime.register_hle("ThreadManForUser", 0x9FA03CD3u,  // sceKernelDeleteThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto uid = static_cast<ThreadUid>(ctx.gpr[4]);
            ctx.set_gpr(2u, static_cast<std::uint32_t>(uid == 0 || uid == kernel.current_uid()
                ? static_cast<std::int32_t>(kIllegalThread) : kernel.delete_thread(uid)));
        });
    runtime.register_hle("ThreadManForUser", 0x616403BAu,  // sceKernelTerminateThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto result = kernel.terminate_thread(ctx, static_cast<ThreadUid>(ctx.gpr[4]));
            if (result != 0) ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
        });
    runtime.register_hle("ThreadManForUser", 0x3B183E26u,  // sceKernelGetThreadExitStatus
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.thread_exit_status(static_cast<ThreadUid>(ctx.gpr[4]))));
        });
    runtime.register_hle("ThreadManForUser", 0x278C0DF5u,  // sceKernelWaitThreadEnd
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            kernel.wait_thread_end(ctx, static_cast<ThreadUid>(ctx.gpr[4]), ctx.gpr[5]);
        });
    runtime.register_hle("ThreadManForUser", 0x293B45B8u,  // sceKernelGetThreadId
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.current_uid()));
        });
    runtime.register_hle("ThreadManForUser", 0x94AA61EEu,  // sceKernelGetThreadCurrentPriority
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto *current = kernel.thread(kernel.current_uid());
            ctx.set_gpr(2u, current != nullptr ? current->priority : kIllegalThread);
        });
    runtime.register_hle("ThreadManForUser", 0xEA748E31u,  // sceKernelChangeCurrentThreadAttr
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.change_current_thread_attributes(ctx.gpr[4], ctx.gpr[5])));
        });
    runtime.register_hle("ThreadManForUser", 0x369ED59Du,  // sceKernelGetSystemTimeLow
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.now_us()));
        });
    runtime.register_hle("ThreadManForUser", 0x82BC5777u,  // sceKernelGetSystemTimeWide
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.now_us()));
            ctx.set_gpr(3u, static_cast<std::uint32_t>(kernel.now_us() >> 32u));
        });
    runtime.register_hle("ThreadManForUser", 0x3AD58B8Cu,  // sceKernelSuspendDispatchThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.suspend_dispatch()));
        });
    runtime.register_hle("ThreadManForUser", 0x27E22EC2u,  // sceKernelResumeDispatchThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.resume_dispatch(ctx, ctx.gpr[4]); });
    runtime.register_hle("ThreadManForUser", 0xCEADEB47u,  // sceKernelDelayThread
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.delay_current_thread(ctx, ctx.gpr[4]); });
    runtime.register_hle("ThreadManForUser", 0x68DA9E36u,  // sceKernelDelayThreadCB
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.delay_current_thread(ctx, ctx.gpr[4], true); });
    runtime.register_hle("ThreadManForUser", 0x349D6D6Cu,  // sceKernelCheckCallback
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { kernel.check_callbacks(ctx); });
    runtime.register_hle("ThreadManForUser", 0xEDBA5844u,  // sceKernelDeleteCallback
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.delete_callback(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("ThreadManForUser", 0xD59EAD2Fu,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto caller = kernel.current_uid();
            const auto result = kernel.wakeup_thread(ctx, static_cast<ThreadUid>(ctx.gpr[4]));
            if (kernel.current_uid() == caller) ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
        });
    runtime.register_hle("sceUmdUser", 0xAEE7404Du,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(kernel.register_umd_callback(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("Kernel_Library", 0x092968F4u,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, kernel.suspend_interrupts());
        });
    runtime.register_hle("Kernel_Library", 0x5F10D406u,
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            kernel.resume_interrupts(ctx.gpr[4] != 0u);
            ctx.set_gpr(2u, 0u);
        });
}

} // namespace sarusaru::kernel
