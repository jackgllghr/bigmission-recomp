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
    std::vector<std::uint8_t> bytes;
    bytes.reserve(8u + static_cast<std::size_t>(identity.width) * identity.height * 4u);
    // A version tag and the size, so equal bytes in another shape differ.
    for (const char c : std::string_view("SSHD")) bytes.push_back(static_cast<std::uint8_t>(c));
    for (const std::uint16_t value : {identity.width, identity.height}) {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
    }
    for (std::size_t y = 0; y < identity.height; ++y)
        for (std::size_t x = 0; x < identity.width; ++x) {
            const std::uint32_t value = texels[y * texture.width + x];
            for (unsigned shift = 0; shift < 32u; shift += 8u) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    identity.hash = psprecomp::sha256(bytes).substr(0, 32);
    return identity;
}

} // namespace sarusaru::gpu
