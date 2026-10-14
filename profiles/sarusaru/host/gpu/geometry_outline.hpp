#pragma once

#include "gpu/ge_state.hpp"

#include <cmath>
#include <map>
#include <span>

namespace sarusaru::gpu {

// Depth-writing 3D surfaces support outlines, including characters,
// static scenery and the game’s standard alpha-over blend.
// Cutouts, additive effects and screen-space draws do not.
[[nodiscard]] inline bool geometry_outline(const DrawCall &call) noexcept {
    const bool triangles = call.primitive == PrimitiveType::Triangles ||
        call.primitive == PrimitiveType::TriangleStrip || call.primitive == PrimitiveType::TriangleFan;
    const bool alpha_over = call.blend.source_factor == 2u &&
        call.blend.destination_factor == 3u && call.blend.equation == 0u;
    return triangles && !call.through && !call.clear_mode &&
        call.depth.test_enabled && call.depth.write_enabled && (!call.blend.enabled || alpha_over) &&
        (!call.alpha_test.enabled || call.alpha_test.function == 1u);
}

// Boundary, silhouette and sharp crease edges of an expanded triangle list.
// Shared planar edges are omitted so the world does not become a wireframe.
// `facing` supplies each triangle's projected signed area, for silhouettes.
[[nodiscard]] inline std::vector<std::array<std::size_t, 2>> outline_edges(
    std::span<const std::array<float, 3>> positions, std::span<const float> facing) {
    using Position = std::array<float, 3>;
    struct Edge { std::array<std::size_t, 2> indices; Position normal; float facing; unsigned count{1}; bool contour{}; };
    std::map<std::array<Position, 2>, Edge> edges;
    for (std::size_t i = 0; i + 2u < positions.size(); i += 3u) {
        const auto &a = positions[i], &b = positions[i + 1u], &c = positions[i + 2u];
        bool finite = true;
        for (const auto *p : {&a, &b, &c})
            for (const float component : *p) finite = finite && std::isfinite(component);
        if (!finite) continue;
        Position ab{}, ac{};
        for (std::size_t axis = 0; axis < 3u; ++axis) { ab[axis] = b[axis] - a[axis]; ac[axis] = c[axis] - a[axis]; }
        Position normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
        const float length = std::hypot(normal[0], normal[1], normal[2]);
        if (!(length > 0.0f) || !std::isfinite(length)) continue;
        for (float &component : normal) component /= length;
        const float face = i / 3u < facing.size() ? facing[i / 3u] : 1.0f;
        for (const auto pair : {std::array{i, i + 1u}, std::array{i + 1u, i + 2u}, std::array{i + 2u, i}}) {
            std::array<Position, 2> key{positions[pair[0]], positions[pair[1]]};
            if (key[1] < key[0]) std::swap(key[0], key[1]);
            const auto [found, inserted] = edges.try_emplace(key, Edge{pair, normal, face});
            if (!inserted) {
                auto &edge = found->second;
                ++edge.count;
                const float dot = edge.normal[0] * normal[0] + edge.normal[1] * normal[1] + edge.normal[2] * normal[2];
                edge.contour = edge.contour || dot < 0.5f || (edge.facing * face < 0.0f);
            }
        }
    }
    std::vector<std::array<std::size_t, 2>> result;
    for (const auto &[position, edge] : edges)
        if (edge.count == 1u || edge.contour) result.push_back(edge.indices);
    return result;
}

} // namespace sarusaru::gpu
