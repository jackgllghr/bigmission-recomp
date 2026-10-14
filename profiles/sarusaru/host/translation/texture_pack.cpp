#include "translation/texture_pack.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace sarusaru::translation {
namespace {
constexpr std::size_t kMaxPackBytes = 32u * 1024u * 1024u;
[[noreturn]] void invalid(std::string_view reason) {
    throw std::runtime_error("translation pack: " + std::string(reason));
}
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::span<const std::uint8_t> take(std::size_t length) {
        if (length > bytes_.size()) invalid("truncated data");
        const auto result = bytes_.first(length);
        bytes_ = bytes_.subspan(length);
        return result;
    }
    std::uint32_t integer(std::size_t count) {
        const auto bytes = take(count);
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < count; ++i) value |= std::uint32_t(bytes[i]) << (i * 8u);
        return value;
    }
    std::string text(std::size_t count) {
        const auto bytes = take(count);
        return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    }
    std::string name(std::size_t maximum, bool path = false) {
        const auto length = integer(2);
        if (length == 0 || length > maximum) invalid("invalid identifier length");
        auto value = text(length);
        if (std::ranges::any_of(value, [](unsigned char c) { return c < 33 || c > 126 || c == '\\' || c == ':'; }))
            invalid("invalid identifier");
        if (!path && value.find('/') != value.npos) invalid("invalid entry ID");
        if (path) {
            const std::filesystem::path parsed(value);
            if (parsed.is_absolute()) invalid("absolute source path");
            for (const auto &part : parsed) if (part == "." || part == ".." || part.empty()) invalid("unsafe source path");
            if (value.find("//") != value.npos || value.back() == '/') invalid("unsafe source path");
        }
        return value;
    }
    std::string digest() {
        auto result = text(64);
        if (std::ranges::any_of(result, [](char c) { return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')); }))
            invalid("invalid SHA-256");
        return result;
    }
    bool empty() const { return bytes_.empty(); }
private:
    std::span<const std::uint8_t> bytes_;
};
}

TexturePack TexturePack::load(const std::filesystem::path &path, std::string_view disc_id,
                              std::string_view executable_hash, const SourceReader &read_source) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) invalid("cannot open " + path.string());
    const auto length = in.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > kMaxPackBytes) invalid("oversized file");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) invalid("short read");
    Reader reader(bytes);
    if (reader.text(8) != "SSTEXPK1") invalid("unsupported format/version");
    if (disc_id.size() != 9 || reader.text(9) != disc_id || reader.digest() != executable_hash) invalid("incompatible game");
    TexturePack pack;
    pack.locale_ = reader.text(2);
    if (pack.locale_ != "en") invalid("unsupported locale");
    const auto source_count = reader.integer(4);
    if (source_count == 0 || source_count > 16) invalid("invalid source count");
    std::set<std::string> sources;
    for (std::uint32_t i = 0; i < source_count; ++i) {
        const auto source = reader.name(255, true);
        if (!sources.insert(source).second) invalid("duplicate source path");
        const auto expected = reader.digest();
        const auto original = read_source(source);
        if (original.size() > kMaxPackBytes || psprecomp::sha256(original) != expected) invalid("source hash mismatch: " + source);
    }
    const auto count = reader.integer(4);
    if (count == 0 || count > 64) invalid("invalid replacement count");
    std::set<std::string> ids, identities;
    for (std::uint32_t i = 0; i < count; ++i) {
        Entry entry;
        entry.id = reader.name(128);
        if (!ids.insert(entry.id).second) invalid("duplicate entry ID");
        entry.width = static_cast<std::uint16_t>(reader.integer(2));
        entry.height = static_cast<std::uint16_t>(reader.integer(2));
        entry.ge_width = static_cast<std::uint16_t>(reader.integer(2));
        entry.ge_height = static_cast<std::uint16_t>(reader.integer(2));
        entry.buffer_width = static_cast<std::uint16_t>(reader.integer(2));
        const auto power_of_two = [](std::uint16_t n) { return n != 0 && n <= 512 && (n & (n - 1)) == 0; };
        if (!power_of_two(entry.ge_width) || !power_of_two(entry.ge_height) || entry.width == 0 || entry.height == 0 ||
            entry.width > entry.ge_width || entry.height > entry.ge_height || entry.buffer_width < entry.width || entry.buffer_width > 512)
            invalid("invalid texture dimensions");
        entry.source_hash = reader.digest();
        const auto identity = std::to_string(entry.width) + ":" + std::to_string(entry.height) + ":" +
            std::to_string(entry.ge_width) + ":" + std::to_string(entry.ge_height) + ":" + std::to_string(entry.buffer_width) + ":" + entry.source_hash;
        if (!identities.insert(identity).second) invalid("ambiguous replacement identity");
        const auto size = reader.integer(4);
        if (size != static_cast<std::uint32_t>(entry.width) * entry.height * 4u) invalid("invalid pixel payload size");
        const auto rgba = reader.take(size);
        entry.rgba.assign(rgba.begin(), rgba.end());
        pack.entries_.push_back(std::move(entry));
    }
    if (!reader.empty()) invalid("trailing data");
    return pack;
}

bool TexturePack::apply(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels) const {
    if (pixels.size() != static_cast<std::size_t>(texture.width) * texture.height) return false;
    for (const auto &entry : entries_) {
        if (texture.width != entry.ge_width || texture.height != entry.ge_height || texture.buffer_width != entry.buffer_width) continue;
        std::vector<std::uint8_t> source;
        source.reserve(entry.rgba.size());
        for (std::size_t y = 0; y < entry.height; ++y) for (std::size_t x = 0; x < entry.width; ++x) {
            const auto value = pixels[y * texture.width + x];
            for (unsigned shift = 0; shift < 32; shift += 8) source.push_back(static_cast<std::uint8_t>(value >> shift));
        }
        if (psprecomp::sha256(source) != entry.source_hash) continue;
        // Preserve padding and neighboring image data outside the logical crop.
        for (std::size_t y = 0; y < entry.height; ++y) for (std::size_t x = 0; x < entry.width; ++x) {
            const auto offset = (y * entry.width + x) * 4;
            std::uint32_t value = 0;
            for (unsigned byte = 0; byte < 4; ++byte) value |= std::uint32_t(entry.rgba[offset + byte]) << (byte * 8);
            pixels[y * texture.width + x] = value;
        }
        return true;
    }
    return false;
}

} // namespace sarusaru::translation
