#pragma once

#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru {

// Narrow startup-only HLE module. It owns only values reported by the game's
// SDK/compiler configuration imports; it does not pretend to be a PSP kernel.
class StartupHle {
public:
    void install(psprecomp::Runtime &runtime);

    [[nodiscard]] std::uint32_t compiled_sdk_version() const noexcept { return compiled_sdk_version_; }
    [[nodiscard]] std::uint32_t compiler_version() const noexcept { return compiler_version_; }

private:
    std::uint32_t compiled_sdk_version_{};
    std::uint32_t compiler_version_{};
};

} // namespace sarusaru
