#pragma once
#include "save_state/ppst.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace sarusaru::save_state {
inline constexpr std::uint32_t kSlotCount = 5;
inline constexpr std::uint32_t kThumbnailWidth = 240, kThumbnailHeight = 136;
struct Thumbnail {
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> rgba;
    bool operator==(const Thumbnail &) const = default;
};
struct SlotInfo {
    bool occupied{};
    std::int64_t saved_at{}; // Unix seconds; zero for an unavailable timestamp.
    Thumbnail thumbnail;
};
// Slots are numbered 1..5. Slot 1 retains the original quick-state filename.
[[nodiscard]] std::filesystem::path slot_path(const std::filesystem::path &settings, std::uint32_t slot);
[[nodiscard]] inline std::filesystem::path quick_state_path(const std::filesystem::path &settings) { return slot_path(settings, 1); }
// Reads only the bounded preview header, never the compressed guest state.
[[nodiscard]] SlotInfo slot_info(const std::filesystem::path &path);
// One atomic file contains the preview and the original compressed container.
void write_slot(const std::filesystem::path &path, const PpstFile &state, const Thumbnail &thumbnail);
// Accepts both preview-bearing slots and older raw native containers.
[[nodiscard]] PpstFile read_slot(const std::filesystem::path &path);
}
