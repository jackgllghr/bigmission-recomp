#include "game/draw_distance.hpp"

#include <bit>
#include <cmath>

namespace sarusaru::game {
namespace {
constexpr std::uint32_t kSystemPointer = 0x08A82B24u;
constexpr std::uint32_t kEye = 112u;
// The actor's world matrix: translation at +80, its w (1) at +92.
constexpr std::uint32_t kPosition = 80u, kPositionW = 92u;
// A call this far below the loop's stack is on another thread's stack.
constexpr std::uint32_t kStackWindow = 0x40000u;

std::array<std::uint32_t, 3> load3(psprecomp::GuestMemory &memory, std::uint32_t address) {
    return {memory.load32(address), memory.load32(address + 4u), memory.load32(address + 8u)};
}
void store3(psprecomp::GuestMemory &memory, std::uint32_t address, const std::array<std::uint32_t, 3> &value) {
    for (unsigned i = 0; i < 3u; ++i) memory.store32(address + i * 4u, value[i]);
}
} // namespace

void DrawDistance::set_scale(float scale) noexcept {
    scale_ = scale >= 1.0f && std::isfinite(scale) ? scale : 1.0f;
}

void DrawDistance::restore(psprecomp::GuestMemory &memory) noexcept {
    // Only a value written here is put back; one the game wrote since stands.
    if (active_ && load3(memory, camera_ + kEye) == written_) store3(memory, camera_ + kEye, real_);
    active_ = false;
}

void DrawDistance::on_call(std::uint32_t pc, psprecomp::GuestMemory &memory, const psprecomp::AllegrexContext &ctx) {
    const auto sp = ctx.gpr[29];
    // The loop returned (a call from its caller's frame) or another thread runs.
    if (loop_sp_ != 0 && (sp >= loop_sp_ || loop_sp_ - sp > kStackWindow)) {
        restore(memory);
        loop_sp_ = 0;
    }
    if (pc == kUpdateLoop) {
        restore(memory);
        loop_sp_ = 0;
        const auto system = memory.load32(kSystemPointer);
        if (!enabled() || !memory.contains(system, 24u)) return;
        const auto camera = memory.load32(system + 20u);
        if ((camera & 3u) != 0 || !memory.contains(camera + kEye, 12u)) return;
        camera_ = camera;
        loop_sp_ = sp;
        return;
    }
    const auto source = ctx.gpr[31];
    if (loop_sp_ == 0 || sp >= loop_sp_ || (source != kUpdateReturnFirst && source != kUpdateReturnNext)) return;

    // The eye as the game has it: the real one unless it still holds an actor's.
    if (!active_ || load3(memory, camera_ + kEye) != written_) {
        real_ = load3(memory, camera_ + kEye);
        active_ = false;
    }
    const auto actor = ctx.gpr[4];
    if ((actor & 3u) != 0 || !memory.contains(actor + kPosition, 16u) ||
        std::bit_cast<float>(memory.load32(actor + kPositionW)) != 1.0f) {
        restore(memory);
        return;
    }
    std::array<float, 3> eye{}, offset{};
    float distance_squared = 0.0f;
    for (unsigned i = 0; i < 3u; ++i) {
        eye[i] = std::bit_cast<float>(real_[i]);
        offset[i] = eye[i] - std::bit_cast<float>(memory.load32(actor + kPosition + i * 4u));
        distance_squared += offset[i] * offset[i];
    }
    const float distance = std::sqrt(distance_squared);
    if (!std::isfinite(distance) || distance <= kKeepDistance) {
        restore(memory);
        return;
    }
    // Slide the eye towards the actor: the excess over kKeepDistance shrinks by the scale.
    const float pull = (distance - kKeepDistance) * (1.0f - 1.0f / scale_) / distance;
    for (unsigned i = 0; i < 3u; ++i) written_[i] = std::bit_cast<std::uint32_t>(eye[i] - offset[i] * pull);
    store3(memory, camera_ + kEye, written_);
    active_ = true;
    ++moved_updates_;
}

} // namespace sarusaru::game
