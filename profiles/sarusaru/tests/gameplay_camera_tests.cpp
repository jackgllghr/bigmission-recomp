#include "game/gameplay_camera.hpp"
#include "game/ability_aim.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using sarusaru::game::GameplayCamera;
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    constexpr std::uint32_t system = 0x08900000, aim_camera = 0x08901000;
    require(!sarusaru::game::ability_aiming(memory), "uninitialised scene does not aim");
    memory.store32(0x08A82B24, system);
    memory.store32(system + 20, aim_camera);
    memory.store32(aim_camera + 0xfc, 1);
    memory.store32(aim_camera + 0x228, 5);
    memory.store8(aim_camera + 0x2ab, 1);
    require(sarusaru::game::ability_aiming(memory), "first-person monkey ability aiming recognised");
    memory.store32(aim_camera + 0x228, 0);
    require(!sarusaru::game::ability_aiming(memory), "ordinary following does not remap movement");
    memory.store32(aim_camera + 0x228, 5);
    memory.store8(aim_camera + 0x2ab, 0);
    require(!sarusaru::game::ability_aiming(memory), "aim transition does not remap input early");
    memory.store8(aim_camera + 0x2ab, 1);
    memory.store32(aim_camera + 0x2dc, 1);
    require(!sarusaru::game::ability_aiming(memory), "scripted camera overrides suppress aiming input");
    memory.store32(system + 20, 0xffffffff);
    require(!sarusaru::game::ability_aiming(memory), "invalid camera pointer is safe");
    sarusaru::display::Display display;
    const auto next_frame = [&] {
        require(display.set_framebuffer({0x04000000u, 512u, 3u}, 0u, 0u, runtime.memory()) == 0, "framebuffer rejected");
    };
    GameplayCamera camera(display);
    require(!camera.active(), "active before any gameplay camera ran");
    camera.on_call(0x08882CB0u);  // the shared camera copy runs in every scene
    require(!camera.active(), "an unrelated routine counted as a gameplay camera");

    camera.on_call(GameplayCamera::kStageFollowCamera);
    require(camera.active(), "stage follow camera not recognised");
    next_frame();
    require(camera.active(), "the frame after a gameplay camera is not covered");
    next_frame();
    require(!camera.active(), "still active two frames after the camera stopped (menu or cutscene)");

    // A pause does not start gameplay, but one entered from gameplay keeps it.
    camera.on_call(GameplayCamera::kStagePause);
    require(!camera.active(), "a pause outside gameplay counted as gameplay");
    camera.on_call(GameplayCamera::kStageFollowCamera);
    for (int frame = 0; frame < 10; ++frame) {
        next_frame();
        camera.on_call(GameplayCamera::kStagePause);
    }
    require(camera.active(), "pausing during gameplay dropped the gameplay camera");
    next_frame();
    next_frame();
    require(!camera.active(), "still active after leaving the pause for a non-gameplay scene");

    camera.on_call(GameplayCamera::kHubCamera);
    require(camera.active() && camera.gameplay_frames() == 3u, "hub camera not recognised");
    std::cout << "gameplay camera tests passed\n";
    return 0;
}
