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
// Decoded images are kept in decoded/ (zstd-compressed texels, several times
// faster to read than the PNG), so later runs have them in time to be drawn
// the first time they are needed; a changed PNG is decoded again.
#include "gpu/texture_hash.hpp"

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
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
    // Safe on any thread: the renderer decodes images on worker threads.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> find(const gpu::TextureIdentity &identity) const;
    // Whether the pack has an image for `identity` (which find() may still
    // reject); cheap.
    [[nodiscard]] bool contains(const gpu::TextureIdentity &identity) const {
        return !identity.hash.empty() && images_.contains(identity.hash);
    }
    // Whether any image is for a texture of this logical size (from the
    // images' PNG headers, read at load): when not, the renderer need not
    // hash the texture to look for one.
    [[nodiscard]] bool may_contain(std::uint32_t width, std::uint32_t height) const {
        return any_size_ || sizes_.contains(width << 16u | height);
    }

    [[nodiscard]] std::uint32_t scale() const noexcept { return manifest_.scale; }
    [[nodiscard]] std::size_t size() const noexcept { return images_.size(); }
    [[nodiscard]] const std::filesystem::path &directory() const noexcept { return directory_; }
    // The textures to collect while playing (wanted.txt), if any.
    [[nodiscard]] std::optional<std::unordered_set<std::string>> wanted() const;
    static constexpr std::string_view kWantedName = "wanted.txt";
    static constexpr std::string_view kDecodedName = "decoded";
    static constexpr std::string_view kCollectedName = "collected";

private:
    std::filesystem::path directory_;
    HdManifest manifest_;
    std::unordered_set<std::string> images_;
    std::unordered_set<std::uint32_t> sizes_;
    bool any_size_{};  // an image whose size could not be read
    // Shared so the pack stays movable.
    struct Rejections {
        std::mutex mutex;
        std::unordered_set<std::string> hashes;
    };
    std::shared_ptr<Rejections> rejected_ = std::make_shared<Rejections>();
    // Set once writing decoded/ fails (a read-only pack), not to try again.
    std::shared_ptr<std::atomic<bool>> cache_failed_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace sarusaru::textures
