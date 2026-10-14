#pragma once

// Field-of-view override at the GE projection.
//
// The game's common 3D camera uploads a perspective projection with a vertical
// FOV of about 36.5 degrees (docs/settings-investigation.md). The override
// widens or narrows every perspective projection by the same factor, so the
// common camera reaches the chosen FOV and narrower ones (zooms, the startup
// scene) keep their proportion to it. Orthographic and screen-space draws are
// untouched, so the HUD and 2D menus stay as they are. The game does not know:
// its own visibility culling still uses the native view, so objects just
// outside it can appear late at the edges of a wider one.
#include "gpu/ge_state.hpp"

namespace sarusaru::gpu {

inline constexpr float kNativeFovDegrees = 36.5f;

class FovOverride {
public:
    // 0 keeps the native projection; otherwise the common camera's vertical FOV.
    explicit FovOverride(float degrees = 0.0f);
    [[nodiscard]] bool enabled() const noexcept { return scale_ != 1.0f; }
    // The factor applied to the projection's x and y scale.
    [[nodiscard]] float scale() const noexcept { return scale_; }
    // Applies the override to a transformed draw's projection, when perspective.
    void apply(DrawCall &call) const noexcept;

private:
    float scale_{1.0f};
};

} // namespace sarusaru::gpu
