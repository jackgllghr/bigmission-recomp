#pragma once
#include "save_state/session_access.hpp"

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>

namespace sarusaru::game {

// The stage's solid geometry, as the camera sees it.
class CameraCollision {
public:
    // The fraction of `delta` a sphere of `radius` at `start` travels before
    // it touches solid geometry: 1 when the way is clear. `caller` is the guest
    // context at the hook, whose stack the query may borrow.
    virtual float sweep(const psprecomp::AllegrexContext &caller, const std::array<float, 3> &start,
                        const std::array<float, 3> &delta, float radius) = 0;

protected:
    ~CameraCollision() = default;
};

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
    // With `collision`, a manually orbited eye stops in front of solid geometry.
    void on_call(std::uint32_t pc, psprecomp::GuestMemory &memory,
                 const psprecomp::AllegrexContext &ctx, std::uint64_t now_us,
                 CameraCollision *collision = nullptr);
    [[nodiscard]] std::uint64_t adjusted_frames() const noexcept { return adjusted_frames_; }

private:
    Input input_;
    float yaw_{}, pitch_{};
    std::uint32_t object_{}, source_{};
    std::uint64_t last_update_us_{}, adjusted_frames_{};
    bool seen_{};
    // Not saved: seconds since manual input, and the native angles last seen.
    // While recent, native follow turns are cancelled so the chosen view holds.
    float idle_{1e9f}, native_azimuth_{}, native_pitch_{};
    // Not saved: the fraction of the orbit distance currently clear of geometry.
    float clear_{1.0f};
};

} // namespace sarusaru::game
