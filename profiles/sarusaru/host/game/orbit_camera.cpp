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
                          const psprecomp::AllegrexContext &ctx, std::uint64_t now_us) {
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
    const float elevation = std::clamp(native_pitch + pitch_, radians(-10.0f), radians(70.0f));
    if (pitch_ != 0.0f) pitch_ = elevation - native_pitch;
    if (yaw_ == 0.0f && pitch_ == 0.0f) return;
    const float azimuth = native_azimuth + yaw_;
    const float horizontal = distance * std::cos(elevation);
    const std::array<float, 3> rotated = {target[0] + horizontal * std::sin(azimuth),
        target[1] - distance * std::sin(elevation), target[2] + horizontal * std::cos(azimuth)};
    for (unsigned i = 0; i < 3u; ++i)
        memory.store32(object + 0x70u + i * 4u, std::bit_cast<std::uint32_t>(rotated[i]));
    // The guest builds its direction, yaw, view matrix and culling from this
    // eye immediately afterwards. Leave the native follow/spring state alone.
    ++adjusted_frames_;
}

} // namespace sarusaru::game
