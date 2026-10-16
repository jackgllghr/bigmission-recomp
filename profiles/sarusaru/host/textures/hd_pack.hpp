#pragma once

// An HD texture pack: a directory made on the player's device by upscaling
// textures from their own disc, or a prebuilt one downloaded from apart from
// the port (install/texture_build.hpp). It holds
//
//   pack.ini      format = 1, disc = <disc ID>, scale = 2-8
//   <hash>.png    one image per texture, named by gpu::TextureIdentity
//   wanted.txt    optional: hashes the pack's recipe lists but could not find
//                 on the disc; the game writes them to collected/ when drawn,
//                 for the launcher to upscale next time
//
// Each image is the texture's logical rectangle at `scale` times its size.
// Only the index is read at startup; an image is decoded when the renderer
// first needs that texture, so a pack can be large and still start at once.
#include "gpu/texture_hash.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sarusaru::textures {

struct HdManifest {
    std::string disc_id;
    std::uint32_t scale{};
};

// Parses pack.ini; throws std::runtime_error on anything it does not know.
[[nodiscard]] HdManifest parse_hd_manifest(std::string_view text);
[[nodiscard]] std::string serialize_hd_manifest(const HdManifest &manifest);

class HdTexturePack {
public:
    static constexpr std::string_view kManifestName = "pack.ini";
    static constexpr std::uint32_t kMaxDimension = 4096u;

    // Throws std::runtime_error for a missing or malformed manifest, or a
    // pack made for another disc.
    static HdTexturePack load(const std::filesystem::path &directory, std::string_view disc_id);

    // The replacement for `identity`: identity.width * scale by
    // identity.height * scale RGBA texels (red in the low byte), or nothing
    // when the pack has none or its image is unreadable or the wrong size
    // (reported once on stderr; the original texture is then drawn).
    // Not thread-safe: the renderer's thread only.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> find(const gpu::TextureIdentity &identity) const;

    [[nodiscard]] std::uint32_t scale() const noexcept { return manifest_.scale; }
    [[nodiscard]] std::size_t size() const noexcept { return images_.size(); }
    [[nodiscard]] const std::filesystem::path &directory() const noexcept { return directory_; }
    // The textures to collect while playing (wanted.txt), if any.
    [[nodiscard]] std::optional<std::unordered_set<std::string>> wanted() const;
    static constexpr std::string_view kWantedName = "wanted.txt";
    static constexpr std::string_view kCollectedName = "collected";

private:
    std::filesystem::path directory_;
    HdManifest manifest_;
    std::unordered_set<std::string> images_;
    mutable std::unordered_set<std::string> rejected_;
};

} // namespace sarusaru::textures
