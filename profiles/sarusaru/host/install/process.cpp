#include "install/process.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace sarusaru::install {

ProcessResult ProcessGroup::run(const std::vector<std::string> &args) {
    ProcessResult result;
    std::vector<const char *> argv;
    for (const auto &arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process *process = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (cancelled_) {
            SDL_DestroyProperties(props);
            result.output = "cancelled";
            return result;
        }
        process = SDL_CreateProcessWithProperties(props);
        if (process != nullptr) running_.push_back(process);
    }
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        result.output = std::string("cannot run ") + args.front() + ": " + SDL_GetError();
        return result;
    }
    result.started = true;
    std::size_t size = 0u;
    void *output = SDL_ReadProcess(process, &size, &result.exit_code);
    if (output != nullptr) {
        result.output.assign(static_cast<const char *>(output), size);
        SDL_free(output);
    }
    {
        std::lock_guard lock(mutex_);
        running_.erase(std::remove(running_.begin(), running_.end(), process), running_.end());
    }
    SDL_DestroyProcess(process);
    return result;
}

void ProcessGroup::cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    for (SDL_Process *process : running_) SDL_KillProcess(process, true);
}

bool ProcessGroup::cancelled() const {
    std::lock_guard lock(mutex_);
    return cancelled_;
}

std::string path_argument(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string command_line(const std::vector<std::string> &args) {
    std::string line;
    for (const auto &arg : args) {
        if (!line.empty()) line += ' ';
        const bool quote = arg.empty() || arg.find_first_of(" \t\"") != std::string::npos;
        line += quote ? '"' + arg + '"' : arg;
    }
    return line;
}

std::optional<std::filesystem::path> on_path(std::string_view name) {
    const char *path = std::getenv("PATH");
    if (path == nullptr) return std::nullopt;
#if defined(_WIN32)
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif
    std::string_view rest = path;
    while (!rest.empty()) {
        const std::size_t end = std::min(rest.find(kSeparator), rest.size());
        const std::string_view directory = rest.substr(0u, end);
        rest.remove_prefix(std::min(end + 1u, rest.size()));
        if (directory.empty()) continue;
        const auto candidate = std::filesystem::path(std::string(directory)) / std::string(name);
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return std::nullopt;
}

std::filesystem::path system_tool(const char *name) {
    const char *root = std::getenv("SystemRoot");
    return std::filesystem::path(root != nullptr ? root : "C:\\Windows") / "System32" / name;
}

} // namespace sarusaru::install
