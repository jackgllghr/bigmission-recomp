#pragma once

// Files the system's document picker hands out. On Android the picker gives a
// content:// URI, not a path: the app may read the document through a file
// descriptor but cannot name it in the file system. Elsewhere a picked file
// is an ordinary path and none of this applies.
#include <filesystem>
#include <string>

namespace sarusaru::platform {

#if defined(__ANDROID__)
// Whether `path` is a document URI rather than a path.
[[nodiscard]] bool is_document(const std::filesystem::path &path);
// A new read-only descriptor for the document (the caller closes it), or -1.
[[nodiscard]] int open_document(const std::filesystem::path &path);
#else
[[nodiscard]] inline bool is_document(const std::filesystem::path &) { return false; }
[[nodiscard]] inline int open_document(const std::filesystem::path &) { return -1; }
#endif

// The name to show for a picked file: its file name, or a document's own
// name (the URI's last segment, percent-decoded, without its folders).
[[nodiscard]] std::string display_name(const std::filesystem::path &path);

} // namespace sarusaru::platform
