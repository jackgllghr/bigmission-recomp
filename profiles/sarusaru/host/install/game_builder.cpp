#include "install/game_builder.hpp"

#include "game_identity.hpp"
#include "game_module.hpp"
#include "install/toolchain.hpp"
#include "psprecomp/common.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sarusaru::install {
namespace {

// Thrown to stop an installation that was cancelled.
struct Cancelled {};

std::string last_lines(const std::string &text, std::size_t count) {
    std::size_t at = text.size();
    for (std::size_t lines = 0u; at > 0u && lines <= count; --at)
        if (text[at - 1u] == '\n') ++lines;
    return text.substr(at);
}

class Log {
public:
    explicit Log(const std::filesystem::path &path) : path_(path), out_(path, std::ios::binary | std::ios::trunc) {}
    void command(const std::vector<std::string> &args, const ProcessResult &result) {
        std::lock_guard lock(mutex_);
        out_ << "$ " << command_line(args) << '\n' << result.output;
        if (!result.output.empty() && result.output.back() != '\n') out_ << '\n';
        out_ << "(exit " << result.exit_code << ")\n\n";
        out_.flush();
    }
    void line(const std::string &text) {
        std::lock_guard lock(mutex_);
        out_ << text << '\n';
        out_.flush();
    }
    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::ofstream out_;
    std::mutex mutex_;
};

} // namespace

std::uint32_t compile_jobs(int cores, int memory_mib) {
    const std::uint32_t by_cores = cores > 0 ? static_cast<std::uint32_t>(cores) : 1u;
    const std::uint32_t by_memory = memory_mib > 0 ? static_cast<std::uint32_t>(memory_mib) / 1536u : 1u;
    return std::clamp(std::min(by_cores, by_memory), 1u, 32u);
}

GameBuilder::GameBuilder(std::filesystem::path iso, const DiscIdentity &identity, std::uint64_t download_bytes)
    : iso_(std::move(iso)), identity_(identity), jobs_(compile_jobs(SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM())) {
    progress_.total = download_bytes;
    thread_ = std::thread([this] { run(); });
}

GameBuilder::~GameBuilder() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void GameBuilder::cancel() {
    cancelled_ = true;
    processes_.cancel();
}

Progress GameBuilder::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

void GameBuilder::set(Progress::Step step, std::uint64_t done, std::uint64_t total) {
    if (cancelled_) throw Cancelled{};
    std::lock_guard lock(mutex_);
    progress_.step = step;
    progress_.done = done;
    progress_.total = total;
}

void GameBuilder::run() {
    try {
        install();
    } catch (const Cancelled &) {
        std::lock_guard lock(mutex_);
        progress_.step = Progress::Step::Cancelled;
    } catch (const std::exception &failure) {
        std::lock_guard lock(mutex_);
        progress_.step = cancelled_ ? Progress::Step::Cancelled : Progress::Step::Failed;
        progress_.error = failure.what();
    }
}

