#pragma once

// Which version of the English pack and the prebuilt HD texture pack is
// installed, and the newest of each, so the launcher can say what the player
// has and offer the latest.
//
// Both packs are published as GitHub releases apart from the port
// (sarusaru-translations, sarusaru-hd-textures); a release's tag names its
// version ("english-e94f09a659f6", "hd-2026-10-18-previews"). A build pins
// one release of each (pack_config.hpp, texture_config.hpp); the latest is
// read from the GitHub API, strictly: only the expected asset of that
// repository's release, with its SHA-256 and size, which the download then
// checks as it does the pinned one. The version of a pack installed from a
// release is kept beside it (<pack>.version).
#include "install/pack_download.hpp"
#include "install/process.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sarusaru::install {

enum class PackKind { English, HdTextures };

// A pack's release: what to download, and its version and publication date
// (YYYY-MM-DD, empty when unknown).
struct PackRelease {
    PackSource source;
    std::string version;
    std::string published;
};

// "english-e94f09a659f6" is version "e94f09a659f6", "hd-2026-10-18-previews"
// "2026-10-18-previews": the tag without its first word.
[[nodiscard]] std::string release_version(std::string_view tag);
// The tag in a GitHub release download URL (.../releases/download/<tag>/<asset>), or empty.
[[nodiscard]] std::string release_tag(std::string_view url);

// The release this build pins for `kind`, or nothing if it offers none.
[[nodiscard]] std::optional<PackRelease> pinned_release(PackKind kind);

// The GitHub API's latest release of the repository for `kind`.
[[nodiscard]] std::string latest_release_url(PackKind kind);
// Reads that API's answer: nothing unless it is the expected asset of that
// repository's release, with a SHA-256 digest and a plausible size.
[[nodiscard]] std::optional<PackRelease> parse_latest_release(std::string_view json, PackKind kind);

// Where `release` is kept once installed (english_pack_path, prebuilt_pack_directory).
[[nodiscard]] std::optional<std::filesystem::path> release_destination(PackKind kind, const PackRelease &release);
// Records `release` as the version installed at `pack`, a pack file or directory.
void record_release(const std::filesystem::path &pack, const PackRelease &release);
// The release installed at `pack`: as recorded, or the pinned one when that
// is where the pinned one goes. Nothing for a pack from anywhere else.
[[nodiscard]] std::optional<PackRelease> installed_release(PackKind kind, const std::filesystem::path &pack);
// For a pack installed where the launcher keeps them (english-<id>.sstp,
// 4x-prebuilt-<id>), its <id>: the first digits of its SHA-256. Empty for
// any other path. Tells a pack an earlier build downloaded, without a
// record, from one of the player's own.
[[nodiscard]] std::string installed_pack_id(PackKind kind, const std::filesystem::path &pack);
// Whether `release` is the one installed at `pack` (by its SHA-256).
[[nodiscard]] bool is_installed(PackKind kind, const PackRelease &release, const std::filesystem::path &pack);

// The packs of `kind` already installed under the per-user data directory,
// whole (the English pack's file, an unpacked HD pack's directory), the
// newest first: the most recently published, else the most recently written.
[[nodiscard]] std::vector<std::filesystem::path> installed_packs(PackKind kind);
// Removes every installed pack of `kind` but `keep` (and their records).
void remove_other_packs(PackKind kind, const std::filesystem::path &keep);

// Asks GitHub for the latest release of each pack the build offers, on its
// own thread. Never downloads a pack itself.
class PackUpdateCheck {
public:
    struct Result {
        enum class State { Checking, Done, Failed };
        State state{State::Checking};
        // Nothing for a pack the build does not offer, or that could not be read.
        std::optional<PackRelease> english, hd_textures;
    };
    PackUpdateCheck();
    ~PackUpdateCheck();
    PackUpdateCheck(const PackUpdateCheck &) = delete;
    PackUpdateCheck &operator=(const PackUpdateCheck &) = delete;
    [[nodiscard]] Result result() const;

private:
    ProcessGroup processes_;
    mutable std::mutex mutex_;
    Result result_;
    std::thread worker_;
};

} // namespace sarusaru::install
