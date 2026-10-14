#include "kernel/early_kernel.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace sarusaru::kernel {
namespace {
constexpr std::uint32_t kMinimumStackSize = 0x200u;
constexpr std::uint32_t kStackAlignment = 0x100u;
constexpr std::uint32_t kArgumentHome = 0x40u;
constexpr std::uint32_t kThreadControlBlockSize = 0x100u;
constexpr std::uint32_t kNotDormant = 0x800201A4u;

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
    active_kernel->exit_current_thread(context);
}
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
    interrupts_enabled_ = true;
    free_ranges_ = {{start, next_stack_top_ - start}};
    next_uid_ = 0x100;
    current_uid_ = 0;
    next_ready_sequence_ = 1u;
    active_kernel = this;
    runtime.register_function(kThreadExitStub, &thread_exit, "sarusaru_thread_exit");
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
    thread.state = ThreadState::Running;
    current_uid_ = uid;
    context = thread.context;
    psprecomp::set_runtime_thread_identity(uid, thread.name);
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
    if (current != threads_.end() && thread.priority < current->second.priority) {
        current->second.context = context;
        make_ready(current->second);
        switch_to(context, uid, thread);
    } else {
        context.set_gpr(2u, 0u);
    }
    return 0;
}

void EarlyKernel::exit_current_thread(psprecomp::AllegrexContext &context) {
    const auto current = threads_.find(current_uid_);
    if (current == threads_.end()) {
        runtime_->stop("Thread exit with no current early-kernel thread");
        return;
    }
    current->second.state = ThreadState::Exited;
    if (ThreadInfo *next = best_ready_thread(); next != nullptr) {
        for (auto &[uid, candidate] : threads_) {
            if (&candidate == next) {
                switch_to(context, uid, candidate);
                return;
            }
        }
    }
    current_uid_ = 0;
    psprecomp::set_runtime_thread_identity(-1, "idle");
    runtime_->stop("All early-kernel guest threads exited; waits and scheduler idle are not implemented");
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
            const auto result = kernel.start_thread(ctx, static_cast<ThreadUid>(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6]);
            if (result != 0 || ctx.pc == ctx.gpr[31]) ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
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
