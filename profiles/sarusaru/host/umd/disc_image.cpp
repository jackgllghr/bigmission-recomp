#include "umd/disc_image.hpp"

#include "platform/documents.hpp"
#include "psprecomp/common.hpp"

#include <algorithm>
#include <array>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sarusaru::umd {
namespace {
constexpr std::uint32_t kMaxDirectoryBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxDirectoryEntries = 4096u;

std::uint32_t both_endian(std::span<const std::uint8_t> data, std::size_t offset, std::size_t width) {
    std::uint32_t little = 0u, big = 0u;
    for (std::size_t i = 0; i < width; ++i) {
        little |= static_cast<std::uint32_t>(data[offset + i]) << (8u * i);
        big = (big << 8u) | data[offset + width + i];
    }
    if (little != big) throw psprecomp::Error("Inconsistent ISO integer encoding");
    return little;
}

bool same_name(std::string_view left, std::string_view right) noexcept {
    return std::ranges::equal(left, right, [](char a, char b) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return lower(a) == lower(b);
    });
}
}

#if defined(_WIN32)
DiscImage::Descriptor &DiscImage::Descriptor::operator=(Descriptor &&other) noexcept {
    value_ = std::exchange(other.value_, -1);
    return *this;
}
DiscImage::Descriptor::~Descriptor() = default;
#else
DiscImage::Descriptor &DiscImage::Descriptor::operator=(Descriptor &&other) noexcept {
    if (this != &other) {
        if (value_ >= 0) close(value_);
        value_ = std::exchange(other.value_, -1);
    }
    return *this;
}
DiscImage::Descriptor::~Descriptor() {
    if (value_ >= 0) close(value_);
}

DiscImage::DiscImage(int descriptor) : descriptor_(descriptor) {
    struct stat status {};
    if (descriptor < 0) throw psprecomp::Error("Cannot open disc image");
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) throw psprecomp::Error("Cannot size disc image");
    open(static_cast<std::uint64_t>(status.st_size));
}
#endif

DiscImage::DiscImage(const std::filesystem::path &path) {
#if !defined(_WIN32)
    if (platform::is_document(path)) {
        *this = DiscImage(platform::open_document(path));
        return;
    }
#endif
    stream_.open(path, std::ios::binary);
    if (!stream_) throw psprecomp::Error("Cannot open disc image");
    std::error_code error;
    const auto file_bytes = std::filesystem::file_size(path, error);
    if (error) throw psprecomp::Error("Cannot size disc image");
    open(file_bytes);
}

void DiscImage::open(std::uint64_t file_bytes) {
    volume_bytes_ = file_bytes;
    std::array<std::uint8_t, kSectorSize> pvd{};
    read_exact(16u * kSectorSize, pvd);
    static constexpr std::array<std::uint8_t, 7> signature{1u, 'C', 'D', '0', '0', '1', 1u};
    if (!std::equal(signature.begin(), signature.end(), pvd.begin()))
        throw psprecomp::Error("Expected an ISO9660 primary volume descriptor");
    if (both_endian(pvd, 128u, 2u) != kSectorSize) throw psprecomp::Error("Unsupported ISO block size");
    const std::uint64_t volume = static_cast<std::uint64_t>(both_endian(pvd, 80u, 4u)) * kSectorSize;
    if (volume < 17u * kSectorSize || volume > file_bytes) throw psprecomp::Error("Truncated or invalid ISO volume");
    volume_bytes_ = volume;
    sector_count_ = static_cast<std::uint32_t>(volume / kSectorSize);
    const std::span<const std::uint8_t> root_record(pvd.data() + 156u, std::min<std::size_t>(pvd[156u], 34u));
    root_ = parse_record(root_record);
    if (!root_.directory) throw psprecomp::Error("ISO root is not a directory");
}

void DiscImage::read_exact(std::uint64_t offset, std::span<std::uint8_t> out) {
    if (offset > volume_bytes_ || out.size() > volume_bytes_ - offset) throw psprecomp::Error("ISO extent outside image");
#if !defined(_WIN32)
    if (descriptor_.get() >= 0) {
        for (std::size_t done = 0; done < out.size();) {
            const ssize_t got = pread(descriptor_.get(), out.data() + done, out.size() - done,
                                      static_cast<off_t>(offset + done));
            if (got <= 0) throw psprecomp::Error("Short ISO read");
            done += static_cast<std::size_t>(got);
        }
        return;
    }
#endif
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    stream_.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
    if (static_cast<std::size_t>(stream_.gcount()) != out.size()) throw psprecomp::Error("Short ISO read");
}

