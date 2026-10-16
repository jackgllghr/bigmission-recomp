#include "kernel/interrupts.hpp"

#include <string>

namespace sarusaru::kernel {
namespace {
bool valid(std::uint32_t interrupt, std::uint32_t sub) noexcept {
    return interrupt < 67u && sub < kSubInterruptCount;
}
}

InterruptManager::InterruptManager(EarlyKernel &kernel) : kernel_(&kernel) {
    kernel.set_vblank_handler([this] { raise(kInterruptVblank); });
}

std::int32_t InterruptManager::register_handler(std::uint32_t interrupt, std::uint32_t sub, std::uint32_t handler,
                                                std::uint32_t argument, std::uint32_t gp) {
    if (!valid(interrupt, sub)) return static_cast<std::int32_t>(kIllegalInterruptCode);
    if (handlers_.contains({interrupt, sub})) return static_cast<std::int32_t>(kHandlerAlreadyRegistered);
    handlers_.emplace(std::pair{interrupt, sub}, Handler{handler, argument, gp, false});
    return 0;
}

std::int32_t InterruptManager::release_handler(std::uint32_t interrupt, std::uint32_t sub) {
    if (!valid(interrupt, sub)) return static_cast<std::int32_t>(kIllegalInterruptCode);
    if (handlers_.erase({interrupt, sub}) == 0u) return static_cast<std::int32_t>(kHandlerNotFound);
    update_vblank_activity();
    return 0;
}

std::int32_t InterruptManager::enable(std::uint32_t interrupt, std::uint32_t sub) {
    if (!valid(interrupt, sub)) return static_cast<std::int32_t>(kIllegalInterruptCode);
    const auto found = handlers_.find({interrupt, sub});
    if (found == handlers_.end()) return static_cast<std::int32_t>(kHandlerNotFound);
    found->second.enabled = true;
    update_vblank_activity();
    return 0;
}

std::int32_t InterruptManager::disable(std::uint32_t interrupt, std::uint32_t sub) {
    if (!valid(interrupt, sub)) return static_cast<std::int32_t>(kIllegalInterruptCode);
    const auto found = handlers_.find({interrupt, sub});
    if (found == handlers_.end()) return static_cast<std::int32_t>(kHandlerNotFound);
    found->second.enabled = false;
    update_vblank_activity();
    return 0;
}

std::size_t InterruptManager::raise(std::uint32_t interrupt) {
    std::size_t queued = 0u;
    for (const auto &[key, handler] : handlers_) {
        if (key.first != interrupt || !handler.enabled || handler.address == 0u) continue;
        kernel_->queue_interrupt({handler.address, {key.second, handler.argument, 0u, 0u}, handler.gp,
                                  "interrupt " + std::to_string(interrupt) + "." + std::to_string(key.second), {}});
        ++queued;
    }
    return queued;
}

void InterruptManager::update_vblank_activity() noexcept {
    bool active = false;
    for (const auto &[key, handler] : handlers_) active = active || (key.first == kInterruptVblank && handler.enabled);
    kernel_->set_vblank_active(active);
}

void register_interrupts(InterruptManager &interrupts, psprecomp::Runtime &runtime) {
    runtime.register_hle("InterruptManager", 0xCA04A2B9u,  // sceKernelRegisterSubIntrHandler
        [&interrupts](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(interrupts.register_handler(
                ctx.gpr[4], ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[28])));
        });
    runtime.register_hle("InterruptManager", 0xD61E6961u,  // sceKernelReleaseSubIntrHandler
        [&interrupts](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(interrupts.release_handler(ctx.gpr[4], ctx.gpr[5])));
        });
    runtime.register_hle("InterruptManager", 0xFB8E22ECu,  // sceKernelEnableSubIntr
        [&interrupts](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(interrupts.enable(ctx.gpr[4], ctx.gpr[5])));
        });
}

} // namespace sarusaru::kernel
