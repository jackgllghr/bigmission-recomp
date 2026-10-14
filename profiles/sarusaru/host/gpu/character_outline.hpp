#pragma once

#include "gpu/ge_state.hpp"

namespace sarusaru::gpu {

// The GE has no character ID. Weighted meshes with normals identify animated
// models without outlining the static scenery. Opaque depth-writing surfaces
// support an inverted hull; cutouts, effects and screen-space draws do not.
[[nodiscard]] inline bool character_outline(const DrawCall &call) noexcept {
    const bool triangles = call.primitive == PrimitiveType::Triangles ||
        call.primitive == PrimitiveType::TriangleStrip || call.primitive == PrimitiveType::TriangleFan;
    const bool skinned = ((call.vertex_type >> 9u) & 3u) != 0u;
    const bool normals = ((call.vertex_type >> 5u) & 3u) != 0u;
    return triangles && skinned && normals && !call.through && !call.clear_mode &&
        call.depth.test_enabled && call.depth.write_enabled && !call.blend.enabled &&
        (!call.alpha_test.enabled || call.alpha_test.function == 1u);
}

} // namespace sarusaru::gpu
