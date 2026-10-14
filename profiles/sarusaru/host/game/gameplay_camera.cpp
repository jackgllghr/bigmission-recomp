#include "game/gameplay_camera.hpp"

namespace sarusaru::game {
void GameplayCamera::on_call(std::uint32_t target_pc) noexcept {
    // Pausing extends gameplay frame by frame; it never starts it.
    if (target_pc == kStagePause) {
        if (active()) last_frame_ = display_.framebuffer_requests();
        return;
    }
    if (target_pc != kStageFollowCamera && target_pc != kHubCamera) return;
    seen_ = true;
    last_frame_ = display_.framebuffer_requests();
    ++calls_;
}

bool GameplayCamera::active() const noexcept {
    return seen_ && display_.framebuffer_requests() - last_frame_ <= 1u;
}

} // namespace sarusaru::game
