#include "game/gameplay_camera.hpp"

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

    camera.on_call(GameplayCamera::kHubCamera);
    require(camera.active() && camera.gameplay_frames() == 2u, "hub camera not recognised");
    std::cout << "gameplay camera tests passed\n";
    return 0;
}
