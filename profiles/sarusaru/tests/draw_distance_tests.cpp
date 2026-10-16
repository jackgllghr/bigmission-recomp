#include "game/draw_distance.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
using sarusaru::game::DrawDistance;
constexpr std::uint32_t camera = 0x08901000u, camera_system = 0x08900000u;
constexpr std::uint32_t near_actor = 0x08910000u, far_actor = 0x08911000u, no_matrix = 0x08912000u;
constexpr std::uint32_t loop_sp = 0x09F00000u, caller = 0x08868160u;
using Vec = std::array<float, 3>;

void store(psprecomp::GuestMemory &m, std::uint32_t address, const Vec &v) {
    for (unsigned i = 0; i < 3; ++i) m.store32(address + i * 4u, std::bit_cast<std::uint32_t>(v[i]));
}
Vec load(psprecomp::GuestMemory &m, std::uint32_t address) {
    Vec v{};
    for (unsigned i = 0; i < 3; ++i) v[i] = std::bit_cast<float>(m.load32(address + i * 4u));
    return v;
}
float distance(const Vec &a, const Vec &b) {
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}
void place(psprecomp::GuestMemory &m, std::uint32_t actor, const Vec &position) {
    store(m, actor + 80u, position);
    m.store32(actor + 92u, std::bit_cast<std::uint32_t>(1.0f));
}
}

int main() {
    require(!DrawDistance().enabled() && !DrawDistance(0.5f).enabled() && !DrawDistance(NAN).enabled(),
            "native or invalid scales enabled the override");

    psprecomp::Runtime rt;
    auto &m = rt.memory(); auto &ctx = rt.cpu();
    m.store32(0x08A82B24u, camera_system); m.store32(camera_system + 20u, camera);
    const Vec eye{100.0f, -30.0f, -700.0f};
    store(m, camera + 112u, eye);
    // One actor near the camera, one 400 units away along -z, one without a world matrix.
    const Vec near_position{100.0f, -30.0f, -550.0f}, far_position{100.0f, -30.0f, -300.0f};
    place(m, near_actor, near_position);
    place(m, far_actor, far_position);
    store(m, no_matrix + 80u, far_position);

    const auto call = [&](DrawDistance &dd, std::uint32_t pc, std::uint32_t sp, std::uint32_t ra, std::uint32_t a0) {
        ctx.set_gpr(29u, sp); ctx.set_gpr(31u, ra); ctx.set_gpr(4u, a0);
        dd.on_call(pc, m, ctx);
    };
    // The loop's own frame is 16 bytes below its caller's.
    const auto loop = [&](DrawDistance &dd) { call(dd, DrawDistance::kUpdateLoop, loop_sp, caller, 0x08A80000u); };
    const auto update = [&](DrawDistance &dd, std::uint32_t actor, std::uint32_t ra = DrawDistance::kUpdateReturnNext) {
        call(dd, 0x0883160Cu, loop_sp - 16u, ra, actor);
    };

    DrawDistance native;
    loop(native);
    update(native, far_actor);
    require(load(m, camera + 112u) == eye, "native draw distance moved the eye");

    DrawDistance dd(4.0f);
    loop(dd);
    // Within kKeepDistance an actor sees the real eye.
    update(dd, near_actor, DrawDistance::kUpdateReturnFirst);
    require(load(m, camera + 112u) == eye && !dd.active(), "a near actor saw a moved eye");
    // Beyond it, the excess shrinks by the scale, on the line to the real eye.
    update(dd, far_actor);
    auto seen = load(m, camera + 112u);
    require(std::fabs(distance(seen, far_position) - (200.0f + 200.0f / 4.0f)) < 0.01f, "far distance not scaled");
    require(std::fabs(seen[0] - eye[0]) < 1e-4f && std::fabs(seen[1] - eye[1]) < 1e-4f && seen[2] > eye[2],
            "the eye left the line to the actor");
    // Calls inside the actor's update keep the moved eye.
    call(dd, 0x0882EA68u, loop_sp - 96u, 0x08831B60u, far_actor);
    require(load(m, camera + 112u) == seen, "a call inside the update put the eye back");
    // The next actor starts from the real eye, not the moved one.
    update(dd, near_actor);
    require(load(m, camera + 112u) == eye, "the next actor did not get the real eye");
    update(dd, no_matrix);
    require(load(m, camera + 112u) == eye, "an actor without a world matrix saw a moved eye");

    // The real eye is back as soon as the loop returns to its caller.
    update(dd, far_actor);
    require(dd.active(), "far actor did not move the eye");
    call(dd, 0x089D4E84u, loop_sp, 0x08868168u, 0u);
    require(load(m, camera + 112u) == eye && !dd.active(), "the eye was not restored after the loop");
    // Update calls outside a loop change nothing.
    update(dd, far_actor);
    require(load(m, camera + 112u) == eye, "an update outside the loop moved the eye");

    // An eye the game wrote during an update is the new real one, and stays.
    loop(dd);
    update(dd, far_actor);
    const Vec moved{120.0f, -30.0f, -710.0f};
    store(m, camera + 112u, moved);
    update(dd, far_actor);
    const float expected = 200.0f + (distance(moved, far_position) - 200.0f) / 4.0f;
    require(std::fabs(distance(load(m, camera + 112u), far_position) - expected) < 0.01f,
            "the next actor was not measured from the game's new eye");
    call(dd, 0x08867898u, loop_sp, 0x08868180u, 0u);
    require(load(m, camera + 112u) == moved, "the game's own eye was overwritten");

    // Another thread's stack ends the loop too.
    loop(dd);
    update(dd, far_actor);
    call(dd, 0x08A00000u, 0x09A00000u, 0x08A00010u, 0u);
    require(load(m, camera + 112u) == moved, "another thread saw a moved eye");

    // Turning it off mid-loop puts the eye back at the next call.
    store(m, camera + 112u, eye);
    loop(dd);
    update(dd, far_actor);
    dd.set_scale(1.0f);
    loop(dd);
    require(load(m, camera + 112u) == eye, "turning the draw distance off kept a moved eye");
    std::cout << "Draw distance tests passed\n";
    return 0;
}