DiscImage::Entry DiscImage::parse_record(std::span<const std::uint8_t> data) const {
    if (data.size() < 34u || data[0] != data.size() || 33u + data[32] > data.size())
        throw psprecomp::Error("Malformed directory record");
    if (data[1] != 0u || (data[25] & 0x80u) != 0u || data[26] != 0u || data[27] != 0u)
        throw psprecomp::Error("Extended attributes, multi-extent or interleaved files unsupported");
    if (both_endian(data, 28u, 2u) != 1u) throw psprecomp::Error("Multi-volume ISO unsupported");
    Entry entry;
    entry.sector = both_endian(data, 2u, 4u);
    entry.size = both_endian(data, 10u, 4u);
    entry.directory = (data[25] & 2u) != 0u;
    if (static_cast<std::uint64_t>(entry.sector) * kSectorSize + entry.size > volume_bytes_)
        throw psprecomp::Error("Directory extent outside volume");
    const std::uint8_t name_length = data[32];
    if (name_length == 1u && (data[33] == 0u || data[33] == 1u)) {
        entry.name = data[33] == 0u ? "." : "..";
        return entry;
    }
    for (std::size_t i = 0; i < name_length; ++i) {
        const auto c = data[33u + i];
        if (c >= 0x80u) throw psprecomp::Error("Non-ASCII ISO filename");
        entry.name.push_back(static_cast<char>(c));
    }
    if (entry.name.ends_with(";1")) entry.name.resize(entry.name.size() - 2u);
    if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
        entry.name.find_first_of(std::string_view("/\\;\0", 4)) != std::string::npos)
        throw psprecomp::Error("Unsafe ISO filename");
    return entry;
}

std::vector<DiscImage::Entry> DiscImage::list(const Entry &directory) {
    if (!directory.directory || directory.size > kMaxDirectoryBytes)
        throw psprecomp::Error("Invalid or oversized directory");
    std::vector<std::uint8_t> data(directory.size);
    read_exact(static_cast<std::uint64_t>(directory.sector) * kSectorSize, data);
    std::vector<Entry> result;
    std::size_t offset = 0u;
    while (offset < data.size()) {
        const std::uint8_t length = data[offset];
        if (length == 0u) {
            offset = (offset / kSectorSize + 1u) * kSectorSize;
            continue;
        }
        if (offset % kSectorSize + length > kSectorSize || offset + length > data.size())
            throw psprecomp::Error("Directory record crosses block");
        Entry entry = parse_record(std::span<const std::uint8_t>(data).subspan(offset, length));
        offset += length;
        if (entry.name == "." || entry.name == "..") continue;
        if (std::ranges::any_of(result, [&](const Entry &seen) { return same_name(seen.name, entry.name); }))
            throw psprecomp::Error("Duplicate ISO filename");
        result.push_back(std::move(entry));
        if (result.size() > kMaxDirectoryEntries) throw psprecomp::Error("Too many directory entries");
    }
    return result;
}

std::optional<DiscImage::Entry> DiscImage::find(std::string_view path) {
    while (path.starts_with('/')) path.remove_prefix(1u);
    Entry current = root_;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto component = path.substr(0u, slash);
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1u);
        if (component.empty() || component == "." || component == ".." || !current.directory) return std::nullopt;
        const auto entries = list(current);
        const auto found = std::ranges::find_if(entries, [&](const Entry &e) { return same_name(e.name, component); });
        if (found == entries.end()) return std::nullopt;
        current = *found;
    }
    return current;
}

std::size_t DiscImage::read(const Entry &file, std::uint64_t offset, std::span<std::uint8_t> out) {
    if (file.directory) throw psprecomp::Error("Cannot read a directory as a file");
    if (offset >= file.size) return 0u;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), file.size - offset));
    read_exact(static_cast<std::uint64_t>(file.sector) * kSectorSize + offset, out.first(count));
    return count;
}

std::vector<std::uint8_t> DiscImage::read_file(const Entry &file, std::uint32_t limit) {
    if (file.directory || file.size > limit) throw psprecomp::Error("Invalid or oversized disc file: " + file.name);
    std::vector<std::uint8_t> data(file.size);
    if (read(file, 0u, data) != data.size()) throw psprecomp::Error("Short disc file read");
    return data;
}

} // namespace sarusaru::umd
