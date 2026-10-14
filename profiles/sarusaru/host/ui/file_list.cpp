#include "ui/file_list.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace sarusaru::ui {
namespace {

std::string lower(std::string text) {
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool by_name(const FileEntry &a, const FileEntry &b) {
    const auto x = lower(a.name), y = lower(b.name);
    return x != y ? x < y : a.name < b.name;
}

void add_if_folder(std::vector<Place> &out, std::string name, const std::filesystem::path &path) {
    std::error_code error;
    if (!path.empty() && std::filesystem::is_directory(path, error))
        out.push_back({std::move(name), path});
}

// Each folder within `parent` as a place (mounted drives).
void add_children(std::vector<Place> &out, const std::filesystem::path &parent) {
    std::error_code error;
    std::vector<Place> found;
    for (std::filesystem::directory_iterator it(parent, error), end; !error && it != end; it.increment(error)) {
        std::error_code ignored;
        if (it->is_directory(ignored)) found.push_back({utf8(it->path().filename()), it->path()});
    }
    std::sort(found.begin(), found.end(), [](const Place &a, const Place &b) { return a.name < b.name; });
    out.insert(out.end(), found.begin(), found.end());
}

} // namespace

std::string utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::vector<FileEntry> list_folder(const std::filesystem::path &folder, bool all_files) {
    std::vector<FileEntry> folders, files;
    for (const auto &entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied)) {
        FileEntry item;
        item.path = entry.path();
        item.name = utf8(entry.path().filename());
        if (item.name.empty() || item.name.front() == '.') continue;
        std::error_code error;
        item.directory = entry.is_directory(error);
        if (error) continue;
        if (item.directory) {
            folders.push_back(std::move(item));
            continue;
        }
        if (!entry.is_regular_file(error) || error) continue;
        if (!all_files && lower(utf8(entry.path().extension())) != ".iso") continue;
        item.size = entry.file_size(error);
        if (error) item.size = 0u;
        files.push_back(std::move(item));
    }
    std::sort(folders.begin(), folders.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    folders.insert(folders.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
    return folders;
}

std::vector<Place> places() {
    std::vector<Place> out;
#if defined(_WIN32)
    if (const char *profile = std::getenv("USERPROFILE"); profile && *profile) {
        // getenv is in the ANSI code page, which path's narrow constructor expects.
        const std::filesystem::path home(profile);
        add_if_folder(out, "Home", home);
        add_if_folder(out, "Desktop", home / "Desktop");
        add_if_folder(out, "Downloads", home / "Downloads");
        add_if_folder(out, "Documents", home / "Documents");
    }
    for (char drive = 'A'; drive <= 'Z'; ++drive) {
        const std::string root = std::string(1, drive) + ":\\";
        add_if_folder(out, root, root);
    }
#else
    if (const char *home = std::getenv("HOME"); home && *home) {
        const std::filesystem::path path(home);
        add_if_folder(out, "Home", path);
        add_if_folder(out, "Desktop", path / "Desktop");
        add_if_folder(out, "Downloads", path / "Downloads");
        add_if_folder(out, "Documents", path / "Documents");
    }
#if defined(__APPLE__)
    add_children(out, "/Volumes");
#elif defined(__ANDROID__)
    add_if_folder(out, "Storage", "/storage/emulated/0");
#else
    if (const char *user = std::getenv("USER"); user && *user) {
        add_children(out, std::filesystem::path("/run/media") / user);
        add_children(out, std::filesystem::path("/media") / user);
    }
    add_children(out, "/mnt");
    add_if_folder(out, "Computer", "/");
#endif
#endif
    return out;
}

std::string format_size(std::uintmax_t bytes) {
    constexpr const char *kUnits[] = {"bytes", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1000.0 && unit + 1 < std::size(kUnits)) {
        value /= 1000.0;
        ++unit;
    }
    char text[32];
    if (unit == 0)
        std::snprintf(text, sizeof text, "%ju bytes", bytes);
    else
        std::snprintf(text, sizeof text, value < 10.0 ? "%.1f %s" : "%.0f %s", value, kUnits[unit]);
    return text;
}

} // namespace sarusaru::ui
