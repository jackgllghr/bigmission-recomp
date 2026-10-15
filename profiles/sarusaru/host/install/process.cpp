#include "install/process.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <system_error>

#if defined(__ANDROID__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace sarusaru::install {

#if defined(__ANDROID__)
ProcessResult ProcessGroup::run(const std::vector<std::string> &args) {
    ProcessResult result;
    std::vector<char *> argv;
    for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    int output[2];
    if (pipe2(output, O_CLOEXEC) != 0) {
        result.output = std::string("cannot run ") + args.front() + ": " + std::strerror(errno);
        return result;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO);
    pid_t pid = -1;
    int error = 0;
    {
        std::lock_guard lock(mutex_);
        if (cancelled_) {
            error = ECANCELED;
        } else {
            // args[0] is always a path here: Android has no tools on PATH.
            error = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
            if (error == 0) running_.push_back(pid);
        }
    }
    posix_spawn_file_actions_destroy(&actions);
    close(output[1]);
    if (error != 0) {
        close(output[0]);
        result.output = error == ECANCELED ? std::string("cancelled")
                                           : std::string("cannot run ") + args.front() + ": " + std::strerror(error);
        return result;
    }
    result.started = true;
    char buffer[4096];
    for (ssize_t length; (length = read(output[0], buffer, sizeof buffer)) != 0;) {
        if (length < 0) {
            if (errno == EINTR) continue;
            break;
        }
        result.output.append(buffer, static_cast<std::size_t>(length));
    }
    close(output[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    {
        std::lock_guard lock(mutex_);
        running_.erase(std::remove(running_.begin(), running_.end(), pid), running_.end());
    }
    return result;
}

void ProcessGroup::cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    for (const int pid : running_) kill(pid, SIGKILL);
}
#else
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
#endif

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
