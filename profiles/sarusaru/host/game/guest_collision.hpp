#pragma once
#include "game/orbit_camera.hpp"

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>

namespace sarusaru::game {

// The camera's collision, answered by the game's own sphere sweep: the query
// the player's movement makes against the stage's collision meshes.
//
// 0x088623F0(mask a0, start a1, delta a2, result a3, radius f12) sweeps a
// sphere along `delta` through every collision object on the list at
// 0x08A805F8 whose layer is in the mask. It returns nonzero on contact, with
// the fraction of `delta` travelled at result+100. The player passes radius
// 2.4 and mask 0x07FF5380. The routine is pure guest code (no imports); it
// leaves only its query scratch in globals, which are restored afterwards.
class GuestCollision final : public CameraCollision {
public:
    static constexpr std::uint32_t kSphereSweep = 0x088623F0u;
    static constexpr std::uint32_t kPlayerSolids = 0x07FF5380u;

    explicit GuestCollision(psprecomp::Runtime &runtime) : runtime_(runtime) {}
    GuestCollision(const GuestCollision &) = delete;
    GuestCollision &operator=(const GuestCollision &) = delete;

    float sweep(const psprecomp::AllegrexContext &caller, const std::array<float, 3> &start,
                const std::array<float, 3> &delta, float radius) override;
    // True while the guest query runs; call hooks must not act on its calls.
    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    psprecomp::Runtime &runtime_;
    bool active_{};
};

} // namespace sarusaru::game
