#pragma once

#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru {

// Narrow startup HLE module. It owns values the game reports through its
// SDK/compiler configuration and GPO (debug LED) imports, counts power ticks
// (there are no idle/backlight timers to reset), and binds data-cache
// writeback calls, which have no effect on coherent host memory. It does not
// pretend to be a PSP kernel.
class StartupHle {
public:
    void install(psprecomp::Runtime &runtime);

    [[nodiscard]] std::uint32_t compiled_sdk_version() const noexcept { return compiled_sdk_version_; }
    [[nodiscard]] std::uint32_t compiler_version() const noexcept { return compiler_version_; }
    [[nodiscard]] std::uint32_t gpo() const noexcept { return gpo_; }
    [[nodiscard]] std::uint32_t power_ticks() const noexcept { return power_ticks_; }

private:
    std::uint32_t compiled_sdk_version_{};
    std::uint32_t compiler_version_{};
    std::uint32_t gpo_{};
    std::uint32_t power_ticks_{};
};

} // namespace sarusaru
