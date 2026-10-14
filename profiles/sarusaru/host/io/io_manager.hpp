#pragma once

#include "umd/disc_image.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sarusaru::io {

inline constexpr std::uint32_t kFileNotFound = 0x80010002u;
inline constexpr std::uint32_t kNotDirectory = 0x80010014u;
inline constexpr std::uint32_t kInvalidArgument = 0x80010016u;
inline constexpr std::uint32_t kReadOnly = 0x8001001Eu;
inline constexpr std::uint32_t kTooManyFiles = 0x80020320u;
inline constexpr std::uint32_t kNoDevice = 0x80020321u;
inline constexpr std::uint32_t kBadFile = 0x80020323u;
inline constexpr std::uint32_t kAsyncBusy = 0x80020329u;
inline constexpr std::uint32_t kNoAsync = 0x8002032Au;

inline constexpr std::uint32_t kOpenRead = 0x0001u;
inline constexpr std::uint32_t kOpenWriteFlags = 0x0002u | 0x0100u | 0x0200u | 0x0400u;  // WRONLY, APPEND, CREAT, TRUNC
inline constexpr std::uint32_t kStatSize = 88u;
inline constexpr std::uint32_t kDirentNameOffset = kStatSize;
inline constexpr std::uint32_t kMaxDescriptors = 64u;

// IoFileMgr over the user's disc. `disc0:` paths and `sce_lbnN_sizeM` sector
// extents are read-only views of DiscImage; descriptors 0-2 are stdio, and
// writes to 1 and 2 go to the host log. Asynchronous operations complete when
// issued; their result is collected with Wait/Poll. Other devices and
// relative paths stop execution rather than being guessed.
// SARUSARU_TRACE_IO=1 logs file operations to stderr.
class IoManager {
public:
    explicit IoManager(umd::DiscImage &disc, psprecomp::Runtime &runtime) noexcept
        : disc_(&disc), runtime_(&runtime) {}

    [[nodiscard]] std::int32_t open(const std::string &path, std::uint32_t flags);
    [[nodiscard]] std::int32_t close(std::int32_t fd);
    [[nodiscard]] std::int64_t read(std::int32_t fd, std::uint32_t address, std::uint32_t size);
    [[nodiscard]] std::int32_t write(std::int32_t fd, std::uint32_t address, std::uint32_t size);
    [[nodiscard]] std::int64_t seek(std::int32_t fd, std::int64_t offset, std::uint32_t whence);
    [[nodiscard]] std::int32_t get_stat(const std::string &path, std::uint32_t address);
    [[nodiscard]] std::int32_t open_directory(const std::string &path);
    [[nodiscard]] std::int32_t read_directory(std::int32_t fd, std::uint32_t address);
    [[nodiscard]] std::int32_t close_directory(std::int32_t fd);

    // ISO9660 file ioctls: 0x01020006 start sector (4 bytes), 0x01020007 size (8 bytes).
    [[nodiscard]] std::int32_t ioctl(std::int32_t fd, std::uint32_t command, std::uint32_t out_address,
                                     std::uint32_t out_length);

    [[nodiscard]] std::int32_t open_async(const std::string &path, std::uint32_t flags);
    [[nodiscard]] std::int32_t read_async(std::int32_t fd, std::uint32_t address, std::uint32_t size);
    [[nodiscard]] std::int32_t close_async(std::int32_t fd);
    // Writes the 64-bit result and returns 0 (wait) or 0/1 done/busy (poll).
    [[nodiscard]] std::int32_t collect_async(std::int32_t fd, std::uint32_t result_address);

private:
    struct File {
        bool directory{};
        bool open_failed{};  // An async open that failed keeps its descriptor until collected.
        umd::DiscImage::Entry entry;
        std::uint64_t position{};
        std::vector<umd::DiscImage::Entry> listing;
        std::size_t next_entry{};
        std::optional<std::int64_t> async_result;
        bool close_after_collect{};
    };

    umd::DiscImage *disc_;
    psprecomp::Runtime *runtime_;
    std::map<std::int32_t, File> files_;

    // Resolves a path on the disc, or returns an error code. Unmodeled devices
    // and path forms stop the runtime with an explanation.
    [[nodiscard]] std::variant<umd::DiscImage::Entry, std::uint32_t> resolve(const std::string &path);
    [[nodiscard]] std::int32_t allocate(File file);
    [[nodiscard]] File *file(std::int32_t fd) noexcept;
    void write_stat(std::uint32_t address, const umd::DiscImage::Entry &entry);
    void trace(const std::string &line) const;
};

void register_io(IoManager &io, psprecomp::Runtime &runtime);

} // namespace sarusaru::io
