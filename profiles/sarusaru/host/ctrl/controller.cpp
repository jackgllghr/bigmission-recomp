#include "ctrl/controller.hpp"

namespace sarusaru::ctrl {

std::int32_t Controller::set_sampling_mode(std::uint32_t mode) noexcept {
    if (mode > 1u) return static_cast<std::int32_t>(kInvalidMode);
    const auto previous = mode_;
    mode_ = mode;
    return static_cast<std::int32_t>(previous);
}

std::int32_t Controller::set_sampling_cycle(std::uint32_t cycle) noexcept {
    // 0 samples on vblank; otherwise the period is in microseconds.
    if (cycle != 0u && (cycle < 5555u || cycle > 20000u)) return static_cast<std::int32_t>(kInvalidValue);
    const auto previous = cycle_;
    cycle_ = cycle;
    return static_cast<std::int32_t>(previous);
}

std::int32_t Controller::set_idle_cancel_threshold(std::int32_t idle_reset, std::int32_t idle_back) noexcept {
    if (idle_reset < -1 || idle_reset > 128 || idle_back < -1 || idle_back > 128)
        return static_cast<std::int32_t>(kInvalidValue);
    idle_reset_ = idle_reset;
    idle_back_ = idle_back;
    return 0;
}

void register_ctrl(Controller &controller, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceCtrl", 0x1F4011E6u,  // sceCtrlSetSamplingMode
        [&controller](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(controller.set_sampling_mode(ctx.gpr[4])));
        });
    runtime.register_hle("sceCtrl", 0x6A2774F3u,  // sceCtrlSetSamplingCycle
        [&controller](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(controller.set_sampling_cycle(ctx.gpr[4])));
        });
    runtime.register_hle("sceCtrl", 0xA7144800u,  // sceCtrlSetIdleCancelThreshold
        [&controller](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(controller.set_idle_cancel_threshold(
                static_cast<std::int32_t>(ctx.gpr[4]), static_cast<std::int32_t>(ctx.gpr[5]))));
        });
}

} // namespace sarusaru::ctrl
