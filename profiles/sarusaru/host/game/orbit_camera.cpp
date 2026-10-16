#include "game/orbit_camera.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>

namespace sarusaru::game {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float radians(float degrees) { return degrees * kPi / 180.0f; }
constexpr std::uint32_t kSystemPointer = 0x08A82B24u;
// After the stick is released, hold the manual view against the native
// follow camera, then hand control back over a short blend.
constexpr float kFollowDelay = 1.5f, kFollowBlend = 1.0f;
// Manual pitch limits. Below the pull-in angle (or the native one, if lower)
// the eye moves towards the player, as low cameras do, so looking up from
// near the ground keeps the eye close to the player instead of under the floor.
constexpr float kMinElevation = radians(-10.0f), kMaxElevation = radians(70.0f);
constexpr float kPullInStart = radians(15.0f), kClosestDistance = 0.3f;
// The eye is a sphere this size against walls and floors. A blocked view
// moves in at once, then eases back out (fraction of the distance a second).
constexpr float kEyeRadius = 2.0f, kClearRecovery = 1.5f;
// A larger per-update native turn is a cut, not follow motion.
constexpr float kMaxFollowStep = radians(30.0f);
float load(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return std::bit_cast<float>(memory.load32(address));
}
float axis(float value) { return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f; }
}

void OrbitCamera::set_input(Input input) noexcept {
    input_ = {axis(input.x), axis(input.y), input.reset};
}

void OrbitCamera::on_call(std::uint32_t pc, psprecomp::GuestMemory &memory,
                          const psprecomp::AllegrexContext &ctx, std::uint64_t now_us,
                          CameraCollision *collision) {
    if (pc != kBuildView) return;
    const auto source = ctx.gpr[31];
    if (source != kStageViewReturn && source != kHubViewReturn) return;
    const auto object = ctx.gpr[4];
    const auto system = memory.load32(kSystemPointer);
    if ((object & 3u) != 0 || !memory.contains(system, 24u) || memory.load32(system + 20u) != object ||
        !memory.contains(object, source == kStageViewReturn ? 0x2E0u : 0x100u)) return;
    // Stage modes 0/1 follow the player. Other modes interpolate, track a
    // fixed point, or run a scripted camera. An explicit camera override or
    // transition flag also takes precedence over manual orbiting.
    if (source == kStageViewReturn && (memory.load32(object + 0xFCu) != 1u ||
        memory.load32(object + 0x228u) > 1u || memory.load32(object + 0x2DCu) != 0u ||
        memory.load8(object + 0x2ABu) != 0u)) {
        seen_ = false;
        return;
    }

    std::array<float, 3> eye{}, target{}, delta{};
    float length_squared = 0.0f;
    for (unsigned i = 0; i < 3u; ++i) {
        eye[i] = load(memory, object + 0x70u + i * 4u);
        target[i] = load(memory, object + 0x90u + i * 4u);
        if (!std::isfinite(eye[i]) || !std::isfinite(target[i])) return;
        delta[i] = eye[i] - target[i];
        length_squared += delta[i] * delta[i];
    }
    if (!std::isfinite(length_squared) || length_squared < 0.0001f) return;

    // Reset on scene/camera changes or a gap in ordinary gameplay. A paused
    // or locked camera never accumulates input or carries an old time delta.
    const bool continuing = seen_ && object_ == object && source_ == source &&
        now_us >= last_update_us_ && now_us - last_update_us_ <= 100'000u;
    const float seconds = continuing ? static_cast<float>(now_us - last_update_us_) * 1e-6f : 0.0f;
    if (!continuing || input_.reset) {
        yaw_ = pitch_ = 0.0f;
        idle_ = kFollowDelay + kFollowBlend;
        clear_ = 1.0f;
    }
    seen_ = true;
    object_ = object;
    source_ = source;
    last_update_us_ = now_us;
    const float distance = std::sqrt(length_squared);
    // This game's world Y points down. Keep the eye away from the vertical
    // singularity; retaining the native target and radius keeps it centred.
    const float native_azimuth = std::atan2(delta[0], delta[2]);
    const float native_pitch = std::asin(std::clamp(-delta[1] / distance, -1.0f, 1.0f));
    // Shortly after manual input, cancel the native camera's follow turn so
    // it does not immediately swing the chosen view back behind the player.
    const float hold = std::clamp((kFollowDelay + kFollowBlend - idle_) / kFollowBlend, 0.0f, 1.0f);
    const float turn = std::remainder(native_azimuth - native_azimuth_, 2.0f * kPi);
    const float tilt = native_pitch - native_pitch_;
    if (continuing && hold > 0.0f && (yaw_ != 0.0f || pitch_ != 0.0f) &&
        std::abs(turn) <= kMaxFollowStep && std::abs(tilt) <= kMaxFollowStep) {
        yaw_ = std::remainder(yaw_ - turn * hold, 2.0f * kPi);
        pitch_ -= tilt * hold;
    }
    native_azimuth_ = native_azimuth;
    native_pitch_ = native_pitch;
    if (!input_.reset) {
        yaw_ = std::remainder(yaw_ + input_.x * radians(100.0f) * seconds, 2.0f * kPi);
        pitch_ -= input_.y * radians(70.0f) * seconds;
        idle_ = input_.x != 0.0f || input_.y != 0.0f ? 0.0f : std::min(idle_ + seconds, kFollowDelay + kFollowBlend);
    }
    const float elevation = std::clamp(native_pitch + pitch_, kMinElevation, kMaxElevation);
    if (pitch_ != 0.0f) pitch_ = elevation - native_pitch;
    if (yaw_ == 0.0f && pitch_ == 0.0f) {
        clear_ = 1.0f;
        return;
    }
    const float azimuth = native_azimuth + yaw_;
    // Never pull in at or above the native angle, so the native view is unchanged.
    const float pull_start = std::min(kPullInStart, native_pitch);
    const float pull = pull_start > kMinElevation ?
        std::clamp((pull_start - elevation) / (pull_start - kMinElevation), 0.0f, 1.0f) : 0.0f;
    const float radius = distance * (1.0f - (1.0f - kClosestDistance) * pull * pull * (3.0f - 2.0f * pull));
    const float horizontal = radius * std::cos(elevation);
    std::array<float, 3> offset = {horizontal * std::sin(azimuth), -radius * std::sin(elevation),
                                   horizontal * std::cos(azimuth)};
    // Native cameras are placed by the stage's design; only a manual view can
    // swing behind a wall or under the floor, so only it is swept.
    if (collision != nullptr) {
        const float free = collision->sweep(ctx, target, offset, kEyeRadius);
        const float reached = std::isfinite(free) ? std::clamp(free, 0.0f, 1.0f) : 1.0f;
        clear_ = reached < clear_ ? reached : std::min(reached, clear_ + kClearRecovery * seconds);
        for (auto &value : offset) value *= clear_;
    }
    for (unsigned i = 0; i < 3u; ++i)
        memory.store32(object + 0x70u + i * 4u, std::bit_cast<std::uint32_t>(target[i] + offset[i]));
    // The guest builds its direction, yaw, view matrix and culling from this
    // eye immediately afterwards. Leave the native follow/spring state alone.
    ++adjusted_frames_;
}

} // namespace sarusaru::game
