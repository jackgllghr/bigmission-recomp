#pragma once

// The optional English translation pack. The port ships no translated text:
// a player who chooses English downloads the pack this build was configured
// with (pack_config.hpp), checked by its SHA-256, into the per-user data
// directory. Runs on its own thread; the launcher shows its progress.
#include "install/game_builder.hpp"
#include "install/process.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sarusaru::install {

struct PackSource {
    std::string url;
    std::string sha256;
    std::uint64_t bytes{};
};

// The pack this build offers, or nothing if it was configured without one.
[[nodiscard]] std::optional<PackSource> english_pack_source();

// Where `source` is kept once downloaded: <user data>/translations/, named by
// its hash so a newer pack never mistakes an older one for itself.
[[nodiscard]] std::optional<std::filesystem::path> english_pack_path(const PackSource &source);

class PackDownload {
public:
    PackDownload(PackSource source, std::filesystem::path destination);
    // Cancels a download still under way and waits for it to stop.
    ~PackDownload();
    PackDownload(const PackDownload &) = delete;
    PackDownload &operator=(const PackDownload &) = delete;

    void cancel();
    // Downloading (done of total bytes), then Done, Failed or Cancelled.
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] const std::filesystem::path &destination() const noexcept { return destination_; }

private:
    PackSource source_;
    std::filesystem::path destination_;
    ProcessGroup processes_;
    std::atomic<bool> cancelled_{false};
    mutable std::mutex mutex_;
    Progress progress_;
    std::thread thread_;

    void run();
};

} // namespace sarusaru::install
