#pragma once

#include "gpu/ge_state.hpp"
#include "psprecomp/elf32.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
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
    // Whether the file at `path` is a pack in a format this version reads
    // (its content is checked only by load()).
    [[nodiscard]] static bool readable_format(const std::filesystem::path &path);

    // Called only on a renderer cache miss for a non-render-target texture.
    // Unmatched images retain their Japanese pixels. Only the logical source
    // rectangle is hashed: PSP power-of-two extents can contain unrelated bytes.
    bool apply(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels) const;
    // HD art for the untranslated texture (an HD pack image): its logical
    // rectangle, width x height texels, at `scale` times that size.
    struct BaseArt {
        std::span<const std::uint32_t> texels;
        std::uint32_t width{}, height{}, scale{};
    };
    // Returns the output pixel scale (zero on no match). Only authored regions
    // gain HD lettering; guest UV/layout remain unchanged. Outside the regions
    // the source pixels are enlarged, or sampled from `base` when given.
    std::uint32_t apply_scaled(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels,
                               std::uint32_t requested_scale, const BaseArt *base = nullptr) const;
    // Validates every read-only string slot before modifying any guest data.
    // Called once after executable verification/relocation and before execution.
    void apply_strings(psprecomp::GuestMemory &memory, const psprecomp::Elf32Image &elf,
                       std::uint32_t load_base) const;
    [[nodiscard]] std::size_t string_count() const noexcept { return strings_.size(); }
    using DialogueAllocator = std::function<std::uint32_t(std::uint32_t)>;
    // Reserve session-owned guest memory through the kernel, then redirect only
    // source-verified message lookup slots. Original encoded messages stay intact.
    void apply_dialogue(psprecomp::GuestMemory &memory, const psprecomp::Elf32Image &elf,
                        std::uint32_t load_base, const DialogueAllocator &allocate) const;
    [[nodiscard]] std::size_t dialogue_count() const noexcept { return dialogue_.size(); }
    using DrawSink = std::function<void(const gpu::DrawCall &)>;
    void translate_draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call, const DrawSink &emit);
    void flush_draws(const DrawSink &emit);
    // Reserved host-only IDs are generated at runtime, never read from guest assets.
    [[nodiscard]] bool host_texture(std::uint32_t address) const noexcept;
    [[nodiscard]] std::vector<std::uint8_t> host_rgba(std::uint32_t address) const;
    // Decoded on each call: the renderer uploads a line once and keeps its texture.
    struct HostImage {
        std::vector<std::uint8_t> rgba;
        std::uint32_t width{}, height{};
    };
    [[nodiscard]] HostImage host_image(std::uint32_t address, std::uint32_t requested_scale) const;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::string &locale() const noexcept { return locale_; }
    // The movies' English subtitles as SubRip text, by movie name
    // (movie::subtitle_key); empty before version 6.
    [[nodiscard]] const std::map<std::string, std::string> &subtitles() const noexcept { return subtitles_; }

private:
    struct Region { std::uint16_t x{}, y{}, width{}, height{}; };
    struct Entry {
        std::string id, source_hash;
        std::uint16_t width{}, height{}, ge_width{}, ge_height{}, buffer_width{};
        std::vector<std::uint8_t> rgba;
        std::vector<Region> regions;
        std::uint16_t hd_scale{};
        std::vector<std::uint8_t> hd_runs;
    };
    std::string locale_;
    bool padded_strings_{};
    std::vector<Entry> entries_;
    struct StringEntry {
        std::string id, source_hash, replacement;
        std::uint32_t offset{}, capacity{};
    };
    std::vector<StringEntry> strings_;
    struct DialogueEntry {
        std::string id, source_hash;
        std::uint32_t pointer_offset{}, source_offset{}, source_size{};
        std::vector<std::uint8_t> encoded;
    };
    std::vector<DialogueEntry> dialogue_;
    // Lines stay as pixel runs (u16 count, u32 RGBA) until drawn.
    struct GlyphRun {
        std::string id, font_hash, signature;
        std::uint16_t width{}, height{}, glyph_count{}, image_width{};
        std::vector<std::uint8_t> rgba_runs;
        std::uint16_t hd_scale{};
        std::vector<std::uint8_t> hd_runs;
    };
    static constexpr std::uint32_t kHostTextureBase = 0xFFF00000u, kHostTextureStride = 0x100u;
    static constexpr std::uint32_t kMaxGlyphRuns = (0u - kHostTextureBase) / kHostTextureStride;
    std::vector<GlyphRun> runs_;
    std::unordered_map<std::string, std::size_t> run_index_;
    std::vector<gpu::DrawCall> pending_;
    std::string pending_font_;
    std::unordered_map<std::string, std::string> fonts_;
    std::map<std::string, std::string> subtitles_;
};

} // namespace sarusaru::translation
