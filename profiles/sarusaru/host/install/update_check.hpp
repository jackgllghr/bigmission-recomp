#pragma once

#include "install/process.hpp"
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace sarusaru::install {
inline constexpr std::string_view kReleasePage = "https://github.com/jackgllghr/bigmission-recomp/releases/latest";
inline constexpr std::string_view kUpdateFeed = "https://github.com/jackgllghr/bigmission-recomp/releases/latest/download/update-info.txt";
inline constexpr std::string_view kObtainium = "obtainium://add/https://github.com/jackgllghr/bigmission-recomp";
using ReleaseVersion = std::array<std::uint32_t, 3>;
[[nodiscard]] std::optional<ReleaseVersion> parse_release_version(std::string_view text);
[[nodiscard]] std::string_view update_platform();
struct UpdateResult {
    enum class State { Checking, Current, Available, Unsupported, Failed };
    State state{State::Checking};
    std::string version, download_url;
};
// A strict, bounded release feed. Links are restricted to this repository's
// exact tagged platform package. This check never downloads or executes code.
[[nodiscard]] UpdateResult parse_update_feed(std::string_view feed, std::string_view installed, std::string_view platform);
[[nodiscard]] bool update_check_due(std::int64_t now, std::int64_t last);
class UpdateCheck {
public:
    UpdateCheck();
    ~UpdateCheck();
    UpdateCheck(const UpdateCheck &) = delete;
    UpdateCheck &operator=(const UpdateCheck &) = delete;
    [[nodiscard]] UpdateResult result() const;
private:
    ProcessGroup processes_;
    mutable std::mutex mutex_;
    UpdateResult result_;
    std::thread worker_;
};
} // namespace sarusaru::install
