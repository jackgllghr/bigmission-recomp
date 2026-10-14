#include "game/frame_rate.hpp"
#include "psprecomp/common.hpp"
#include "kernel/guest_clock.hpp"
#include <algorithm>

namespace sarusaru::game {
namespace {
// These are specific to the SHA-256-verified UCJS10060 executable loaded by
// GameSession. Use the import boundary rather than an AOT function replacement:
// same-unit calls in the generated corpus can jump directly to local labels.
constexpr std::uint32_t kFrameWaitVcountReturn = 0x088FEB1Cu;
constexpr std::uint32_t kFirstVblankWaitReturn = 0x088FEB40u;
constexpr std::uint32_t kSecondVblankWaitReturn = 0x088FEB80u;
constexpr std::uint32_t kFrameObjectPointer = 0x08A82B24u;
constexpr std::uint32_t kFrameObjectSize = 92u;
constexpr std::uint32_t kIntervalOffset = 52u;
}
FrameRateOverride::FrameRateOverride(std::uint32_t fps) {
    switch (fps) {
    case 0: break;
    case 15: case 20: case 30: case 60: interval_ = 60u / fps; break;
    default: throw psprecomp::Error("Game frame rate must be 0 (native), 15, 20, 30 or 60");
    }
}
void FrameRateOverride::on_vcount(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &context) {
    if (!enabled() || context.gpr[31] != kFrameWaitVcountReturn) return;
    auto &memory = runtime.memory();
    const auto object = context.gpr[16];
    if ((object & 3u) != 0 || !memory.contains(object, kFrameObjectSize) ||
        !memory.contains(kFrameObjectPointer, 4u) || memory.load32(kFrameObjectPointer) != object ||
        memory.load32(object + kIntervalOffset) > 3u) {
        runtime.stop("Game frame-rate override encountered an unexpected frame object");
        return;
    }
    // The wait routine loads this immediately after GetVcount returns. Leave
    // saved vcount, buffer index, CPU registers, and all PSP clocks untouched.
    memory.store32(object + kIntervalOffset, interval_ - 1u);
    waiting_object_ = object;
    previous_vcount_ = memory.load32(object + 48u);
    sampled_vcount_ = context.gpr[2];
    ++overridden_frames_;
}
std::optional<std::uint64_t> FrameRateOverride::wait_deadline(psprecomp::Runtime &runtime,
    const psprecomp::AllegrexContext &context, std::uint64_t now_us) const {
    if (!enabled() || (context.gpr[31] != kFirstVblankWaitReturn && context.gpr[31] != kSecondVblankWaitReturn))
        return std::nullopt;
    if (waiting_object_ == 0 || context.gpr[16] != waiting_object_) {
        runtime.stop("Game frame-rate override encountered an unexpected vblank wait");
        return std::nullopt;
    }
    // The original routine has only two wait calls, not an N-vblank loop. A
    // deadline makes 15/20 FPS genuinely wait four/three vblanks. Both calls
    // share the same deadline, so the second cannot add another interval.
    const auto now_count = kernel::vblanks_by(now_us);
    const auto since_sample = static_cast<std::uint32_t>(now_count) - sampled_vcount_;
    const auto sample_count = now_count - since_sample;
    const auto elapsed = previous_vcount_ == 0xFFFFFFFFu ? 0u : sampled_vcount_ - previous_vcount_;
    if (elapsed >= interval_) return now_us;
    return std::max(now_us, kernel::vblank_time_us(sample_count + interval_ - elapsed));
}
} // namespace sarusaru::game
