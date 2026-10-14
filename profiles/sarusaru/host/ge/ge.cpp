#include "ge/ge.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>

namespace sarusaru::ge {
namespace {
constexpr std::uint32_t kAddressMask = 0x0FFFFFFFu;
constexpr std::uint64_t kCommandBudget = 1u << 24u;

enum Command : std::uint8_t {
    kNop = 0x00, kPrim = 0x04, kBezier = 0x05, kSpline = 0x06, kJump = 0x08, kBoundingJump = 0x09,
    kCall = 0x0A, kReturn = 0x0B, kEnd = 0x0C, kSignal = 0x0E, kFinish = 0x0F, kBase = 0x10,
    kOffsetAddress = 0x13, kOrigin = 0x14,
};
constexpr std::uint32_t kSignalHandlerSuspend = 0x01u;
constexpr std::uint32_t kSignalHandlerContinue = 0x02u;
}

Ge::Ge(kernel::EarlyKernel &kernel) : kernel_(&kernel) {
    state_.set_transfer_sink([this](const gpu::BlockTransfer &transfer) {
        if (!copy_transfer(kernel_->runtime().memory(), transfer))
            kernel_->runtime().stop("GE block transfer outside guest memory");
    });
}

bool Ge::copy_transfer(psprecomp::GuestMemory &memory, const gpu::BlockTransfer &transfer) {
    const std::uint32_t row_bytes = transfer.width * transfer.bytes_per_pixel;
    std::vector<std::uint8_t> row(row_bytes);
    for (std::uint32_t y = 0; y < transfer.height; ++y) {
        const std::uint32_t source = transfer.source +
            ((transfer.source_y + y) * transfer.source_stride + transfer.source_x) * transfer.bytes_per_pixel;
        const std::uint32_t destination = transfer.destination +
            ((transfer.destination_y + y) * transfer.destination_stride + transfer.destination_x) * transfer.bytes_per_pixel;
        if (!memory.contains(source, row_bytes) || !memory.contains(destination, row_bytes)) return false;
        memory.copy_out(source, row);
        memory.copy_in(destination, row);
    }
    return true;
}

std::int32_t Ge::set_callback(std::uint32_t data_address, std::uint32_t gp) {
    auto &memory = kernel_->runtime().memory();
    if ((data_address & 3u) != 0u || !memory.contains(data_address, 16u)) return static_cast<std::int32_t>(kInvalidPointer);
    for (std::uint32_t id = 0; id < kMaxCallbacks; ++id) {
        if (callbacks_[id]) continue;
        callbacks_[id] = Callback{memory.load32(data_address), memory.load32(data_address + 4u),
                                  memory.load32(data_address + 8u), memory.load32(data_address + 12u), gp};
        return static_cast<std::int32_t>(id);
    }
    return static_cast<std::int32_t>(kOutOfMemory);
}

std::int32_t Ge::unset_callback(std::uint32_t id) {
    if (id >= kMaxCallbacks || !callbacks_[id]) return static_cast<std::int32_t>(kInvalidId);
    callbacks_[id].reset();
    return 0;
}

std::optional<std::uint32_t> Ge::slot(std::uint32_t id) const noexcept {
    const std::uint32_t index = id ^ kListIdMagic;
    if (index >= kMaxLists || !lists_[index].used) return std::nullopt;
    return index;
}

std::int32_t Ge::enqueue(std::uint32_t list, std::uint32_t stall, std::int32_t callback, std::uint32_t argument,
                         bool head) {
    (void)argument;  // PspGeListArgs (context save, stack sets) is not modeled yet.
    if ((list & 3u) != 0u || !kernel_->runtime().memory().contains(list & kAddressMask, 4u))
        return static_cast<std::int32_t>(kInvalidPointer);
    const auto free = std::ranges::find_if(lists_, [](const List &candidate) { return !candidate.in_use; });
    if (free == lists_.end()) return static_cast<std::int32_t>(kOutOfMemory);
    const auto index = static_cast<std::uint32_t>(free - lists_.begin());
    *free = List{};
    free->used = true;
    free->in_use = true;
    free->pc = list & kAddressMask;
    free->stall = stall & kAddressMask;
    if (callback >= 0 && static_cast<std::uint32_t>(callback) < kMaxCallbacks && callbacks_[callback]) free->callback = callback;
    // A list already being processed keeps the engine; a head list runs next.
    if (head && !queue_.empty() && lists_[queue_.front()].state != ListState::Queued)
        queue_.insert(queue_.begin() + 1, index);
    else if (head)
        queue_.push_front(index);
    else
        queue_.push_back(index);
    run();
    return static_cast<std::int32_t>(index ^ kListIdMagic);
}

std::int32_t Ge::update_stall(std::uint32_t id, std::uint32_t stall) {
    const auto index = slot(id);
    if (!index || !lists_[*index].in_use) return static_cast<std::int32_t>(kInvalidId);
    lists_[*index].stall = stall & kAddressMask;
    run();
    return 0;
}

void Ge::run() {
    while (!queue_.empty() && !kernel_->runtime().stopped()) {
        const std::uint32_t index = queue_.front();
        if (lists_[index].state == ListState::Paused || !execute(index)) return;
    }
}

std::uint32_t Ge::target(std::uint32_t data, std::uint32_t offset) const noexcept {
    return ((base_ | (data & 0x00FFFFFCu)) + offset) & kAddressMask;
}

bool Ge::execute(std::uint32_t index) {
    auto &runtime = kernel_->runtime();
    auto &memory = runtime.memory();
    List &list = lists_[index];
    list.state = ListState::Drawing;
    const auto fail = [&](const std::string &reason) {
        runtime.stop("GE list at " + psprecomp::hex32(list.pc) + ": " + reason);
        return false;
    };
    for (std::uint64_t budget = 0; budget < kCommandBudget; ++budget) {
        if (list.stall != 0u && list.pc == list.stall) {
            list.state = ListState::Stalling;
            return false;
        }
        if (!memory.contains(list.pc, 4u)) return fail("read outside guest memory");
        const std::uint32_t word = memory.load32(list.pc);
        const auto command = static_cast<std::uint8_t>(word >> 24u);
        const std::uint32_t data = word & 0x00FFFFFFu;
        const std::uint32_t address = list.pc;
        list.pc += 4u;
        ++stats_.commands;
        switch (command) {
        case kNop:
        case kBoundingJump:
            break;
        case kPrim:
        case kBezier:
        case kSpline:
            ++stats_.primitives;
            registers_[command] = data;
            state_.process(memory, address, word);
            break;
        case kJump:
            list.pc = target(data, list.offset);
            break;
        case kCall:
            if (list.stack.size() >= kCallStackDepth) return fail("call stack overflow");
            list.stack.push_back({list.pc, list.offset});
            list.pc = target(data, list.offset);
            break;
        case kReturn:
            if (list.stack.empty()) return fail("return with an empty call stack");
            list.pc = list.stack.back().return_address;
            list.offset = list.stack.back().offset;
            list.stack.pop_back();
            break;
        case kBase:
            base_ = (data & 0x000F0000u) << 8u;
            registers_[command] = data;
            state_.process(memory, address, word);
            break;
        case kOffsetAddress:
            list.offset = data << 8u;
            registers_[command] = data;
            state_.process(memory, address, word);
            break;
        case kOrigin:
            list.offset = address;
            state_.process(memory, address, word);
            break;
        case kSignal:
        case kFinish: {
            // Both take effect at the END that must follow them.
            if (!memory.contains(list.pc, 4u) || (memory.load32(list.pc) >> 24u) != kEnd)
                return fail(command == kFinish ? "FINISH not followed by END" : "SIGNAL not followed by END");
            list.pc += 4u;
            ++stats_.commands;
            const std::uint32_t value = data & 0xFFFFu;
            const Callback *callback = list.callback >= 0 && callbacks_[list.callback] ? &*callbacks_[list.callback] : nullptr;
            if (command == kFinish) {
                if (callback != nullptr)
                    raise(callback->finish_function, callback->finish_argument, callback->gp, value, address, "GE finish");
                complete(index);
                return true;
            }
            const std::uint32_t behaviour = (data >> 16u) & 0xFFu;
            if (behaviour != kSignalHandlerSuspend && behaviour != kSignalHandlerContinue)
                return fail("unsupported SIGNAL behaviour " + psprecomp::hex32(behaviour));
            if (callback == nullptr || callback->signal_function == 0u) break;
            if (behaviour == kSignalHandlerContinue) {
                raise(callback->signal_function, callback->signal_argument, callback->gp, value, address, "GE signal");
                break;
            }
            // Suspend: the list waits for the handler to return.
            list.state = ListState::Paused;
            raise(callback->signal_function, callback->signal_argument, callback->gp, value, address, "GE signal",
                  [this, index](std::uint32_t) {
                      lists_[index].state = ListState::Drawing;
                      run();
                  });
            return false;
        }
        case kEnd:
            return fail("END without a preceding FINISH or SIGNAL");
        default:
            registers_[command] = data;
            state_.process(memory, address, word);
            break;
        }
    }
    return fail("command budget exhausted; the list probably loops");
}

void Ge::complete(std::uint32_t index) {
    List &list = lists_[index];
    list.state = ListState::Completed;
    list.in_use = false;
    ++stats_.lists_completed;
    queue_.erase(std::ranges::find(queue_, index));
    for (const auto waiter : list.waiters) kernel_->release_wait(waiter, 0u);
    list.waiters.clear();
    if (queue_.empty()) {
        for (const auto waiter : draw_waiters_) kernel_->release_wait(waiter, 0u);
        draw_waiters_.clear();
    }
}

void Ge::raise(std::uint32_t function, std::uint32_t argument, std::uint32_t gp, std::uint32_t value,
               std::uint32_t pc, std::string label, std::function<void(std::uint32_t)> on_return) {
    if (function == 0u) {
        if (on_return) on_return(0u);
        return;
    }
    kernel_->queue_interrupt({function, {value, argument, pc, 0u}, gp, std::move(label), std::move(on_return)});
}

std::optional<ListState> Ge::list_state(std::uint32_t id) const noexcept {
    const auto index = slot(id);
    if (!index) return std::nullopt;
    return lists_[*index].state;
}

ListState Ge::queue_state() const noexcept {
    if (queue_.empty()) return ListState::Completed;
    const ListState front = lists_[queue_.front()].state;
    return front == ListState::Stalling || front == ListState::Paused ? front : ListState::Drawing;
}

void Ge::list_sync(psprecomp::AllegrexContext &context, std::uint32_t id, std::uint32_t mode) {
    if (mode > 1u) { context.set_gpr(2u, kInvalidMode); return; }
    const auto index = slot(id);
    if (!index) { context.set_gpr(2u, kInvalidId); return; }
    List &list = lists_[*index];
    if (mode == 1u) { context.set_gpr(2u, static_cast<std::uint32_t>(list.state)); return; }
    if (list.state == ListState::Completed) { context.set_gpr(2u, 0u); return; }
    list.waiters.push_back(kernel_->current_uid());
    kernel_->block_current(context, kernel_->object_wait("GE list sync", 0u));
}

void Ge::draw_sync(psprecomp::AllegrexContext &context, std::uint32_t mode) {
    if (mode > 1u) { context.set_gpr(2u, kInvalidMode); return; }
    if (mode == 1u) { context.set_gpr(2u, static_cast<std::uint32_t>(queue_state())); return; }
    if (queue_.empty()) { context.set_gpr(2u, 0u); return; }
    draw_waiters_.push_back(kernel_->current_uid());
    kernel_->block_current(context, kernel_->object_wait("GE draw sync", 0u));
}

void register_ge(Ge &ge, kernel::EarlyKernel &kernel, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceGe_user", 0xE47E40E4u,  // sceGeEdramGetAddr
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, kEdramAddress); });
    runtime.register_hle("sceGe_user", 0xA4FC06A4u,  // sceGeSetCallback
        [&ge](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(ge.set_callback(ctx.gpr[4], ctx.gpr[28])));
        });
    runtime.register_hle("sceGe_user", 0x05DB22CEu,  // sceGeUnsetCallback
        [&ge](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(ge.unset_callback(ctx.gpr[4])));
        });
    const auto enqueue = [&ge, &kernel](bool head) {
        return [&ge, &kernel, head](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto result = ge.enqueue(ctx.gpr[4], ctx.gpr[5], static_cast<std::int32_t>(ctx.gpr[6]), ctx.gpr[7], head);
            kernel.return_with_preemption(ctx, static_cast<std::uint32_t>(result));
        };
    };
    runtime.register_hle("sceGe_user", 0xAB49E76Au, enqueue(false));  // sceGeListEnQueue
    runtime.register_hle("sceGe_user", 0x1C0D95A6u, enqueue(true));   // sceGeListEnQueueHead
    runtime.register_hle("sceGe_user", 0xE0D68148u,  // sceGeListUpdateStallAddr
        [&ge, &kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            kernel.return_with_preemption(ctx, static_cast<std::uint32_t>(ge.update_stall(ctx.gpr[4], ctx.gpr[5])));
        });
    runtime.register_hle("sceGe_user", 0x03444EB4u,  // sceGeListSync
        [&ge](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ge.list_sync(ctx, ctx.gpr[4], ctx.gpr[5]); });
    runtime.register_hle("sceGe_user", 0xB287BD61u,  // sceGeDrawSync
        [&ge](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ge.draw_sync(ctx, ctx.gpr[4]); });
}

} // namespace sarusaru::ge
