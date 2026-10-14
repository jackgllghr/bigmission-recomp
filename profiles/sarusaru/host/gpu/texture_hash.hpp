#pragma once

// The identity of a decoded texture, shared by the texture dump and HD
// texture packs: what the game draws, independent of where in guest memory it
// sits or which palette format produced it.
#include "gpu/ge_state.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace sarusaru::gpu {

struct TextureIdentity {
    // The logical size: the GE extent, less the columns past the row stride
    // (those wrap into the next row and are not the texture's own).
    std::uint16_t width{}, height{};
    // The first 128 bits of the SHA-256 of the size and the logical
    // rectangle's RGBA (red first), as 32 lowercase hex digits.
    std::string hash;
};

// `texels` is the decoded texture, `texture.width` texels to a row; an empty
// hash when it holds fewer than the GE extent.
[[nodiscard]] TextureIdentity identify_texture(const TextureState &texture, std::span<const std::uint32_t> texels);

} // namespace sarusaru::gpu
