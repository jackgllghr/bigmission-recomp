#pragma once
#include "save_state/session_access.hpp"

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <optional>

namespace sarusaru::display {

inline constexpr std::uint32_t kInvalidPointer = 0x80000103u;
inline constexpr std::uint32_t kInvalidSize = 0x80000104u;
inline constexpr std::uint32_t kInvalidMode = 0x80000107u;
inline constexpr std::uint32_t kInvalidFormat = 0x80000109u;

// Lines per frame and the measured length of the vertical blanking interval.
inline constexpr std::uint64_t kLinesPerFrame = 286u;
inline constexpr std::uint64_t kVblankDurationUs = 731u;

// sceDisplay state on the kernel's virtual clock. A framebuffer set with
// sync=1 is latched at the next vblank. Nothing is presented yet.
class Display {
    friend struct sarusaru::save_state::SessionCodec;
public:
    struct Framebuffer {
        std::uint32_t address{};
        std::uint32_t buffer_width{};
        std::uint32_t pixel_format{};
    };

    [[nodiscard]] std::int32_t set_mode(std::uint32_t mode, std::uint32_t width, std::uint32_t height) noexcept;
    [[nodiscard]] std::int32_t set_framebuffer(const Framebuffer &framebuffer, std::uint32_t sync,
                                               std::uint64_t now_us, const psprecomp::GuestMemory &memory);
    // The framebuffer being scanned out (sync 1) or the most recently requested one (sync 0).
    [[nodiscard]] Framebuffer framebuffer(std::uint32_t sync, std::uint64_t now_us);
    // Count accepted framebuffer requests (each one ends a game frame) and
    // framebuffers scanned out (each one a picture the player sees change).
    [[nodiscard]] std::uint64_t framebuffer_requests() const noexcept { return requests_; }
    [[nodiscard]] std::uint64_t framebuffer_latches(std::uint64_t now_us) noexcept {
        apply_pending(now_us);
        return latches_;
    }

    [[nodiscard]] static std::uint32_t vcount(std::uint64_t now_us) noexcept;
    [[nodiscard]] static bool in_vblank(std::uint64_t now_us) noexcept;
    [[nodiscard]] static std::uint32_t accumulated_hcount(std::uint64_t now_us) noexcept;
    [[nodiscard]] static std::uint64_t next_vblank_start_us(std::uint64_t now_us) noexcept;

    [[nodiscard]] std::uint32_t mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    std::uint32_t mode_{};
    std::uint32_t width_{480u};
    std::uint32_t height_{272u};
    Framebuffer latched_;
    Framebuffer requested_;
    std::optional<std::uint64_t> latch_at_vblank_;
    std::uint64_t requests_{};
    std::uint64_t latches_{};

    void apply_pending(std::uint64_t now_us) noexcept;
};

// Optional profile hook after GetVcount returns; the PSP vcount itself remains native.
using VcountHook = std::function<void(psprecomp::Runtime &, psprecomp::AllegrexContext &)>;
using VblankWaitHook = std::function<std::optional<std::uint64_t>(psprecomp::Runtime &,
    const psprecomp::AllegrexContext &, std::uint64_t now_us)>;
void register_display(Display &display, kernel::EarlyKernel &kernel, psprecomp::Runtime &runtime,
                      VcountHook vcount_hook = {}, VblankWaitHook vblank_wait_hook = {});

} // namespace sarusaru::display
