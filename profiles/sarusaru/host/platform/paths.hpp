#pragma once

// Where the port keeps things on the player's machine.
#include <filesystem>
#include <optional>

namespace sarusaru::platform {

// The per-user data directory: %APPDATA%\SaruSaruRecomp on Windows,
// ~/Library/Application Support/SaruSaruRecomp on macOS and
// $XDG_DATA_HOME/SaruSaruRecomp (or ~/.local/share/SaruSaruRecomp) elsewhere.
// Nothing when the environment names no home.
[[nodiscard]] std::optional<std::filesystem::path> user_data_directory();

// The directory of the running executable (resolved through links), or
// nothing if the system will not say.
[[nodiscard]] std::optional<std::filesystem::path> executable_directory();

// Where the port's shipped files are: resources/ and sdk/ in the shipped-files
// directory, the shared libraries in the library directory. Both are the
// executable's directory (libraries in its lib/), except in a macOS app
// bundle: Contents/Resources and Contents/Frameworks.
[[nodiscard]] std::optional<std::filesystem::path> shipped_files_directory();
[[nodiscard]] std::optional<std::filesystem::path> library_directory();

} // namespace sarusaru::platform
