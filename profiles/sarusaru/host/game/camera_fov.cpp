#include "game/camera_fov.hpp"

#include "game/orbit_camera.hpp"

#include <bit>
#include <cmath>
#include <numbers>

namespace sarusaru::game {
namespace {
constexpr std::uint32_t kSystemPointer = 0x08A82B24u;
constexpr std::uint32_t kExtents = 8u;
}

void CameraFov::set_degrees(float degrees) noexcept {
    factor_ = 1.0f;
    if (!(degrees > 0.0f && degrees < 180.0f)) return;
    const auto half_tangent = [](float fov) { return std::tan(fov * std::numbers::pi_v<float> / 360.0f); };
    factor_ = half_tangent(degrees) / half_tangent(kNativeFovDegrees);
}

void CameraFov::on_call(std::uint32_t pc, psprecomp::GuestMemory &memory, const psprecomp::AllegrexContext &ctx) {
    if (pc != OrbitCamera::kBuildView) return;
    const auto object = ctx.gpr[4];
    const auto system = memory.load32(kSystemPointer);
    if ((object & 3u) != 0 || !memory.contains(system, 24u) || memory.load32(system + 20u) != object ||
        !memory.contains(object + kExtents, 16u)) return;

    std::array<std::uint32_t, 4> current{};
    for (unsigned i = 0; i < 4u; ++i) current[i] = memory.load32(object + kExtents + i * 4u);
    // Another camera, or the game recomputed this one's lens: what is there is native.
    if (object != object_ || current != written_) {
        object_ = object;
        native_ = current;
    }
    const auto source = ctx.gpr[31];
    const bool gameplay = source == OrbitCamera::kStageViewReturn || source == OrbitCamera::kHubViewReturn;
    auto target = native_;
    if (gameplay && enabled()) {
        for (auto &bits : target) {
            const float scaled = std::bit_cast<float>(bits) * factor_;
            if (std::isfinite(scaled)) bits = std::bit_cast<std::uint32_t>(scaled);
        }
        ++widened_views_;
    }
    if (target != current)
        for (unsigned i = 0; i < 4u; ++i) memory.store32(object + kExtents + i * 4u, target[i]);
    written_ = target;
}

} // namespace sarusaru::game
