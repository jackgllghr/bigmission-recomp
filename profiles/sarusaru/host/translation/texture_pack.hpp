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

namespace sarusaru::translation {

// A startup-selected, immutable pack of exact logical-image replacements.
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
};

} // namespace sarusaru::translation
