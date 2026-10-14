#pragma once

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru::ctrl {

inline constexpr std::uint32_t kInvalidMode = 0x80000107u;
inline constexpr std::uint32_t kInvalidValue = 0x800001FEu;
inline constexpr std::uint32_t kInvalidSize = 0x80000104u;
inline constexpr std::uint32_t kInvalidPointer = 0x80000103u;
inline constexpr std::uint32_t kMaxBufferedSamples = 64u;
inline constexpr std::uint32_t kSampleSize = 16u;

// sceCtrl state on the kernel's virtual clock. The pad is sampled at each
// vblank (sampling cycle 0) or every `cycle` microseconds. A read returns the
// samples taken since the previous read, and blocks until the next sample if
// there are none. Until host input exists the pad stays neutral.
class Controller {
public:
    struct Pad {
        std::uint32_t buttons{};
        std::uint8_t lx{0x80u};
        std::uint8_t ly{0x80u};
    };
    void set_pad(const Pad &pad) noexcept { pad_ = pad; }

    // Sample index taken at or before time_us, and when sample `index` is taken.
    [[nodiscard]] std::uint64_t sample_index(std::uint64_t time_us) const noexcept;
    [[nodiscard]] std::uint64_t sample_time_us(std::uint64_t index) const noexcept;
    // Writes up to `count` samples ending at the latest; returns how many.
    [[nodiscard]] std::uint32_t write_samples(psprecomp::GuestMemory &memory, std::uint32_t address,
                                              std::uint32_t count, std::uint64_t now_us);

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
    Pad pad_;
    std::uint64_t last_read_sample_{};
};

void register_ctrl(Controller &controller, kernel::EarlyKernel &kernel, psprecomp::Runtime &runtime);

} // namespace sarusaru::ctrl
