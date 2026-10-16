#pragma once

// Draw-distance override for the game's actors.
//
// Actors (enemies, monkeys, item markers, props with behaviour) fade out with
// distance from the camera on their own: each class's update measures from the
// scene camera's eye (+112 of *(*(0x08A82B24)+20)) and, in the common case,
// sets a fade (actor+300) of (350 - d) / 100, so actors are fully drawn to 250
// units and gone at 350, far inside the camera's far plane of 9000. Some
// classes also skip their animation beyond 350. The actor manager draws an
// actor opaque at fade 1, translucent below that and not at all at 0
// (docs/settings-investigation.md).
//
// Each class tests distance its own way, so the override changes what the
// actors see rather than any one test: while the update loop (kUpdateLoop)
// runs an actor's update, the camera eye is moved along the line from the
// actor towards the real eye, to where the distance beyond kKeepDistance is
// divided by the scale. Directions from actors to the camera, and everything
// within kKeepDistance (near fades), are unchanged. The real eye is put back
// when the loop returns, before the camera and the draws use it.
#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>

namespace sarusaru::game {

class DrawDistance {
public:
    static constexpr std::uint32_t kUpdateLoop = 0x08900BFCu;
    // Where the loop's two update calls return; the actor is the argument.
    static constexpr std::uint32_t kUpdateReturnFirst = 0x08900C40u;
    static constexpr std::uint32_t kUpdateReturnNext = 0x08900C6Cu;
    // The distance up to which actors see the real camera distance.
    static constexpr float kKeepDistance = 200.0f;

    // 1 keeps the native distances; otherwise the factor on the distance
    // beyond kKeepDistance (2 = actors fade from 300 to 500 instead of 250 to 350).
    explicit DrawDistance(float scale = 1.0f) { set_scale(scale); }
    void set_scale(float scale) noexcept;
    [[nodiscard]] float scale() const noexcept { return scale_; }
    [[nodiscard]] bool enabled() const noexcept { return scale_ != 1.0f; }

    void on_call(std::uint32_t pc, psprecomp::GuestMemory &memory, const psprecomp::AllegrexContext &ctx);
    // Whether the eye currently holds a value written here.
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::uint64_t moved_updates() const noexcept { return moved_updates_; }

private:
    void restore(psprecomp::GuestMemory &memory) noexcept;

    float scale_{1.0f};
    bool active_{};
    std::uint32_t camera_{}, loop_sp_{};
    // The camera's real eye, and the one last written for an actor.
    std::array<std::uint32_t, 3> real_{}, written_{};
    std::uint64_t moved_updates_{};
};

} // namespace sarusaru::game
