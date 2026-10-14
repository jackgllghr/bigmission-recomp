#pragma once

#include "psprecomp/runtime.hpp"
#include <cstdint>
#include <optional>

namespace sarusaru::game {
// The supported executable's native frame interval. This changes game update
// cadence; fixed-step game logic is not compensated, so overrides are experimental.
class FrameRateOverride {
public:
    explicit FrameRateOverride(std::uint32_t fps = 0);
    [[nodiscard]] bool enabled() const noexcept { return interval_ != 0; }
    [[nodiscard]] std::uint32_t interval_vblanks() const noexcept { return interval_; }
    [[nodiscard]] std::uint64_t overridden_frames() const noexcept { return overridden_frames_; }
    // Called after sceDisplayGetVcount has returned its unmodified PSP result.
    void on_vcount(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &context);
    [[nodiscard]] std::optional<std::uint64_t> wait_deadline(psprecomp::Runtime &runtime,
        const psprecomp::AllegrexContext &context, std::uint64_t now_us) const;
private:
    std::uint32_t interval_{};
    std::uint64_t overridden_frames_{};
    std::uint32_t waiting_object_{};
    std::uint32_t previous_vcount_{};
    std::uint32_t sampled_vcount_{};
};
} // namespace sarusaru::game
