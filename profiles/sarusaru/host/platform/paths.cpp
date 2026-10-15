#include "platform/paths.hpp"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <dlfcn.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#include <vector>
#endif

namespace sarusaru::platform {
namespace {

constexpr const char *kName = "SaruSaruRecomp";

std::optional<std::filesystem::path> from_environment(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

} // namespace

std::optional<std::filesystem::path> user_data_directory() {
#if defined(_WIN32)
    if (auto appdata = from_environment("APPDATA")) return *appdata / kName;
#elif defined(__ANDROID__)
    // The app's private files/ directory: the whole of it is the port's.
    if (const char *files = SDL_GetAndroidInternalStoragePath(); files != nullptr && *files != '\0')
        return std::filesystem::path(files);
#elif defined(__APPLE__)
    if (auto home = from_environment("HOME")) return *home / "Library" / "Application Support" / kName;
#else
    if (auto data = from_environment("XDG_DATA_HOME")) return *data / kName;
    if (auto home = from_environment("HOME")) return *home / ".local" / "share" / kName;
#endif
    return std::nullopt;
}

std::optional<std::filesystem::path> executable_directory() {
    std::filesystem::path path;
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0u) return std::nullopt;
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2u);
    }
    path = buffer;
#elif defined(__APPLE__)
    std::uint32_t size = 0u;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1u, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return std::nullopt;
    path = buffer.data();
#elif defined(__ANDROID__)
    // The process is the system's app_process: this library's own file, in
    // the app's native library directory, stands for the executable.
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&executable_directory), &info) == 0 || info.dli_fname == nullptr)
        return std::nullopt;
    path = info.dli_fname;
#else
    std::error_code link_error;
    path = std::filesystem::read_symlink("/proc/self/exe", link_error);
    if (link_error) return std::nullopt;
#endif
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    return (error ? path : resolved).parent_path();
}

namespace {

// Contents/ of the app bundle the executable is in, if it is in one.
std::optional<std::filesystem::path> bundle_contents([[maybe_unused]] const std::filesystem::path &executable_directory) {
#if defined(__APPLE__)
    const auto contents = executable_directory.parent_path();
    if (executable_directory.filename() == "MacOS" && contents.filename() == "Contents" &&
        contents.parent_path().extension() == ".app")
        return contents;
#endif
    return std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> shipped_files_directory() {
#if defined(__ANDROID__)
    // The APK's assets cannot be opened as files; the activity copies them
    // out and names the copy (SaruSaruActivity.java).
    return from_environment("SARUSARU_SHIPPED_FILES");
#else
    auto directory = executable_directory();
    if (!directory) return std::nullopt;
    if (auto contents = bundle_contents(*directory)) return *contents / "Resources";
    return directory;
#endif
}

std::optional<std::filesystem::path> library_directory() {
    auto directory = executable_directory();
    if (!directory) return std::nullopt;
#if defined(__ANDROID__)
    // Every library of the APK is in its one native library directory.
    return directory;
#else
    if (auto contents = bundle_contents(*directory)) return *contents / "Frameworks";
    return *directory / "lib";
#endif
}

} // namespace sarusaru::platform
