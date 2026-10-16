#pragma once

// The game's generated code, as a module loaded at run time. Developer builds
// make it beside the executables from the local corpus; players' launchers
// build it on their own machine from their own disc (install/game_builder.hpp)
// into the per-user data directory. Nothing game-derived ships with the port.
//
// The module and the host share one runtime (the psprecomp_runtime library).
// A module records the runtime interface it was built for, game_module_abi,
// and one built for another is refused. Its identity includes the release version,
// so each updated port rebuilds it.
#include "psprecomp/runtime.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace sarusaru {

// sarusaru_game.dll, sarusaru_game.dylib or sarusaru_game.so.
[[nodiscard]] std::string_view game_module_file_name();

// Where a launcher installs the module, for this port's runtime interface:
// <user data>/game/<abi>/. Nothing when there is no per-user data directory.
[[nodiscard]] std::optional<std::filesystem::path> installed_game_directory();

// The module to load: SARUSARU_GAME_MODULE if set, else one beside the
// executable (a developer build), else the installed one, if they exist.
[[nodiscard]] std::optional<std::filesystem::path> find_game_module();

class GameModule {
public:
    // Loads the module at `path` (throws psprecomp::Error if it cannot, or if
    // it was built for another runtime interface).
    explicit GameModule(const std::filesystem::path &path);
    // Loads find_game_module(), or throws psprecomp::Error saying the game is not installed.
    GameModule();
    ~GameModule();
    GameModule(GameModule &&other) noexcept;
    GameModule &operator=(GameModule &&) = delete;
    GameModule(const GameModule &) = delete;
    GameModule &operator=(const GameModule &) = delete;

    // Registers every generated function with `runtime`, which must not
    // outlive this module.
    void register_functions(psprecomp::Runtime &runtime) const;
    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

private:
    using Register = void (*)(psprecomp::Runtime &);
    std::filesystem::path path_;
    void *handle_{};
    Register register_{};
};

} // namespace sarusaru
