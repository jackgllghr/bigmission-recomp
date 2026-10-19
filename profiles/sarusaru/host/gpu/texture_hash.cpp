#include "gpu/texture_hash.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <vector>

namespace sarusaru::gpu {

TextureIdentity identify_texture(const TextureState &texture, std::span<const std::uint32_t> texels) {
    if (texels.size() < static_cast<std::size_t>(texture.width) * texture.height) return {};
    TextureIdentity identity;
    identity.width = texture.buffer_width != 0u && texture.buffer_width < texture.width
                         ? static_cast<std::uint16_t>(texture.buffer_width) : texture.width;
    identity.height = texture.height;
    // A version tag and the size, so equal bytes in another shape differ.
    const std::uint8_t header[8] = {'S', 'S', 'H', 'D', static_cast<std::uint8_t>(identity.width),
                                    static_cast<std::uint8_t>(identity.width >> 8u), static_cast<std::uint8_t>(identity.height),
                                    static_cast<std::uint8_t>(identity.height >> 8u)};
    gpu::Sha256 hasher;
    hasher.update(header);
    hash_texels(hasher, texels, texture.width, identity.width, identity.height);
    identity.hash = hasher.hex().substr(0, 32);
    return identity;
}

} // namespace sarusaru::gpu
