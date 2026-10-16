#pragma once

// Field-of-view override in the game's own camera, so its culling widens with
// the picture.
//
// Every camera object keeps its view frustum as the near-plane extents
// (left, right, bottom, top at +8..+20), computed from the near plane (+0)
// and an FOV parameter (+248) when the camera is made or its lens changes
// (0x088FCF80). The game builds both its GE projection and its visibility
// culling from those extents, so scaling them widens the view without
// objects at the edges being culled as if it were native
// (docs/settings-investigation.md). The common camera's extents give a
// vertical FOV of about 36.5 degrees; every camera is scaled by the same
// factor, so narrower ones (zooms) keep their proportion to it.
//
// The scaling happens at the view-build call (OrbitCamera::kBuildView) of
// the stage follow camera and the hub's walking camera, so the title, menus,
// the warp tunnel, stage introductions and other scripted cameras build their
// views from the native extents, which are put back when one of them builds
// a view with a camera this widened. A pause entered from gameplay builds no
// view and keeps the wide one.
#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>

namespace sarusaru::game {

inline constexpr float kNativeFovDegrees = 36.5f;

class CameraFov {
public:
    // 0 keeps the native view; otherwise the common camera's vertical FOV.
    explicit CameraFov(float degrees = 0.0f) { set_degrees(degrees); }
    void set_degrees(float degrees) noexcept;
    [[nodiscard]] bool enabled() const noexcept { return factor_ != 1.0f; }
    // The factor applied to the extents (tangent of the half-angle).
    [[nodiscard]] float factor() const noexcept { return factor_; }

    void on_call(std::uint32_t pc, psprecomp::GuestMemory &memory, const psprecomp::AllegrexContext &ctx);
    [[nodiscard]] std::uint64_t widened_views() const noexcept { return widened_views_; }

private:
    float factor_{1.0f};
    std::uint32_t object_{};
    // The camera's extents as the game computed them, and as last written here.
    std::array<std::uint32_t, 4> native_{}, written_{};
    std::uint64_t widened_views_{};
};

} // namespace sarusaru::game
