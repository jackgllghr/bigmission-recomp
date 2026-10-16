#pragma once
#include "save_state/session_access.hpp"

#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru::game {

// Player-centred orbit at the native view-build boundary. Only the hub's
// walking camera and the stage's ordinary follow modes accept host input.
class OrbitCamera {
    friend struct sarusaru::save_state::SessionCodec;
public:
    struct Input { float x{}, y{}; bool reset{}; };
    static constexpr std::uint32_t kBuildView = 0x088FD1B0u;
    static constexpr std::uint32_t kStageViewReturn = 0x0888A120u;
    static constexpr std::uint32_t kHubViewReturn = 0x08809124u;

    void set_input(Input input) noexcept;
    void on_call(std::uint32_t pc, psprecomp::GuestMemory &memory,
                 const psprecomp::AllegrexContext &ctx, std::uint64_t now_us);
    [[nodiscard]] std::uint64_t adjusted_frames() const noexcept { return adjusted_frames_; }

private:
    Input input_;
    float yaw_{}, pitch_{};
    std::uint32_t object_{}, source_{};
    std::uint64_t last_update_us_{}, adjusted_frames_{};
    bool seen_{};
};

} // namespace sarusaru::game
