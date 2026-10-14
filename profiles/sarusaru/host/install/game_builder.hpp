#pragma once

// Installs the game on the player's machine: reads the executable from their
// verified disc, generates the C++ for it with the port's code generator,
// compiles that into the game module (game_module.hpp) with the toolchain
// for this platform (toolchain.hpp), and puts the module where the game finds
// it. Runs on its own thread; the launcher shows its progress and can cancel.
#include "game_disc.hpp"
#include "install/process.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace sarusaru::install {

struct Progress {
    enum class Step { Starting, Downloading, Extracting, Generating, Compiling, Linking, Done, Failed, Cancelled };
    Step step{Step::Starting};
    // Downloading: bytes of `total`; Compiling: units of `total`.
    std::uint64_t done{};
    std::uint64_t total{};
    // Failed: why, in English, with where the full log is.
    std::string error;
    [[nodiscard]] bool finished() const noexcept {
        return step == Step::Done || step == Step::Failed || step == Step::Cancelled;
    }
};

class GameBuilder {
public:
    // Starts installing from `iso`, which must be the disc `identity` describes.
    GameBuilder(std::filesystem::path iso, const DiscIdentity &identity, std::uint64_t download_bytes);
    // Cancels an installation still under way and waits for it to stop.
    ~GameBuilder();
    GameBuilder(const GameBuilder &) = delete;
    GameBuilder &operator=(const GameBuilder &) = delete;

    void cancel();
    [[nodiscard]] Progress progress() const;
    // How many units compile at once (from the cores and memory).
    [[nodiscard]] std::uint32_t jobs() const noexcept { return jobs_; }

private:
    std::filesystem::path iso_;
    DiscIdentity identity_;
    std::uint32_t jobs_{1};
    ProcessGroup processes_;
    // Only the player cancels; a failed step stops the others through processes_ too.
    std::atomic<bool> cancelled_{false};
    mutable std::mutex mutex_;
    Progress progress_;
    std::thread thread_;

    void run();
    void install();
    void set(Progress::Step step, std::uint64_t done = 0u, std::uint64_t total = 0u);
};

// Compiler jobs for `cores` logical cores and `memory_mib` of RAM: each
// generated unit can take about 1.5 GiB to compile.
[[nodiscard]] std::uint32_t compile_jobs(int cores, int memory_mib);

} // namespace sarusaru::install
