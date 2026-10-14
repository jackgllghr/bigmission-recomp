#pragma once

// The C++ compiler that builds the game module on the player's machine, set
// up to match how this build of the port was compiled (game_build_config.hpp):
// Xcode's clang on macOS, the system's g++ or clang++ on Linux, and on
// Windows the pinned llvm-mingw, downloaded into the per-user data directory.
#include "install/process.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sarusaru::install {

struct Toolchain {
    std::string compiler;
    // Before the source and object for one unit: compiler -c <flags> src -o obj.
    std::vector<std::string> compile_flags;
    // compiler <link_flags> -o module <objects> <link_libraries>.
    std::vector<std::string> link_flags;
    std::vector<std::string> link_libraries;
};

// What this machine has for building the game.
struct ToolchainCheck {
    enum class Status {
        Ready,         // a compiler is there
        Download,      // the Windows toolchain will be downloaded first
        NeedXcode,     // macOS: the Command Line Tools are not installed
        NeedCompiler,  // Linux: no g++ or clang++
        Unsupported,   // this build of the port cannot install the game
    };
    Status status{Status::Unsupported};
    std::uint64_t download_bytes{};
};

// Where the port's own files for building are: the code generator, the
// runtime headers and library, beside the executable (sdk/).
struct Kit {
    std::filesystem::path code_generator;
    std::filesystem::path include;
    std::filesystem::path module_entry;
    std::filesystem::path runtime_library;
};

// The kit beside the running executable, or nothing if a part is missing.
[[nodiscard]] std::optional<Kit> find_kit(std::string &missing);

[[nodiscard]] ToolchainCheck check_toolchain(ProcessGroup &processes);

// Asks macOS to offer the Command Line Tools (the system's own dialog).
void request_xcode_tools();

// The toolchain, downloading the Windows one first if need be; `downloaded`
// reports bytes as they arrive. Empty with `error` set on failure.
[[nodiscard]] std::optional<Toolchain> prepare_toolchain(ProcessGroup &processes, const Kit &kit,
    const std::function<void(std::uint64_t)> &downloaded, std::string &error);

} // namespace sarusaru::install
