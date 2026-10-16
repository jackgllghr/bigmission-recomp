#include "save_state/ppst.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>

#include <zstd.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sarusaru::save_state {
namespace {

constexpr std::size_t kHeaderSize = 48;
constexpr std::size_t kTitleSize = 128;

std::string fixed_string(std::span<const std::uint8_t> bytes) {
    const auto end = std::find(bytes.begin(), bytes.end(), 0);
    return std::string(bytes.begin(), end);
}

void fixed_string(PayloadWriter &writer, std::string_view text, std::size_t width, bool allow_full = false) {
    if (text.find('\0') != std::string_view::npos || text.size() > width || (!allow_full && text.size() == width))
        throw FormatError("PPST: metadata exceeds its fixed field or contains NUL");
    writer.bytes({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
    for (std::size_t i = text.size(); i < width; ++i) writer.u8(0);
}

// Independently implemented from Google's raw Snappy wire-format specification.
std::vector<std::uint8_t> unsnappy(std::span<const std::uint8_t> compressed, std::size_t expected) {
    PayloadReader input(compressed);
    std::uint32_t declared{};
    bool terminated = false;
    for (unsigned shift = 0; shift <= 28; shift += 7) {
        const auto b = input.u8();
        if (shift == 28 && (b & 0xF0u) != 0) throw FormatError("PPST: overflowing Snappy length");
        declared |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) { terminated = true; break; }
    }
    if (!terminated || declared != expected) throw FormatError("PPST: Snappy length mismatch");
    std::vector<std::uint8_t> output;
    output.reserve(expected);
    while (input.remaining() != 0) {
        const auto tag = input.u8();
        const unsigned type = tag & 3u;
        std::uint64_t length{};
        if (type == 0) {
            const unsigned code = tag >> 2u;
            length = code + 1u;
            if (code >= 60u) {
                length = 0;
                for (unsigned i = 0; i < code - 59u; ++i)
                    length |= static_cast<std::uint64_t>(input.u8()) << (8u * i);
                ++length;
            }
            if (length > expected - output.size()) throw FormatError("PPST: Snappy literal exceeds output");
            const auto literal = input.bytes(static_cast<std::size_t>(length));
            output.insert(output.end(), literal.begin(), literal.end());
        } else {
            std::uint32_t distance{};
            if (type == 1) {
                length = 4u + ((tag >> 2u) & 7u);
                distance = (static_cast<std::uint32_t>(tag & 0xE0u) << 3u) | input.u8();
            } else {
                length = 1u + (tag >> 2u);
                const unsigned count = type == 2 ? 2u : 4u;
                for (unsigned i = 0; i < count; ++i)
                    distance |= static_cast<std::uint32_t>(input.u8()) << (8u * i);
            }
            if (distance == 0 || distance > output.size() || length > expected - output.size())
                throw FormatError("PPST: invalid Snappy backreference");
            // Forward byte copies deliberately support overlapping references.
            for (std::uint64_t i = 0; i < length; ++i) output.push_back(output[output.size() - distance]);
        }
    }
    if (output.size() != expected) throw FormatError("PPST: truncated Snappy output");
    return output;
}

std::vector<std::uint8_t> snappy_literals(std::span<const std::uint8_t> input) {
    PayloadWriter output;
    auto size = static_cast<std::uint32_t>(input.size());
    do {
        const auto low = static_cast<std::uint8_t>(size & 0x7Fu);
        size >>= 7u;
        output.u8(static_cast<std::uint8_t>(low | (size != 0 ? 0x80u : 0u)));
    } while (size != 0);
    if (!input.empty()) {
        const auto last = static_cast<std::uint32_t>(input.size() - 1u);
        if (input.size() <= 60u) {
            output.u8(static_cast<std::uint8_t>(last << 2u));
        } else {
            unsigned width = 1;
            while (width < 4u && (last >> (width * 8u)) != 0u) ++width;
            output.u8(static_cast<std::uint8_t>((59u + width) << 2u));
            for (unsigned i = 0; i < width; ++i) output.u8(static_cast<std::uint8_t>(last >> (8u * i)));
        }
        output.bytes(input);
    }
    return output.data();
}

} // namespace

std::span<const std::uint8_t> PayloadReader::bytes(std::size_t count) {
    if (count > remaining()) throw FormatError("PPST: truncated payload at byte " + std::to_string(offset_));
    const auto result = bytes_.subspan(offset_, count);
    offset_ += count;
    return result;
}

std::uint8_t PayloadReader::u8() { return bytes(1)[0]; }
std::uint32_t PayloadReader::u32() {
    const auto b = bytes(4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8u) |
           (static_cast<std::uint32_t>(b[2]) << 16u) | (static_cast<std::uint32_t>(b[3]) << 24u);
}
std::uint64_t PayloadReader::u64() {
    const auto low = u32();
    return low | (static_cast<std::uint64_t>(u32()) << 32u);
}
bool PayloadReader::boolean() {
    const auto value = u8();
    if (value > 1) throw FormatError("PPST: invalid boolean");
    return value != 0;
}
std::string PayloadReader::string() {
    const auto size = u32();
    if (size == 0 || size > 1024u * 1024u) throw FormatError("PPST: invalid string length");
    const auto value = bytes(size);
    if (value.back() != 0) throw FormatError("PPST: unterminated string");
    return std::string(value.begin(), value.end() - 1);
}
std::uint32_t PayloadReader::section(std::string_view name, std::uint32_t minimum, std::uint32_t maximum) {
    if (name.empty() || name.find('\0') != std::string_view::npos || minimum == 0 || maximum < minimum)
        throw FormatError("PPST: invalid section schema");
    std::array<std::uint8_t, 16> expected{};
    std::copy_n(name.begin(), std::min(name.size(), expected.size()), expected.begin());
    const auto marker = bytes(expected.size());
    if (!std::equal(marker.begin(), marker.end(), expected.begin()))
        throw FormatError("PPST: expected section " + std::string(name));
    const auto version = u32();
    if (version < minimum || version > maximum)
        throw FormatError("PPST: unsupported " + std::string(name) + " version " + std::to_string(version));
    return version;
}
void PayloadReader::end_section() {
    const auto at = offset_;
    const auto cookie = u32();
    if (cookie != 0x42u) throw FormatError("PPST: invalid section closing cookie at byte " +
        std::to_string(at) + " (found " + std::to_string(cookie) + ")");
}
void PayloadReader::finish() const {
    if (remaining() != 0) throw FormatError("PPST: unexpected trailing payload");
}

void PayloadWriter::bytes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize - bytes_.size()) throw FormatError("PPST: payload exceeds size limit");
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}
void PayloadWriter::u8(std::uint8_t value) { bytes({&value, 1}); }
void PayloadWriter::u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32u; shift += 8u) u8(static_cast<std::uint8_t>(value >> shift));
}
void PayloadWriter::u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32u));
}
void PayloadWriter::boolean(bool value) { u8(value ? 1u : 0u); }
void PayloadWriter::string(std::string_view value) {
    if (value.size() >= 1024u * 1024u) throw FormatError("PPST: string exceeds size limit");
    u32(static_cast<std::uint32_t>(value.size() + 1u));
    bytes({reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
    u8(0);
}
void PayloadWriter::section(std::string_view name, std::uint32_t version) {
    if (name.empty() || name.find('\0') != std::string_view::npos || version == 0)
        throw FormatError("PPST: invalid section schema");
    std::array<std::uint8_t, 16> marker{};
    std::copy_n(name.begin(), std::min(name.size(), marker.size()), marker.begin());
    bytes(marker);
    u32(version);
}
void PayloadWriter::end_section() { u32(0x42u); }

PpstFile decode_ppst(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxPpstSize) throw FormatError("PPST: file exceeds size limit");
    PayloadReader input(bytes);
    PpstFile result;
    result.revision = input.u32();
    if (result.revision != 4 && result.revision != 5) throw FormatError("PPST: unsupported file revision");
    const auto compression = input.u32();
    if (compression > 2) throw FormatError("PPST: unsupported compression");
    result.compression = static_cast<Compression>(compression);
    const auto stored_size = input.u32();
    const auto expanded_size = input.u32();
    if (stored_size == 0 || expanded_size == 0 || expanded_size > kMaxPpstSize)
        throw FormatError("PPST: invalid payload size");
    result.creator = fixed_string(input.bytes(32));
    if (result.revision == 5) result.title = fixed_string(input.bytes(kTitleSize));
    if (stored_size != input.remaining()) throw FormatError("PPST: stored size does not match file length");
    const auto stored = input.bytes(stored_size);
    if (result.compression == Compression::None) {
        if (stored_size != expanded_size) throw FormatError("PPST: uncompressed size mismatch");
        result.payload.assign(stored.begin(), stored.end());
    } else if (result.compression == Compression::Snappy) {
        result.payload = unsnappy(stored, expanded_size);
    } else {
        const auto frame_size = ZSTD_findFrameCompressedSize(stored.data(), stored.size());
        if (ZSTD_isError(frame_size) || frame_size != stored.size())
            throw FormatError("PPST: invalid or trailing Zstandard frame");
        result.payload.resize(expanded_size);
        const auto written = ZSTD_decompress(result.payload.data(), result.payload.size(), stored.data(), stored.size());
        if (ZSTD_isError(written) || written != expanded_size) throw FormatError("PPST: Zstandard decompression failed");
    }
    return result;
}

std::vector<std::uint8_t> encode_ppst(const PpstFile &file) {
    if (file.revision != 4 && file.revision != 5) throw FormatError("PPST: unsupported file revision");
    if (file.revision == 4 && !file.title.empty()) throw FormatError("PPST: revision 4 has no title field");
    if (file.payload.empty() || file.payload.size() > kMaxPpstSize - kHeaderSize - kTitleSize)
        throw FormatError("PPST: invalid payload size");
    std::vector<std::uint8_t> compressed;
    std::span<const std::uint8_t> stored = file.payload;
    if (file.compression == Compression::Snappy) {
        compressed = snappy_literals(file.payload);
        stored = compressed;
    } else if (file.compression == Compression::Zstandard) {
        compressed.resize(ZSTD_compressBound(file.payload.size()));
        const std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx(), &ZSTD_freeCCtx);
        if (!context || ZSTD_isError(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 1)))
            throw FormatError("PPST: cannot create Zstandard compressor");
        const auto written = ZSTD_compress2(context.get(), compressed.data(), compressed.size(), file.payload.data(), file.payload.size());
        if (ZSTD_isError(written)) throw FormatError("PPST: Zstandard compression failed");
        compressed.resize(written);
        stored = compressed;
    } else if (file.compression != Compression::None) {
        throw FormatError("PPST: unsupported compression");
    }
    PayloadWriter output;
    output.u32(file.revision);
    output.u32(static_cast<std::uint32_t>(file.compression));
    output.u32(static_cast<std::uint32_t>(stored.size()));
    output.u32(static_cast<std::uint32_t>(file.payload.size()));
    fixed_string(output, file.creator, 32, true);
    if (file.revision == 5) fixed_string(output, file.title, kTitleSize);
    output.bytes(stored);
    return output.data();
}

PpstFile read_ppst(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw FormatError("PPST: cannot open " + path.string());
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kMaxPpstSize) throw FormatError("PPST: invalid file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) throw FormatError("PPST: file changed while reading");
    return decode_ppst(bytes);
}

void write_ppst(const std::filesystem::path &path, const PpstFile &file) {
    const auto bytes = encode_ppst(file);
    write_atomic_file(path, bytes);
}

void write_atomic_file(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    static std::atomic<std::uint64_t> sequence{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = path;
    temporary += "." + std::to_string(stamp) + "." + std::to_string(sequence++) + ".tmp";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw FormatError("PPST: cannot create temporary file");
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) throw FormatError("PPST: cannot write temporary file");
        output.close();
        if (!output) throw FormatError("PPST: cannot close temporary file");
    }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw FormatError("PPST: cannot replace destination: " + std::to_string(GetLastError()));
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) throw FormatError("PPST: cannot replace destination: " + error.message());
#endif
}

} // namespace sarusaru::save_state
