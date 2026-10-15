#include "install/update_check.hpp"
#include "install/download.hpp"
#include "release_version.hpp"

#include <charconv>
#include <map>

namespace sarusaru::install {
std::optional<ReleaseVersion> parse_release_version(std::string_view text) {
    ReleaseVersion version{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto end = text.find('.');
        const auto part = text.substr(0, end);
        if (part.empty() || (part.size() > 1 && part.front() == '0')) return std::nullopt;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), version[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size()) return std::nullopt;
        if (i == 2) return end == std::string_view::npos ? std::optional(version) : std::nullopt;
        if (end == std::string_view::npos) return std::nullopt;
        text.remove_prefix(end + 1);
    }
    return std::nullopt;
}
std::string_view update_platform() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "windows-x64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__aarch64__)
    return "linux-arm64";
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__x86_64__)
    return "linux-x64";
#else
    return {};
#endif
}
bool update_check_due(std::int64_t now, std::int64_t last) {
    return last <= 0 || now < last || now - last >= 24 * 60 * 60;
}
UpdateResult parse_update_feed(std::string_view feed, std::string_view installed, std::string_view platform) {
    const auto failed = UpdateResult{UpdateResult::State::Failed};
    if (feed.size() > 8192 || !feed.starts_with("SaruSaruRecomp-Update-v1\n")) return failed;
    feed.remove_prefix(std::string_view("SaruSaruRecomp-Update-v1\n").size());
    std::map<std::string, std::string> fields;
    while (!feed.empty()) {
        const auto end = feed.find('\n');
        if (end == std::string_view::npos) return failed;
        const auto line = feed.substr(0, end);
        feed.remove_prefix(end + 1);
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos || line.find('\t', tab + 1) != std::string_view::npos) return failed;
        for (const auto c : line) if (c != '\t' && (c < 32 || c > 126)) return failed;
        if (!fields.emplace(line.substr(0, tab), line.substr(tab + 1)).second) return failed;
    }
    const auto found = fields.find("version");
    if (found == fields.end()) return failed;
    const auto remote = parse_release_version(found->second), local = parse_release_version(installed);
    if (!remote || !local) return failed;
    UpdateResult result;
    result.version = found->second;
    if (*remote <= *local) { result.state = UpdateResult::State::Current; return result; }
    if (platform.empty() || !fields.contains(std::string(platform))) {
        result.state = UpdateResult::State::Unsupported;
        return result;
    }
    const auto &url = fields.at(std::string(platform));
    const auto extension = platform.starts_with("linux-") ? ".AppImage" : ".zip";
    const auto expected = "https://github.com/jackgllghr/bigmission-recomp/releases/download/v" + result.version +
        "/SaruSaruRecomp-v" + result.version + "-" + std::string(platform) + extension;
    if (url != expected) return failed;
    result.state = UpdateResult::State::Available;
    result.download_url = url;
    return result;
}
UpdateCheck::UpdateCheck() : worker_([this] {
    UpdateResult result{UpdateResult::State::Failed};
    if (const auto curl = curl_program()) {
        const auto fetched = processes_.run({path_argument(*curl), "--fail", "--location", "--silent", "--show-error",
            "--connect-timeout", "5", "--max-time", "20", "--max-redirs", "3", "--max-filesize", "8192",
            "--proto", "=https", "--proto-redir", "=https", std::string(kUpdateFeed)});
        if (fetched.ok()) result = parse_update_feed(fetched.output, release_version, update_platform());
    }
    std::lock_guard lock(mutex_);
    result_ = std::move(result);
}) {}
UpdateCheck::~UpdateCheck() {
    processes_.cancel();
    if (worker_.joinable()) worker_.join();
}
UpdateResult UpdateCheck::result() const { std::lock_guard lock(mutex_); return result_; }
} // namespace sarusaru::install
