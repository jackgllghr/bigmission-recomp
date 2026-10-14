#pragma once

#include "gpu/ge_state.hpp"
#include "psprecomp/elf32.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>

namespace sarusaru::translation {

// A startup-selected pack with immutable content and transient glyph-run state.
// No guest addresses or renderer cache keys are persisted in a pack.
class TexturePack {
public:
    using SourceReader = std::function<std::vector<std::uint8_t>(std::string_view)>;
    static TexturePack load(const std::filesystem::path &path, std::string_view disc_id,
                            std::string_view executable_hash, const SourceReader &read_source);

    // Called only on a renderer cache miss for a non-render-target texture.
    // Unmatched images retain their Japanese pixels. Only the logical source
    // rectangle is hashed: PSP power-of-two extents can contain unrelated bytes.
    bool apply(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels) const;
    // Validates every read-only string slot before modifying any guest data.
    // Called once after executable verification/relocation and before execution.
    void apply_strings(psprecomp::GuestMemory &memory, const psprecomp::Elf32Image &elf,
                       std::uint32_t load_base) const;
    [[nodiscard]] std::size_t string_count() const noexcept { return strings_.size(); }
    using DrawSink = std::function<void(const gpu::DrawCall &)>;
    void translate_draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call, const DrawSink &emit);
    void flush_draws(const DrawSink &emit);
    // Reserved host-only IDs are generated at runtime, never read from guest assets.
    [[nodiscard]] std::span<const std::uint8_t> host_rgba(std::uint32_t address) const;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::string &locale() const noexcept { return locale_; }

private:
    struct Region { std::uint16_t x{}, y{}, width{}, height{}; };
    struct Entry {
        std::string id, source_hash;
        std::uint16_t width{}, height{}, ge_width{}, ge_height{}, buffer_width{};
        std::vector<std::uint8_t> rgba;
        std::vector<Region> regions;
    };
    std::string locale_;
    std::vector<Entry> entries_;
    struct StringEntry {
        std::string id, source_hash, replacement;
        std::uint32_t offset{}, capacity{};
    };
    std::vector<StringEntry> strings_;
    struct GlyphRun {
        std::string id, font_hash, signature;
        std::uint16_t width{}, height{}, glyph_count{};
        std::vector<std::uint8_t> rgba;
    };
    static constexpr std::uint32_t kHostTextureBase = 0xFFF00000u;
    std::vector<GlyphRun> runs_;
    std::vector<gpu::DrawCall> pending_;
    std::string pending_font_;
    std::unordered_map<std::string, std::string> fonts_;
};

} // namespace sarusaru::translation
