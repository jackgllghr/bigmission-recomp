#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sarusaru::umd {

inline constexpr std::uint32_t kSectorSize = 2048u;

// Read-only ISO9660 view of the user's own disc image. It applies the same
// structural rules as tools/prepare_game.py and never extracts files to disk.
class DiscImage {
public:
    struct Entry {
        std::string name;
        std::uint32_t sector{};
        std::uint32_t size{};
        bool directory{};
    };

    // Throws psprecomp::Error for anything that is not a usable ISO9660 image.
    // On Android `path` may be a document URI from the system's picker
    // (platform/documents.hpp).
    explicit DiscImage(const std::filesystem::path &path);
#if !defined(_WIN32)
    // The image behind an open read-only descriptor, which it takes over.
    explicit DiscImage(int descriptor);
#endif

    [[nodiscard]] const Entry &root() const noexcept { return root_; }
    [[nodiscard]] std::uint32_t sector_count() const noexcept { return sector_count_; }

    // Slash-separated path from the root, matched case-insensitively as the PSP
    // does. Empty components and "."/".." are rejected rather than resolved.
    [[nodiscard]] std::optional<Entry> find(std::string_view path);
    [[nodiscard]] std::vector<Entry> list(const Entry &directory);

    // Reads up to out.size() bytes of a file at offset; returns bytes read (0 at EOF).
    [[nodiscard]] std::size_t read(const Entry &file, std::uint64_t offset, std::span<std::uint8_t> out);
    [[nodiscard]] std::vector<std::uint8_t> read_file(const Entry &file, std::uint32_t limit);

private:
    // An owned descriptor, closed with the image; -1 when reading a path.
    class Descriptor {
    public:
        Descriptor() noexcept = default;
        explicit Descriptor(int value) noexcept : value_(value) {}
        Descriptor(Descriptor &&other) noexcept : value_(std::exchange(other.value_, -1)) {}
        Descriptor &operator=(Descriptor &&other) noexcept;
        ~Descriptor();
        [[nodiscard]] int get() const noexcept { return value_; }

    private:
        int value_{-1};
    };

    std::ifstream stream_;
    Descriptor descriptor_;
    std::uint64_t volume_bytes_{};
    std::uint32_t sector_count_{};
    Entry root_;

    // Checks the volume descriptor and finds the root, once the source is open.
    void open(std::uint64_t file_bytes);
    void read_exact(std::uint64_t offset, std::span<std::uint8_t> out);
    [[nodiscard]] Entry parse_record(std::span<const std::uint8_t> record) const;
};

} // namespace sarusaru::umd
