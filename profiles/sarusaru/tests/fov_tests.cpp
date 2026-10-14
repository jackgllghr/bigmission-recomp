#include "gpu/fov.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
float vertical_fov(const std::array<float, 16> &p) {
    return 2.0f * std::atan(1.0f / std::fabs(p[5])) * 180.0f / std::numbers::pi_v<float>;
}
// The game's common camera projection, as observed (negative y scale included).
sarusaru::gpu::DrawCall perspective() {
    sarusaru::gpu::DrawCall call;
    call.projection = {1.726f, 0, 0, 0, 0, -3.037f, 0, 0, 0.1f, 0.2f, -1.0f, -1.0f, 0.3f, 0.4f, -2.0f, 0};
    return call;
}
} // namespace

int main() {
    using sarusaru::gpu::FovOverride;
    require(!FovOverride().enabled() && !FovOverride(0.0f).enabled(), "native FOV enabled an override");
    auto call = perspective();
    const auto native = call.projection;
    FovOverride().apply(call);
    require(call.projection == native, "native FOV changed the projection");

    const FovOverride wide(60.0f);
    require(wide.enabled() && wide.scale() < 1.0f, "a wider FOV must shrink the projection scale");
    wide.apply(call);
    require(std::fabs(vertical_fov(call.projection) - 60.0f) < 0.3f, "common camera did not reach the chosen FOV");
    require(call.projection[5] < 0.0f && std::fabs(call.projection[0] / call.projection[5] - native[0] / native[5]) < 1e-5f,
            "aspect or y orientation changed");
    for (const int i : {2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15})
        require(call.projection[i] == native[i], "depth, centre or w terms changed");

    // Orthographic projections and screen-space draws stay native.
    auto ortho = perspective();
    ortho.projection[11] = 0.0f;
    ortho.projection[15] = 1.0f;
    const auto ortho_native = ortho.projection;
    wide.apply(ortho);
    require(ortho.projection == ortho_native, "orthographic projection changed");
    auto through = perspective();
    through.through = true;
    wide.apply(through);
    require(through.projection == native, "screen-space draw changed");

    // A narrower camera keeps its proportion to the common one.
    auto zoom = perspective();
    zoom.projection[0] *= 2.0f;
    zoom.projection[5] *= 2.0f;
    wide.apply(zoom);
    require(std::fabs(zoom.projection[5] - 2.0f * call.projection[5]) < 1e-4f, "zoom lost its proportion");
    std::cout << "FOV tests passed\n";
    return 0;
}
