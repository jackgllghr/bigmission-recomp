#include "game/orbit_camera.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using Camera = sarusaru::game::OrbitCamera;
constexpr std::uint32_t object = 0x08901000u, camera_system = 0x08900000u;
void store(psprecomp::GuestMemory &m, unsigned offset, float value) { m.store32(object + offset, std::bit_cast<std::uint32_t>(value)); }
float load(psprecomp::GuestMemory &m, unsigned offset) { return std::bit_cast<float>(m.load32(object + offset)); }
void pose(psprecomp::GuestMemory &m) {
    // Native eye behind the player, target at the origin, Y pointing down.
    for (unsigned i = 0; i < 3; ++i) { store(m, 0x70u + i * 4, 0); store(m, 0x90u + i * 4, 0); }
    store(m, 0x78u, 10.0f);
}
std::array<unsigned, 3> eye(psprecomp::GuestMemory &m) { return {m.load32(object + 0x70u), m.load32(object + 0x74u), m.load32(object + 0x78u)}; }
}

int main() {
    psprecomp::Runtime rt;
    auto &m = rt.memory(); auto &ctx = rt.cpu();
    m.store32(0x08A82B24u, camera_system); m.store32(camera_system + 20u, object);
    m.store32(object + 0xFCu, 1u); m.store32(object + 0x228u, 0u);
    ctx.set_gpr(4u, object); ctx.set_gpr(31u, Camera::kStageViewReturn);
    const auto update = [&](Camera &camera, unsigned time) { pose(m); camera.on_call(Camera::kBuildView, m, ctx, time); };
    Camera neutral; pose(m); const auto original = eye(m);
    update(neutral, 0); update(neutral, 33367);
    require(eye(m) == original && neutral.adjusted_frames() == 0, "neutral input changed native eye");

    Camera camera; camera.set_input({1, -1, false});
    update(camera, 0); update(camera, 33367);
    require(load(m, 0x70u) > 0 && load(m, 0x74u) < 0, "orbit direction incorrect");
    const float radius = std::sqrt(std::pow(load(m, 0x70u), 2) + std::pow(load(m, 0x74u), 2) + std::pow(load(m, 0x78u), 2));
    require(std::abs(radius - 10.0f) < 1e-4f && load(m, 0x90u) == 0 && load(m, 0x94u) == 0,
            "orbit changed the target or distance");
    camera.set_input({}); const auto moved = eye(m); update(camera, 66734);
    require(eye(m) == moved, "released stick lost the orbit or accumulated it twice");
    camera.set_input({0, 0, true}); update(camera, 100101);
    require(eye(m) == original, "reset did not restore native camera");

    // Native locked/scripted modes take precedence even though they also use
    // the stage follow helper. Neither holding nor resetting may write them.
    for (unsigned mode = 2; mode < 12; ++mode) {
        m.store32(object + 0x228u, mode); camera.set_input({1, -1, false}); update(camera, 133468);
        require(eye(m) == original, "locked stage camera was orbited");
    }
    m.store32(object + 0x228u, 0u);
    m.store32(object + 0x2DCu, 0x08903000u); update(camera, 166835);
    require(eye(m) == original, "scripted override was orbited");
    m.store32(object + 0x2DCu, 0u); m.store8(object + 0x2ABu, 1u); update(camera, 200202);
    require(eye(m) == original, "camera transition was orbited");
    m.store8(object + 0x2ABu, 0u);
    for (auto caller : {0x0882CEB4u, 0x088946A0u, 0x08866918u, 0x088405F4u}) {
        ctx.set_gpr(31u, caller); update(camera, 233569);
        require(eye(m) == original, "tunnel, cutscene, pause or menu camera was orbited");
    }
    ctx.set_gpr(31u, Camera::kStageViewReturn); update(camera, 1'000'000u);
    require(eye(m) == original, "scene gap applied stale orbit or time delta");

    // Equivalent elapsed time at 30 and 60 updates a second must agree.
    const auto elapsed = [&](unsigned interval) {
        Camera c; c.set_input({0.5f, 0, false});
        for (unsigned time = 0; time <= 1'000'000u; time += interval) update(c, time);
        return load(m, 0x70u);
    };
    require(std::abs(elapsed(40'000u) - elapsed(20'000u)) < 1e-4f, "orbit speed depends on update rate");
    Camera limits; limits.set_input({0, -1, false});
    for (unsigned i = 0; i < 500; ++i) update(limits, i * 33367);
    require(std::isfinite(load(m, 0x70u)) && load(m, 0x74u) > -10.0f && load(m, 0x74u) < -9.0f,
            "pitch was not clamped away from vertical");
    Camera hub; ctx.set_gpr(31u, Camera::kHubViewReturn); hub.set_input({-1, 0, false});
    update(hub, 0); update(hub, 33367);
    require(load(m, 0x70u) < 0, "hub camera did not orbit");
    Camera invalid; invalid.set_input({std::numeric_limits<float>::quiet_NaN(), 0, false});
    update(invalid, 0); update(invalid, 33367); require(eye(m) == original, "nonfinite input corrupted camera");
    ctx.set_gpr(4u, 0xFFFFFFFFu); invalid.on_call(Camera::kBuildView, m, ctx, 66734);
    std::cout << "orbit camera tests passed\n";
}
