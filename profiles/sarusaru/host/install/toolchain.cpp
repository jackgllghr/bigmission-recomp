#include "install/toolchain.hpp"

#include "install/download.hpp"
#include "install/game_build_config.hpp"
#include "platform/paths.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string_view>
#include <system_error>

namespace sarusaru::install {
namespace {

bool is_file(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::size_t at = 0u;
    while (at < text.size()) {
        const std::size_t end = std::min(text.find(' ', at), text.size());
        if (end > at) out.emplace_back(text.substr(at, end - at));
        at = end + 1u;
    }
    return out;
}

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

// Linux: $CXX, or the first of g++, clang++ and c++ on PATH.
std::optional<std::string> system_compiler() {
    if (const char *cxx = std::getenv("CXX"); cxx != nullptr && *cxx != '\0') {
        if (is_file(cxx)) return std::string(cxx);
        if (auto found = on_path(cxx)) return path_argument(*found);
    }
    for (const char *name : {"g++", "clang++", "c++"})
        if (auto found = on_path(name)) return path_argument(*found);
    return std::nullopt;
}

// Windows: where the pinned llvm-mingw goes, named as its archive.
std::optional<std::filesystem::path> llvm_mingw_directory() {
    auto data = platform::user_data_directory();
    if (!data) return std::nullopt;
    std::string_view name = kLlvmMingwUrl.substr(kLlvmMingwUrl.rfind('/') + 1u);
    if (name.ends_with(".zip")) name.remove_suffix(4u);
    return *data / "toolchain" / std::string(name);
}

std::filesystem::path llvm_mingw_compiler(const std::filesystem::path &directory) {
    return directory / "bin" / "x86_64-w64-mingw32-clang++.exe";
}

// Downloads and unpacks the pinned llvm-mingw.
bool download_llvm_mingw(ProcessGroup &processes, const std::filesystem::path &directory,
                         const std::function<void(std::uint64_t)> &downloaded, std::string &error) {
    const auto root = directory.parent_path();
    std::error_code ignored;
    std::filesystem::create_directories(root, ignored);
    const auto archive = root / (directory.filename().string() + ".zip");
    if (!download_verified(processes, kLlvmMingwUrl, kLlvmMingwSha256, archive, downloaded, error)) return false;
    std::filesystem::remove_all(directory, ignored);
    const auto unpacked = processes.run({path_argument(system_tool("tar.exe")), "-xf", path_argument(archive),
                                         "-C", path_argument(root)});
    std::filesystem::remove(archive, ignored);
    if (!unpacked.ok() || !is_file(llvm_mingw_compiler(directory))) {
        std::filesystem::remove_all(directory, ignored);
        error = "Cannot unpack the compiler: " + trimmed(unpacked.output);
        return false;
    }
    return true;
}

} // namespace

std::optional<Kit> find_kit(std::string &missing) {
    const auto directory = platform::executable_directory();
    const auto shipped = platform::shipped_files_directory();
    const auto libraries = platform::library_directory();
    if (!directory || !shipped || !libraries) {
        missing = "the executable's directory";
        return std::nullopt;
    }
    Kit kit;
#if defined(_WIN32)
    kit.code_generator = *directory / "psp_recomp.exe";
    kit.runtime_library = *shipped / "sdk" / "lib" / "libpsprecomp_runtime.dll.a";
#elif defined(__APPLE__)
    kit.code_generator = *directory / "psp_recomp";
    kit.runtime_library = *libraries / "libpsprecomp_runtime.dylib";
#else
    kit.code_generator = *directory / "psp_recomp";
    kit.runtime_library = *libraries / "libpsprecomp_runtime.so";
#endif
    kit.include = *shipped / "sdk" / "include";
    kit.module_entry = *shipped / "sdk" / "game_module_entry.cpp";
    for (const auto *part : {&kit.code_generator, &kit.runtime_library, &kit.module_entry}) {
        if (!is_file(*part)) {
            missing = path_argument(*part);
            return std::nullopt;
        }
    }
    if (!is_file(kit.include / "psprecomp" / "runtime.hpp") || !is_file(kit.include / "game_abi.hpp")) {
        missing = path_argument(kit.include);
        return std::nullopt;
    }
    return kit;
}

ToolchainCheck check_toolchain(ProcessGroup &processes) {
    ToolchainCheck check;
    if (kToolchain == "apple") {
        const bool ready = processes.run({"/usr/bin/xcode-select", "-p"}).ok() &&
                           processes.run({"/usr/bin/xcrun", "--find", "clang++"}).ok();
        check.status = ready ? ToolchainCheck::Status::Ready : ToolchainCheck::Status::NeedXcode;
    } else if (kToolchain == "gnu") {
        check.status = system_compiler() ? ToolchainCheck::Status::Ready : ToolchainCheck::Status::NeedCompiler;
    } else if (kToolchain == "llvm-mingw") {
        const auto directory = llvm_mingw_directory();
        if (directory && is_file(llvm_mingw_compiler(*directory))) {
            check.status = ToolchainCheck::Status::Ready;
        } else if (directory) {
            check.status = ToolchainCheck::Status::Download;
            check.download_bytes = std::strtoull(std::string(kLlvmMingwSize).c_str(), nullptr, 10);
        }
    }
    return check;
}

void request_xcode_tools() {
    const char *args[] = {"/usr/bin/xcode-select", "--install", nullptr};
    if (SDL_Process *process = SDL_CreateProcess(args, false)) SDL_DestroyProcess(process);
}

std::optional<Toolchain> prepare_toolchain(ProcessGroup &processes, const Kit &kit,
    const std::function<void(std::uint64_t)> &downloaded, std::string &error) {
    Toolchain toolchain;
    toolchain.compile_flags = {"-std=c++20", "-fvisibility=hidden"};
    for (auto &flag : words(kOptimization)) toolchain.compile_flags.push_back(std::move(flag));
    for (const auto &define : words(kDefines)) toolchain.compile_flags.push_back("-D" + define);
    toolchain.compile_flags.push_back("-I" + path_argument(kit.include));
    toolchain.link_libraries = {path_argument(kit.runtime_library)};

    if (kToolchain == "apple") {
        const auto found = processes.run({"/usr/bin/xcrun", "--find", "clang++"});
        if (!found.ok()) {
            error = "Xcode's clang++ is not installed: " + trimmed(found.output);
            return std::nullopt;
        }
        toolchain.compiler = trimmed(found.output);
        // Called directly, the compiler does not know the SDK; xcrun does.
        const auto sdk = processes.run({"/usr/bin/xcrun", "--show-sdk-path"});
        if (!sdk.ok()) {
            error = "Cannot find the macOS SDK: " + trimmed(sdk.output);
            return std::nullopt;
        }
#if defined(__aarch64__) || defined(__arm64__)
        const std::vector<std::string> target = {"-arch", "arm64"};
#else
        const std::vector<std::string> target = {"-arch", "x86_64"};
#endif
        std::vector<std::string> common = target;
        common.insert(common.end(), {"-isysroot", trimmed(sdk.output)});
        if (!kMacosDeploymentTarget.empty())
            common.push_back("-mmacosx-version-min=" + std::string(kMacosDeploymentTarget));
        toolchain.compile_flags.insert(toolchain.compile_flags.end(), common.begin(), common.end());
        toolchain.link_flags = {"-dynamiclib", "-Wl,-install_name,@rpath/sarusaru_game.dylib"};
        toolchain.link_flags.insert(toolchain.link_flags.end(), common.begin(), common.end());
    } else if (kToolchain == "gnu") {
        const auto compiler = system_compiler();
        if (!compiler) {
            error = "No C++ compiler (g++ or clang++) is installed";
            return std::nullopt;
        }
        toolchain.compiler = *compiler;
        toolchain.compile_flags.push_back("-fPIC");
        toolchain.link_flags = {"-shared"};
    } else if (kToolchain == "llvm-mingw") {
        const auto directory = llvm_mingw_directory();
        if (!directory) {
            error = "No per-user data directory for the compiler";
            return std::nullopt;
        }
        if (!is_file(llvm_mingw_compiler(*directory)) && !download_llvm_mingw(processes, *directory, downloaded, error))
            return std::nullopt;
        toolchain.compiler = path_argument(llvm_mingw_compiler(*directory));
        toolchain.link_flags = {"-shared"};
    } else {
        error = "This build of the port cannot install the game; build it from source with the game module";
        return std::nullopt;
    }
    return toolchain;
}

} // namespace sarusaru::install
