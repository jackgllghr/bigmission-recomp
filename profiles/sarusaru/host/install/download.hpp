#pragma once

// Fetching a pinned file with the system's own curl: the Windows compiler
// that installs the game, and the optional English translation pack. A file
// reaches its destination only if its SHA-256 is the expected one.
#include "install/process.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sarusaru::install {

// The curl to use: Windows' own, macOS's, or the first on PATH (Linux).
[[nodiscard]] std::optional<std::filesystem::path> curl_program();

// Downloads `url` to `destination` (replacing it) through `destination`.part,
// keeping it only if its SHA-256 (lowercase hex) is `sha256`. `downloaded`
// reports the bytes received so far. False with `error` set on failure.
[[nodiscard]] bool download_verified(ProcessGroup &processes, std::string_view url, std::string_view sha256,
                                     const std::filesystem::path &destination,
                                     const std::function<void(std::uint64_t)> &downloaded, std::string &error);

} // namespace sarusaru::install
