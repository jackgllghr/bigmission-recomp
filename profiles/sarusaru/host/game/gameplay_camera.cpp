#include "game/gameplay_camera.hpp"

namespace sarusaru::game {
namespace {
GameplayCamera *g_instance = nullptr;

void pre_chained(psprecomp::Runtime &, psprecomp::AllegrexContext &, std::uint32_t target_pc, std::uint32_t) {
    if (g_instance != nullptr) g_instance->on_call(target_pc);
}
// Calls the generated code cannot chain (depth limit, chaining disabled) go
// through the outer dispatcher instead.
void pre_dispatch(psprecomp::Runtime &, psprecomp::AllegrexContext &, std::uint32_t dispatch_pc, std::int32_t) {
    if (g_instance != nullptr) g_instance->on_call(dispatch_pc);
}
} // namespace

GameplayCamera::~GameplayCamera() {
    if (!installed_ || g_instance != this) return;
    g_instance = nullptr;
    psprecomp::set_runtime_pre_chained_call_hook(nullptr);
    psprecomp::set_runtime_pre_dispatch_hook(nullptr);
}

void GameplayCamera::install() {
    g_instance = this;
    installed_ = true;
    psprecomp::set_runtime_pre_chained_call_hook(&pre_chained);
    psprecomp::set_runtime_pre_dispatch_hook(&pre_dispatch);
}

void GameplayCamera::on_call(std::uint32_t target_pc) noexcept {
    if (target_pc != kStageFollowCamera && target_pc != kHubCamera) return;
    seen_ = true;
    last_frame_ = display_.framebuffer_requests();
    ++calls_;
}

bool GameplayCamera::active() const noexcept {
    return seen_ && display_.framebuffer_requests() - last_frame_ <= 1u;
}

} // namespace sarusaru::game
