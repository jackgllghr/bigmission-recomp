#include "game_module.hpp"

#include "game_abi.hpp"
#include "platform/paths.hpp"
#include "psprecomp/common.hpp"

#include <cstdlib>
#include <string>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sarusaru {
namespace {

#if defined(_WIN32)
void *open_library(const std::filesystem::path &path, std::string &error) {
    // Its dependencies (the runtime, the C++ library) are already loaded by the host.
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) error = "error " + std::to_string(GetLastError());
    return reinterpret_cast<void *>(module);
}
void *find_symbol(void *library, const char *name) {
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), name));
}
void close_library(void *library) { FreeLibrary(static_cast<HMODULE>(library)); }
#else
void *open_library(const std::filesystem::path &path, std::string &error) {
    void *library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char *reason = dlerror();
        error = reason != nullptr ? reason : "unknown error";
    }
    return library;
}
void *find_symbol(void *library, const char *name) { return dlsym(library, name); }
void close_library(void *library) { dlclose(library); }
#endif

bool is_file(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

} // namespace

std::string_view game_module_file_name() {
#if defined(_WIN32)
    return "sarusaru_game.dll";
#elif defined(__APPLE__)
    return "sarusaru_game.dylib";
#else
    return "sarusaru_game.so";
#endif
}

std::optional<std::filesystem::path> installed_game_directory() {
    auto data = platform::user_data_directory();
    if (!data) return std::nullopt;
    return *data / "game" / std::string(game_module_abi);
}

std::optional<std::filesystem::path> find_game_module() {
    if (const char *path = std::getenv("SARUSARU_GAME_MODULE"); path != nullptr && *path != '\0') return path;
    if (auto directory = platform::executable_directory(); directory && is_file(*directory / game_module_file_name()))
        return *directory / game_module_file_name();
    if (auto directory = installed_game_directory(); directory && is_file(*directory / game_module_file_name()))
        return *directory / game_module_file_name();
    return std::nullopt;
}

GameModule::GameModule()
    : GameModule([] {
          auto path = find_game_module();
          if (!path) throw psprecomp::Error("The game is not installed yet: start the launcher to install it from your disc");
          return *path;
      }()) {}

GameModule::GameModule(const std::filesystem::path &path) : path_(path) {
    std::string error;
    handle_ = open_library(path, error);
    if (handle_ == nullptr) throw psprecomp::Error("Cannot load the game module " + path.string() + ": " + error);
    using Abi = const char *(*)();
    const auto abi = reinterpret_cast<Abi>(find_symbol(handle_, "sarusaru_game_abi"));
    register_ = reinterpret_cast<Register>(find_symbol(handle_, "sarusaru_register_game"));
    if (abi == nullptr || register_ == nullptr) {
        close_library(handle_);
        throw psprecomp::Error("Not a SaruSaru game module: " + path.string());
    }
    if (std::string_view(abi()) != game_module_abi) {
        close_library(handle_);
        throw psprecomp::Error("The game module " + path.string() +
                               " was built for another version of the port; install the game again");
    }
}

GameModule::GameModule(GameModule &&other) noexcept
    : path_(std::move(other.path_)), handle_(std::exchange(other.handle_, nullptr)),
      register_(std::exchange(other.register_, nullptr)) {}

GameModule::~GameModule() {
    if (handle_ != nullptr) close_library(handle_);
}

void GameModule::register_functions(psprecomp::Runtime &runtime) const { register_(runtime); }

} // namespace sarusaru
