#pragma once

// Finding a texture on the player's own disc, so HD textures can be made
// without playing through the game first. A texture recipe names where each
// texture lives as a source locator:
//
//   PSP_GAME/USRDIR/stage/st01.lzs!lzs|tex.pac|ground.tm2#1a40
//
// a disc path, then steps applied in order to the bytes so far (`!lzs`
// decompresses them, `|NAME` takes a member of a PAC archive), then `#` and
// the hex offset of the TIM2 picture in the result. The picture is decoded as
// the GE would draw it, and its gpu::TextureIdentity must be the recipe's
// hash, so a locator that points at anything else is never upscaled.
//
// The formats are those of tools/translation_assets.py; variants not seen on
// this disc are rejected rather than guessed.
#include "gpu/texture_hash.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sarusaru::umd { class DiscImage; }

namespace sarusaru::textures {

inline constexpr std::size_t kMaxAssetBytes = 32u * 1024u * 1024u;

// LZSS with a 4 KiB ring (the disc's .lzs). Throws std::runtime_error.
[[nodiscard]] std::vector<std::uint8_t> decode_lzs(std::span<const std::uint8_t> data);
// A PAC archive's members by name, as views into `data`. Throws std::runtime_error.
[[nodiscard]] std::map<std::string, std::span<const std::uint8_t>> pac_members(std::span<const std::uint8_t> data);

struct Picture {
    std::uint16_t width{}, height{};
    // RGBA, red in the low byte, `width` to a row: what the GE decodes.
    std::vector<std::uint32_t> texels;
    [[nodiscard]] gpu::TextureIdentity identity() const;
};
// The first picture (top mip level) of a TIM2 file at the start of `data`:
// 4- or 8-bit indexed, swizzled, with a 32-bit palette. Nothing for other
// layouts. Throws std::runtime_error for a truncated or inconsistent file.
[[nodiscard]] std::optional<Picture> decode_tim2(std::span<const std::uint8_t> data);

struct SourceLocator {
    struct Step {
        enum class Kind { Lzs, Member } kind{};
        std::string member;
        bool operator==(const Step &) const = default;
    };
    std::string path;
    std::vector<Step> steps;
    std::uint32_t offset{};
    bool operator==(const SourceLocator &) const = default;
};
// Throws std::runtime_error for anything malformed.
[[nodiscard]] SourceLocator parse_locator(std::string_view text);
[[nodiscard]] std::string format_locator(const SourceLocator &locator);

// Reads and decodes the picture `locator` names. Throws std::runtime_error
// when the disc does not have it or it does not decode.
[[nodiscard]] Picture read_picture(umd::DiscImage &disc, const SourceLocator &locator);

// Every TIM2 picture in the files under `directory` (a disc path), looking
// inside .lzs files and PAC archives, decoded, with where it was found.
// Pictures whose height is not a power of two are left out: the GE draws them
// with rows of other data below, so their identity cannot be known from the
// disc. `found` returns false to stop. Files that do not decode are skipped.
void scan_disc(umd::DiscImage &disc, std::string_view directory,
               const std::function<bool(const SourceLocator &, Picture &&)> &found);

// Reads each disc file once for many locators: keeps the last file's bytes
// and the containers opened in it.
class DiscTextureReader {
public:
    explicit DiscTextureReader(umd::DiscImage &disc) : disc_(disc) {}
    [[nodiscard]] Picture read(const SourceLocator &locator);

private:
    umd::DiscImage &disc_;
    std::string path_;
    std::vector<std::uint8_t> file_;
    // Decompressed bytes by the locator prefix that produced them.
    std::map<std::string, std::vector<std::uint8_t>> expanded_;
};

} // namespace sarusaru::textures