void GameBuilder::install() {
    std::string missing;
    const auto kit = find_kit(missing);
    if (!kit) throw psprecomp::Error("This copy of the port is incomplete (no " + missing + "); download it again");
    const auto target = installed_game_directory();
    if (!target) throw psprecomp::Error("Cannot determine a per-user data directory to install the game in");
    const auto work = *target / "build";
    std::error_code ignored;
    std::filesystem::remove_all(work, ignored);
    std::filesystem::create_directories(work / "obj");
    Log log(*target / "install.log");
    const auto fail = [&log](const std::string &what, const ProcessResult &result) {
        throw psprecomp::Error(what + ":\n" + last_lines(result.output, 12) + "\nThe full log is " +
                               path_argument(log.path()));
    };
    const auto run = [this, &log](const std::vector<std::string> &args) {
        auto result = processes_.run(args);
        log.command(args, result);
        if (cancelled_) throw Cancelled{};
        return result;
    };

    // The compiler first: on Windows it may need downloading.
    set(Progress::Step::Downloading, 0u, progress().total);
    std::string error;
    const auto toolchain = prepare_toolchain(processes_, *kit, [this](std::uint64_t bytes) {
        std::lock_guard lock(mutex_);
        progress_.done = bytes;
    }, error);
    if (cancelled_) throw Cancelled{};
    if (!toolchain) throw psprecomp::Error(error);
    log.line("compiler: " + toolchain->compiler);

    // The executable, from the verified disc only.
    set(Progress::Step::Extracting);
    const auto executable = work / "BOOT.elf";
    {
        const auto elf = executable_from_disc(iso_, identity_);
        std::ofstream out(executable, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(elf.bytes().data()), static_cast<std::streamsize>(elf.bytes().size()));
        if (!out) throw psprecomp::Error("Cannot write " + path_argument(executable));
    }

    set(Progress::Step::Generating);
    const auto generated = work / "generated";
    if (const auto result = run({path_argument(kit->code_generator), path_argument(executable), "--auto",
                                 path_argument(generated), psprecomp::hex32(load_base), psprecomp::hex32(unit_span)});
        !result.ok())
        fail("Generating the game's code failed", result);
    std::filesystem::remove(executable, ignored);

    // Every unit, then the module's interface.
    std::vector<std::filesystem::path> sources;
    for (const auto &entry : std::filesystem::directory_iterator(generated)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("generated_") && entry.path().extension() == ".cpp") sources.push_back(entry.path());
    }
    std::sort(sources.begin(), sources.end());
    if (sources.empty()) throw psprecomp::Error("The code generator produced no code");
    sources.push_back(kit->module_entry);

    const auto total = static_cast<std::uint64_t>(sources.size());
    set(Progress::Step::Compiling, 0u, total);
    std::vector<std::string> objects(sources.size());
    std::atomic<std::size_t> next{0u};
    std::atomic<std::uint64_t> compiled{0u};
    std::mutex failure_mutex;
    std::optional<std::pair<std::string, ProcessResult>> failure;
    const auto compile = [&] {
        for (std::size_t index = next++; index < sources.size(); index = next++) {
            objects[index] = path_argument(work / "obj" / (sources[index].stem().string() + ".o"));
            std::vector<std::string> args{toolchain->compiler, "-c"};
            args.insert(args.end(), toolchain->compile_flags.begin(), toolchain->compile_flags.end());
            args.insert(args.end(), {path_argument(sources[index]), "-o", objects[index]});
            auto result = processes_.run(args);
            log.command(args, result);
            if (!result.ok()) {
                std::lock_guard lock(failure_mutex);
                if (!failure && !cancelled_)
                    failure.emplace("Compiling " + sources[index].filename().string() + " failed", std::move(result));
                processes_.cancel();
                return;
            }
            const auto done = ++compiled;
            std::lock_guard lock(mutex_);
            progress_.done = done;
        }
    };
    {
        std::vector<std::thread> workers;
        for (std::uint32_t i = 0u; i < std::min<std::uint64_t>(jobs_, total); ++i) workers.emplace_back(compile);
        for (auto &worker : workers) worker.join();
    }
    if (cancelled_) throw Cancelled{};
    if (failure) fail(failure->first, failure->second);

    set(Progress::Step::Linking);
    const auto built = work / std::string(game_module_file_name());
    std::vector<std::string> args{toolchain->compiler};
    args.insert(args.end(), toolchain->link_flags.begin(), toolchain->link_flags.end());
    args.insert(args.end(), {"-o", path_argument(built)});
    args.insert(args.end(), objects.begin(), objects.end());
    args.insert(args.end(), toolchain->link_libraries.begin(), toolchain->link_libraries.end());
    if (const auto result = run(args); !result.ok()) fail("Linking the game failed", result);

    // The module must load and be for this port before it replaces any other.
    { const GameModule check(built); }
    const auto installed = *target / std::string(game_module_file_name());
    std::filesystem::remove(installed, ignored);
    std::filesystem::rename(built, installed);
    std::filesystem::remove_all(work, ignored);
    log.line("installed " + path_argument(installed));
    set(Progress::Step::Done);
}

} // namespace sarusaru::install
