#pragma once

// Running the build's tools (the code generator, the compiler, curl and tar
// for the Windows toolchain) and collecting what they print.
#include <filesystem>
#include <mutex>
#include <string>
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

} // namespace sarusaru::install
