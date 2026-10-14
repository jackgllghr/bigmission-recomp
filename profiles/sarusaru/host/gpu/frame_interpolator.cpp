#include "gpu/frame_interpolator.hpp"

#include <algorithm>
#include <cmath>

namespace sarusaru::gpu {
namespace {

// A matched draw is blended only while it moves less than this between game
// frames: a fraction of its distance from the camera for 3D draws, pixels for
// screen-space ones. Larger steps are cuts or recycled draws, not motion.
constexpr float kMaxRelativeStep = 0.35f;
constexpr float kMaxScreenStep = 64.0f;
// Among instances of one mesh, only this many unused candidates are compared.
constexpr std::size_t kCandidateWindow = 32u;

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
    hash ^= hash >> 31u;
    hash *= 0xBF58476D1CE4E5B9ull;
    return hash ^ (hash >> 29u);
}

using Matrix = std::array<float, 16>;

// Column-major 4x4 times a point.
std::array<float, 3> transform(const Matrix &m, const std::array<float, 4> &p) noexcept {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

Matrix multiply(const Matrix &a, const Matrix &b) noexcept {
    Matrix out{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k) out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return out;
}

float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

std::uint32_t lerp_color(std::uint32_t a, std::uint32_t b, float t) noexcept {
    std::uint32_t out = 0u;
    for (std::uint32_t shift = 0u; shift < 32u; shift += 8u) {
        const float channel = lerp(static_cast<float>((a >> shift) & 0xFFu), static_cast<float>((b >> shift) & 0xFFu), t);
        out |= static_cast<std::uint32_t>(std::clamp(std::lround(channel), 0l, 255l)) << shift;
    }
    return out;
}

float distance(const std::array<float, 3> &a, const std::array<float, 3> &b, bool screen) noexcept {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = screen ? 0.0f : a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Whether a step from `from` to `to` (both in view or screen space) is motion
// rather than a jump.
bool small_step(const std::array<float, 3> &from, const std::array<float, 3> &to, bool screen) noexcept {
    const float step = distance(from, to, screen);
    if (!std::isfinite(step)) return false;
    if (screen) return step <= kMaxScreenStep;
    const float depth = std::max({std::fabs(from[2]), std::fabs(to[2]), 1e-3f});
    return step <= kMaxRelativeStep * depth;
}

} // namespace

void FrameInterpolator::end_frame() {
    previous_.swap(current_);
    current_.clear();
    exact_.clear();
    loose_.clear();
    for (std::uint32_t i = 0; i < previous_.size(); ++i) {
        exact_[previous_[i].exact_key].records.push_back(i);
        loose_[previous_[i].loose_key].records.push_back(i);
    }
}

void FrameInterpolator::reset() {
    previous_.clear();
    current_.clear();
    exact_.clear();
    loose_.clear();
}

FrameInterpolator::Record *FrameInterpolator::best_of(Candidates &candidates, const Record &current) {
    auto &list = candidates.records;
    while (candidates.first_unused < list.size() && previous_[list[candidates.first_unused]].used)
        ++candidates.first_unused;
    Record *best = nullptr;
    float best_distance = 0.0f;
    std::size_t compared = 0u;
    for (std::size_t i = candidates.first_unused; i < list.size() && compared < kCandidateWindow; ++i) {
        Record &candidate = previous_[list[i]];
        if (candidate.used) continue;
        ++compared;
        const float d = distance(candidate.anchor, current.anchor, current.through);
        if (best == nullptr || d < best_distance) {
            best = &candidate;
            best_distance = d;
        }
    }
    return best;
}

FrameInterpolator::Record *FrameInterpolator::match(const Record &current) {
    if (const auto found = exact_.find(current.exact_key); found != exact_.end())
        if (Record *record = best_of(found->second, current)) return record;
    if (const auto found = loose_.find(current.loose_key); found != loose_.end())
        return best_of(found->second, current);
    return nullptr;
}

bool FrameInterpolator::interpolate(const DrawCall &call, float t, DrawCall &out) {
    out = call;
    Record record;
    // Where the vertices come from tells instances of static meshes apart;
    // dynamic buffers move between frames, so the loose key leaves it out.
    // The render target is not part of either: the game alternates two
    // display buffers, so consecutive frames always draw to different ones.
    std::uint64_t loose = 0u;
    for (const std::uint64_t value : {std::uint64_t{static_cast<std::uint8_t>(call.primitive)}, std::uint64_t{call.through},
                                      std::uint64_t{call.clear_mode}, std::uint64_t{call.vertex_type},
                                      std::uint64_t{call.primitive_count}, std::uint64_t{call.texture.address},
                                      std::uint64_t{call.vertices.size()}})
        loose = mix(loose, value);
    record.loose_key = loose;
    record.exact_key = mix(mix(loose, call.vertex_address), call.index_address);
    record.through = call.through;
    record.world = call.world;
    record.view = call.view;
    record.projection = call.projection;
    record.offset_u = call.texture.offset_u;
    record.offset_v = call.texture.offset_v;
    record.material_color = call.material_color;
    record.vertices = call.vertices;
    const Matrix view_world = multiply(call.view, call.world);
    if (!call.vertices.empty())
        record.anchor = call.through ? std::array<float, 3>{call.vertices[0].position[0], call.vertices[0].position[1], 0.0f}
                                     : transform(view_world, call.vertices[0].position);

    Record *previous = call.vertices.empty() ? nullptr : match(record);
    bool blended = false;
    if (previous == nullptr) {
        ++stats_.unmatched;
    } else {
        previous->used = true;
        if (!small_step(previous->anchor, record.anchor, call.through)) {
            ++stats_.snapped;
        } else {
            blended = true;
            if (!call.through) {
                for (int i = 0; i < 16; ++i) {
                    out.world[i] = lerp(previous->world[i], call.world[i], t);
                    out.view[i] = lerp(previous->view[i], call.view[i], t);
                    out.projection[i] = lerp(previous->projection[i], call.projection[i], t);
                }
            }
            // Scrolling textures; a wrap back to the start is not motion.
            if (std::fabs(call.texture.offset_u - previous->offset_u) < 0.25f)
                out.texture.offset_u = lerp(previous->offset_u, call.texture.offset_u, t);
            if (std::fabs(call.texture.offset_v - previous->offset_v) < 0.25f)
                out.texture.offset_v = lerp(previous->offset_v, call.texture.offset_v, t);
            out.material_color = lerp_color(previous->material_color, call.material_color, t);

            // Vertices the CPU moved (skinned meshes, particles, 2D layout).
            // Texture coordinates stay: blending them would show a mixture of
            // two animation cells. One vertex jumping means the buffer was
            // reused for something else, so then none are blended.
            const auto &before = previous->vertices;
            if (before.size() == call.vertices.size()) {
                const Matrix previous_view_world = multiply(previous->view, previous->world);
                bool motion = true;
                for (std::size_t i = 0; i < before.size() && motion; ++i) {
                    if (before[i].position == call.vertices[i].position) continue;
                    motion = call.through
                        ? small_step({before[i].position[0], before[i].position[1], 0.0f},
                                     {call.vertices[i].position[0], call.vertices[i].position[1], 0.0f}, true)
                        : small_step(transform(previous_view_world, before[i].position),
                                     transform(view_world, call.vertices[i].position), false);
                }
                if (motion) {
                    for (std::size_t i = 0; i < before.size(); ++i) {
                        auto &vertex = out.vertices[i];
                        for (int c = 0; c < 4; ++c) vertex.position[c] = lerp(before[i].position[c], vertex.position[c], t);
                        for (int c = 0; c < 3; ++c) vertex.normal[c] = lerp(before[i].normal[c], vertex.normal[c], t);
                        vertex.color = lerp_color(before[i].color, vertex.color, t);
                    }
                }
            }
            ++stats_.matched;
        }
    }
    current_.push_back(std::move(record));
    return blended;
}

} // namespace sarusaru::gpu
