#include "gpu/fov.hpp"

#include <cmath>
#include <numbers>

namespace sarusaru::gpu {

FovOverride::FovOverride(float degrees) {
    if (degrees <= 0.0f || degrees >= 180.0f) return;
    const auto half_tangent = [](float fov) { return std::tan(fov * std::numbers::pi_v<float> / 360.0f); };
    scale_ = half_tangent(kNativeFovDegrees) / half_tangent(degrees);
}

void FovOverride::apply(DrawCall &call) const noexcept {
    auto &p = call.projection;
    // Column-major: a perspective projection puts -z (or z) into clip w and
    // nothing constant there; an orthographic one has w = 1.
    if (!enabled() || call.through || p[11] == 0.0f || p[15] != 0.0f) return;
    // Scale clip x and y about the projection centre: the off-centre and
    // translation terms (p[8], p[9], p[12], p[13]) stay.
    for (const int i : {0, 1, 4, 5}) p[i] *= scale_;
}

} // namespace sarusaru::gpu
