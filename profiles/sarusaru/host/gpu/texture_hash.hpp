#pragma once

// The identity of a decoded texture, shared by the texture dump and HD
// texture packs: what the game draws, independent of where in guest memory it
// sits or which palette format produced it.
#include "gpu/ge_state.hpp"
#include "gpu/sha256.hpp"

#include <bit>
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

// Adds the `width` x `height` rectangle at the top left of `texels` (rows of
// `stride` texels) to `hasher`, each texel as four bytes, red first.
inline void hash_texels(gpu::Sha256 &hasher, std::span<const std::uint32_t> texels, std::size_t stride,
                        std::size_t width, std::size_t height) {
    for (std::size_t y = 0; y < height; ++y) {
        const auto row = texels.subspan(y * stride, width);
        if constexpr (std::endian::native == std::endian::little) {
            hasher.update({reinterpret_cast<const std::uint8_t *>(row.data()), row.size() * 4u});
        } else {
            for (const auto value : row) {
                const std::uint8_t bytes[4] = {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8u),
                                               static_cast<std::uint8_t>(value >> 16u), static_cast<std::uint8_t>(value >> 24u)};
                hasher.update(bytes);
            }
        }
    }
}

} // namespace sarusaru::gpu
