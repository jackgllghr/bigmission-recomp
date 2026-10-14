#pragma once

// Texture dump mode (--dump-textures DIR): every texture the renderer decodes
// is written once, by its identity, as DIR/<hash>.png (the logical rectangle,
// RGBA), with a line in DIR/textures.tsv describing how the GE drew it. A
// later run into the same directory adds only textures it has not seen, so a
// play-through can be collected over several sessions. This is the input to
// the HD texture recipe and to on-device upscaling.
#include "gpu/ge_state.hpp"
#include "gpu/texture_hash.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>

namespace sarusaru::textures {

class TextureDump {
public:
    // Creates `directory` if needed and reads the index already there.
    // With `wanted`, only those hashes are written (an HD pack collecting the
    // textures its recipe cannot find on the disc).
    explicit TextureDump(std::filesystem::path directory, std::optional<std::unordered_set<std::string>> wanted = {});

    // Writes the texture unless its hash is already in the dump. A failed
    // write is reported once on stderr and does not stop the game.
    void record(const gpu::TextureState &texture, const gpu::TextureIdentity &identity,
                std::span<const std::uint32_t> texels);
    [[nodiscard]] std::size_t written() const noexcept { return written_; }
    [[nodiscard]] std::size_t known() const noexcept { return seen_.size(); }
    [[nodiscard]] const std::filesystem::path &directory() const noexcept { return directory_; }

    static constexpr std::string_view kIndexName = "textures.tsv";
    static constexpr std::string_view kIndexHeader =
        "hash\twidth\theight\tge_width\tge_height\tbuffer_width\tformat\tswizzled";

private:
    std::filesystem::path directory_;
    std::unordered_set<std::string> seen_;
    std::optional<std::unordered_set<std::string>> wanted_;
    std::size_t written_{};
    bool failed_{};
};

// Writes RGBA texels (red in the low byte), `width` to a row, as a PNG.
bool write_png(const std::filesystem::path &path, std::span<const std::uint32_t> texels, std::uint32_t width,
               std::uint32_t height, std::uint32_t stride);

} // namespace sarusaru::textures
