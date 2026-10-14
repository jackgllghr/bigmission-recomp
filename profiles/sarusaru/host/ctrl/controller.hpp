#pragma once

#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru::ctrl {

inline constexpr std::uint32_t kInvalidMode = 0x80000107u;
inline constexpr std::uint32_t kInvalidValue = 0x800001FEu;

// sceCtrl configuration state. Sampling itself is tied to vblank on hardware
// and is not modeled until display timing exists, so reads stay unbound.
class Controller {
public:
    [[nodiscard]] std::int32_t set_sampling_mode(std::uint32_t mode) noexcept;
    [[nodiscard]] std::int32_t set_sampling_cycle(std::uint32_t cycle) noexcept;
    [[nodiscard]] std::int32_t set_idle_cancel_threshold(std::int32_t idle_reset, std::int32_t idle_back) noexcept;

    [[nodiscard]] std::uint32_t sampling_mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint32_t sampling_cycle() const noexcept { return cycle_; }
    [[nodiscard]] std::int32_t idle_reset() const noexcept { return idle_reset_; }
    [[nodiscard]] std::int32_t idle_back() const noexcept { return idle_back_; }

private:
    std::uint32_t mode_{};
    std::uint32_t cycle_{};
    std::int32_t idle_reset_{-1};
    std::int32_t idle_back_{-1};
};

void register_ctrl(Controller &controller, psprecomp::Runtime &runtime);

} // namespace sarusaru::ctrl
