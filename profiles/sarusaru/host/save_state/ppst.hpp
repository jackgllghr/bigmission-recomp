#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sarusaru::save_state {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Compression : std::uint32_t { None = 0, Snappy = 1, Zstandard = 2 };

// This is the PPSSPP file container, not a resumable GameSession. The payload
// contains ordered, nested subsystem records. It must be decoded and validated
// completely before any live runtime state is replaced.
struct PpstFile {
    std::uint32_t revision{5};
    Compression compression{Compression::Zstandard};
    std::string creator;
    std::string title;
    std::vector<std::uint8_t> payload;
};

// Bound compressed input and expanded output independently, before allocation.
inline constexpr std::size_t kMaxPpstSize = 128u * 1024u * 1024u;
[[nodiscard]] PpstFile decode_ppst(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encode_ppst(const PpstFile &file);
[[nodiscard]] PpstFile read_ppst(const std::filesystem::path &path);
// Atomically replaces the destination only after a complete, closed write.
void write_ppst(const std::filesystem::path &path, const PpstFile &file);

// PPSSPP sections carry a 16-byte name, an LE32 version, and an LE32 0x42
// closing cookie. There is no section length: an unknown section cannot safely
// be skipped by searching for a name or cookie in arbitrary guest memory.
// Longer schema names are truncated to sixteen bytes, as in PPSSPP's format.
class PayloadReader {
public:
    explicit PayloadReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] std::span<const std::uint8_t> bytes(std::size_t count);
    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();
    [[nodiscard]] bool boolean();
    [[nodiscard]] std::string string();
    [[nodiscard]] std::uint32_t section(std::string_view name, std::uint32_t minimum, std::uint32_t maximum);
    void end_section();
    void finish() const;

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

class PayloadWriter {
public:
    void bytes(std::span<const std::uint8_t> bytes);
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void boolean(bool value);
    void string(std::string_view value);
    void section(std::string_view name, std::uint32_t version);
    void end_section();
    [[nodiscard]] const std::vector<std::uint8_t> &data() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace sarusaru::save_state
