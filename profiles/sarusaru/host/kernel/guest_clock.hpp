#pragma once

#include <cstdint>

namespace sarusaru::kernel {

// Vblank n (n >= 1) starts at n * 1001000 / 60 microseconds: 60000/1001 Hz.
[[nodiscard]] constexpr std::uint64_t vblank_time_us(std::uint64_t n) noexcept {
    return n * 1001000u / 60u;
}

// Number of vblanks that have started at or before time_us.
[[nodiscard]] constexpr std::uint64_t vblanks_by(std::uint64_t time_us) noexcept {
    return (60u * (time_us + 1u) - 1u) / 1001000u;
}

static_assert(vblanks_by(0u) == 0u && vblank_time_us(1u) == 16683u);
static_assert(vblanks_by(16682u) == 0u && vblanks_by(16683u) == 1u && vblanks_by(vblank_time_us(60u)) == 60u);

} // namespace sarusaru::kernel
