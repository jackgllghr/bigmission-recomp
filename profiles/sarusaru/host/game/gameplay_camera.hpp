#pragma once
#include "save_state/session_access.hpp"

// Recognises the game frames drawn through a player-following gameplay camera,
// so camera overrides (the FOV) leave the title, menus and cutscenes native.
//
// Every scene writes one shared camera parameter block (the system object at
// 0x08A82B24, +20), so its contents cannot tell a scripted camera from a
// gameplay one. Which routine drives it can: the stage's follow camera and
// the stage hub's camera run once per game frame exactly while the player is
// in control, and not in the title, menus, the stage-select menu, the pause
// menu, the warp tunnel or the stage introduction (docs/settings-investigation.md).
// The stage's pause state runs its own routine every paused frame; a pause
// entered from gameplay keeps counting as gameplay, so the frozen scene behind
// the pause menu keeps its view. All are reached through cross-unit calls or
// outer dispatch, which the runtime's call hooks see.
#include "display/display.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru::game {

class GameplayCamera {
    friend struct sarusaru::save_state::SessionCodec;
public:
    // Routines of the SHA-256-verified UCJS10060 executable.
    static constexpr std::uint32_t kStageFollowCamera = 0x0888A09Cu;
    static constexpr std::uint32_t kHubCamera = 0x08809814u;
    static constexpr std::uint32_t kStagePause = 0x08866918u;

    explicit GameplayCamera(const display::Display &display) : display_(display) {}
    GameplayCamera(const GameplayCamera &) = delete;
    GameplayCamera &operator=(const GameplayCamera &) = delete;

    // Records a guest call; what the hooks do, exposed for tests.
    void on_call(std::uint32_t target_pc) noexcept;
    // Whether the game frame now being drawn, or the one before it, ran a
    // gameplay camera. The extra frame covers draws that come before the
    // camera update in a frame, so a cut changes the FOV at most a frame late.
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint64_t gameplay_frames() const noexcept { return calls_; }

private:
    const display::Display &display_;
    bool seen_{};
    std::uint64_t last_frame_{};
    std::uint64_t calls_{};
};

} // namespace sarusaru::game
