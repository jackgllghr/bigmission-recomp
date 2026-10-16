#include "game/camera_fov.hpp"
#include "game/orbit_camera.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using sarusaru::game::CameraFov;
using sarusaru::game::OrbitCamera;
constexpr std::uint32_t object = 0x08901000u, camera_system = 0x08900000u;
// The common camera's extents at its near plane of 6, as the game computes them.
constexpr std::array<float, 4> kNative = {-3.46096f, 3.46096f, -1.96121f, 1.96121f};
void lens(psprecomp::GuestMemory &m, const std::array<float, 4> &extents) {
    for (unsigned i = 0; i < 4; ++i) m.store32(object + 8u + i * 4u, std::bit_cast<std::uint32_t>(extents[i]));
}
std::array<float, 4> extents(psprecomp::GuestMemory &m) {
    std::array<float, 4> out{};
    for (unsigned i = 0; i < 4; ++i) out[i] = std::bit_cast<float>(m.load32(object + 8u + i * 4u));
    return out;
}
// The FOV the GE projection shows: the native one, scaled by the extents.
float vertical_fov(const std::array<float, 4> &e) {
    constexpr float kPi = std::numbers::pi_v<float>;
    const float native_tangent = std::tan(sarusaru::game::kNativeFovDegrees * kPi / 360.0f);
    return 2.0f * std::atan(native_tangent * e[3] / kNative[3]) * 180.0f / kPi;
}
}

int main() {
    require(!CameraFov().enabled() && !CameraFov(0.0f).enabled(), "native FOV enabled an override");

    psprecomp::Runtime rt;
    auto &m = rt.memory(); auto &ctx = rt.cpu();
    m.store32(0x08A82B24u, camera_system); m.store32(camera_system + 20u, object);
    ctx.set_gpr(4u, object);
    const auto build = [&](CameraFov &fov, std::uint32_t source) {
        ctx.set_gpr(31u, source);
        fov.on_call(OrbitCamera::kBuildView, m, ctx);
    };

    CameraFov native;
    lens(m, kNative);
    build(native, OrbitCamera::kStageViewReturn);
    require(extents(m) == kNative, "native FOV changed the camera");

    // The gameplay cameras reach the chosen FOV, centred, at the native aspect.
    CameraFov wide(60.0f);
    build(wide, OrbitCamera::kStageViewReturn);
    auto e = extents(m);
    require(std::fabs(vertical_fov(e) - 60.0f) < 0.3f, "stage camera did not reach the chosen FOV");
    require(e[0] == -e[1] && e[2] == -e[3] && std::fabs(e[1] / e[3] - kNative[1] / kNative[3]) < 1e-5f,
            "centre or aspect changed");
    // Every frame of it stays at that FOV rather than compounding.
    build(wide, OrbitCamera::kStageViewReturn);
    require(extents(m) == e, "a second gameplay frame widened again");
    build(wide, OrbitCamera::kHubViewReturn);
    require(extents(m) == e, "hub camera differs from the stage camera");

    // Any other view of that camera (a scripted one) gets the native extents back.
    build(wide, 0x0882CCF0u);
    require(extents(m) == kNative, "scripted view kept the wide FOV");
    build(wide, OrbitCamera::kStageViewReturn);
    require(extents(m) == e, "gameplay did not widen again after a scripted view");

    // A lens the game recomputes (a zoom) is the new native and keeps its proportion.
    const std::array<float, 4> zoom = {kNative[0] / 2, kNative[1] / 2, kNative[2] / 2, kNative[3] / 2};
    lens(m, zoom);
    build(wide, OrbitCamera::kStageViewReturn);
    require(std::fabs(extents(m)[3] - e[3] / 2) < 1e-5f, "zoom lost its proportion");
    build(wide, 0x0882CCF0u);
    require(extents(m) == zoom, "zoom's native extents were not restored");

    // Turning the override off puts the native view back at the next build.
    lens(m, kNative);
    build(wide, OrbitCamera::kStageViewReturn);
    wide.set_degrees(0.0f);
    build(wide, OrbitCamera::kStageViewReturn);
    require(extents(m) == kNative, "turning the FOV off kept the wide view");

    // Views of cameras other than the shared one are left alone.
    wide.set_degrees(90.0f);
    m.store32(camera_system + 20u, object + 0x400u);
    build(wide, OrbitCamera::kStageViewReturn);
    require(extents(m) == kNative, "a camera that is not the scene's was changed");
    std::cout << "Camera FOV tests passed\n";
    return 0;
}
