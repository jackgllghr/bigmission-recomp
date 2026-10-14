#pragma once

// What the launcher's own file browser lists. The system's file dialog may be
// missing (Steam Deck's Game Mode) or unusable with a gamepad, so the disc
// image can always be found with the launcher's browser instead.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sarusaru::ui {

struct FileEntry {
    std::filesystem::path path;
    std::string name;  // UTF-8
    bool directory{};
    std::uintmax_t size{};
};

// A folder's subfolders, then its disc images (.iso, any case), or every
// file with all_files; each group sorted by name ignoring case. Hidden
// entries (starting with a dot) and entries that cannot be read are left out.
// Throws std::filesystem::filesystem_error when the folder cannot be read.
[[nodiscard]] std::vector<FileEntry> list_folder(const std::filesystem::path &folder, bool all_files);

struct Place {
    std::string name;
    std::filesystem::path path;
};
// The usual starting points that exist here: home, desktop, downloads,
// documents, then removable and other drives.
[[nodiscard]] std::vector<Place> places();

// "1.2 GB", "640 KB".
[[nodiscard]] std::string format_size(std::uintmax_t bytes);
[[nodiscard]] std::string utf8(const std::filesystem::path &path);

} // namespace sarusaru::ui
