#pragma once

// Running the build's tools (the code generator, the compiler, curl and tar
// for downloads) and collecting what they print.
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Process;

namespace sarusaru::install {

struct ProcessResult {
    bool started{};
    int exit_code{-1};
    std::string output;  // standard output and error together
    [[nodiscard]] bool ok() const noexcept { return started && exit_code == 0; }
};

// The processes under way, so that cancelling stops them all.
class ProcessGroup {
public:
    // Runs `args` (args[0] the program, by path or on PATH) to the end.
    // Refuses to start once cancelled.
    ProcessResult run(const std::vector<std::string> &args);
    // Kills every process under way and any started later.
    void cancel();
    [[nodiscard]] bool cancelled() const;

private:
    mutable std::mutex mutex_;
    std::vector<SDL_Process *> running_;
    bool cancelled_{};
};

// UTF-8 for a path, as the tools take it on their command lines.
[[nodiscard]] std::string path_argument(const std::filesystem::path &path);

// Shows a command line in a log.
[[nodiscard]] std::string command_line(const std::vector<std::string> &args);

// A program on PATH, as the shell would find it.
[[nodiscard]] std::optional<std::filesystem::path> on_path(std::string_view name);

// A program Windows itself provides (System32), such as curl.exe or tar.exe.
[[nodiscard]] std::filesystem::path system_tool(const char *name);

} // namespace sarusaru::install
