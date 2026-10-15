#include "translation/texture_pack.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <optional>
#include <stdexcept>

namespace sarusaru::translation {
namespace {
constexpr std::size_t kMaxPackBytes = 64u * 1024u * 1024u;
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
            if (parsed.has_root_path()) invalid("absolute source path");
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
    const auto magic = reader.text(8);
    if (magic != "SSTEXPK1" && magic != "SSTEXPK2" && magic != "SSTEXPK3" && magic != "SSTEXPK4" && magic != "SSTEXPK5") invalid("unsupported format/version");
    if (disc_id.size() != 9 || reader.text(9) != disc_id || reader.digest() != executable_hash) invalid("incompatible game");
    TexturePack pack;
    pack.padded_strings_ = magic == "SSTEXPK3" || magic == "SSTEXPK4" || magic == "SSTEXPK5";
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
        if (magic != "SSTEXPK1") {
            const auto regions = reader.integer(2);
            if (regions == 0 || regions > 64) invalid("invalid replacement region count");
            for (std::uint32_t r = 0; r < regions; ++r) {
                Region region;
                region.x = static_cast<std::uint16_t>(reader.integer(2));
                region.y = static_cast<std::uint16_t>(reader.integer(2));
                region.width = static_cast<std::uint16_t>(reader.integer(2));
                region.height = static_cast<std::uint16_t>(reader.integer(2));
                if (region.width == 0 || region.height == 0 || region.x + region.width > entry.width || region.y + region.height > entry.height)
                    invalid("replacement region outside image");
                for (const auto &other : entry.regions)
                    if (region.x < other.x + other.width && other.x < region.x + region.width &&
                        region.y < other.y + other.height && other.y < region.y + region.height)
                        invalid("overlapping replacement regions");
                entry.regions.push_back(region);
            }
        } else entry.regions.push_back({0, 0, entry.width, entry.height});
        const auto identity = std::to_string(entry.width) + ":" + std::to_string(entry.height) + ":" +
            std::to_string(entry.ge_width) + ":" + std::to_string(entry.ge_height) + ":" + std::to_string(entry.buffer_width) + ":" + entry.source_hash;
        if (!identities.insert(identity).second) invalid("ambiguous replacement identity");
        const auto size = reader.integer(4);
        if (size != static_cast<std::uint32_t>(entry.width) * entry.height * 4u) invalid("invalid pixel payload size");
        const auto rgba = reader.take(size);
        entry.rgba.assign(rgba.begin(), rgba.end());
        pack.entries_.push_back(std::move(entry));
    }
    if (magic != "SSTEXPK1") {
        const auto string_count = reader.integer(4);
        if (string_count > 1024) invalid("too many string replacements");
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
        for (std::uint32_t i = 0; i < string_count; ++i) {
            StringEntry entry;
            entry.id = reader.name(128);
            if (!ids.insert(entry.id).second) invalid("duplicate entry ID");
            entry.offset = reader.integer(4);
            entry.capacity = reader.integer(4);
            if (entry.capacity < 2 || entry.capacity > 2048 || entry.offset > 4u * 1024u * 1024u - entry.capacity)
                invalid("invalid string slot");
            entry.source_hash = reader.digest();
            const auto length = reader.integer(2);
            if (length == 0 || length >= entry.capacity) invalid("string exceeds its source slot");
            entry.replacement = reader.text(length);
            if (std::ranges::any_of(entry.replacement, [](unsigned char c) { return (c < 32 && c != 10) || c > 126; }))
                invalid("string must use printable guest ASCII");
            ranges.emplace_back(entry.offset, entry.offset + entry.capacity);
            pack.strings_.push_back(std::move(entry));
        }
        std::ranges::sort(ranges);
        for (std::size_t i = 1; i < ranges.size(); ++i)
            if (ranges[i].first < ranges[i - 1].second) invalid("overlapping string slots");
        const auto runs = reader.integer(2);
        if (runs > 32) invalid("too many glyph-run replacements");
        std::set<std::string> run_identities;
        for (std::uint32_t i = 0; i < runs; ++i) {
            GlyphRun run;
            run.id = reader.name(128);
            if (!ids.insert(run.id).second) invalid("duplicate entry ID");
            run.width = static_cast<std::uint16_t>(reader.integer(2));
            run.height = static_cast<std::uint16_t>(reader.integer(2));
            if (run.width == 0 || run.width > 448 || run.height != 16) invalid("invalid help-line dimensions");
            run.font_hash = reader.digest();
            run.signature = reader.digest();
            run.glyph_count = static_cast<std::uint16_t>(reader.integer(2));
            if (run.glyph_count == 0 || run.glyph_count > 64 || !run_identities.insert(run.font_hash + run.signature).second)
                invalid("invalid or duplicate help-line identity");
            const auto size = reader.integer(4);
            if (size != 512u * 16u * 4u) invalid("invalid help-line pixel payload");
            const auto pixels = reader.take(size);
            run.rgba.assign(pixels.begin(), pixels.end());
            pack.runs_.push_back(std::move(run));
        }
    }
    if (magic == "SSTEXPK4" || magic == "SSTEXPK5") {
        const auto count = reader.integer(2);
        if (count > 512) invalid("too many compact messages");
        std::set<std::uint32_t> pointers;
        std::size_t total = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            DialogueEntry entry;
            entry.id = reader.name(128);
            if (!ids.insert(entry.id).second) invalid("duplicate entry ID");
            entry.pointer_offset = reader.integer(4);
            entry.source_offset = reader.integer(4);
            entry.source_size = reader.integer(4);
            if (entry.pointer_offset % 4 != 0 || entry.pointer_offset > 4u * 1024u * 1024u - 4u ||
                !pointers.insert(entry.pointer_offset).second || entry.source_size < 2 || entry.source_size > 16384 ||
                entry.source_offset > 4u * 1024u * 1024u - entry.source_size)
                invalid("invalid dialogue source/lookup slot");
            entry.source_hash = reader.digest();
            const auto size = reader.integer(4);
            if (size < 2 || size > 16384 || (total += size) > 512u * 1024u) invalid("oversized dialogue payload");
            const auto bytes = reader.take(size);
            entry.encoded.assign(bytes.begin(), bytes.end());
            pack.dialogue_.push_back(std::move(entry));
        }
    }
    if (magic == "SSTEXPK5") {
        const auto count = reader.integer(2);
        if (count > pack.entries_.size()) invalid("too many HD layers");
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto id = reader.name(128);
            auto entry = std::ranges::find(pack.entries_, id, &Entry::id);
            if (entry == pack.entries_.end() || entry->hd_scale) invalid("unknown or duplicate HD layer");
            const auto scale = reader.integer(2);
            if (scale != 2 && scale != 4 && scale != 8) invalid("invalid HD scale");
            const auto size = reader.integer(4);
            if (!size || size % 6) invalid("invalid HD runs");
            const auto runs = reader.take(size);
            Reader pixels(runs);
            const std::size_t expected = std::size_t(entry->width) * entry->height * scale * scale;
            std::size_t total = 0;
            while (!pixels.empty()) {
                const auto length = pixels.integer(2);
                (void)pixels.integer(4);
                if (!length || length > expected - total) invalid("HD run exceeds image");
                total += length;
            }
            if (total != expected) invalid("short HD image");
            entry->hd_scale = static_cast<std::uint16_t>(scale);
            entry->hd_runs.assign(runs.begin(), runs.end());
        }
    }
    if (!reader.empty()) invalid("trailing data");
    return pack;
}

namespace {
// Preserve printf conversion order/types. No new conversions may be introduced.
std::vector<std::string> placeholders(std::string_view text) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') continue;
        const auto start = i++;
        if (i < text.size() && text[i] == '%') continue;
        while (i < text.size() && std::string_view("-+ #0.123456789hlz").find(text[i]) != std::string_view::npos) ++i;
        if (i == text.size() || std::string_view("sdiuXxfc").find(text[i]) == std::string_view::npos)
            invalid("unsupported formatting token");
        result.emplace_back(text.substr(start, i - start + 1));
    }
    return result;
}
}

void TexturePack::apply_strings(psprecomp::GuestMemory &memory, const psprecomp::Elf32Image &elf,
                                std::uint32_t load_base) const {
    std::vector<std::uint32_t> addresses;
    for (const auto &entry : strings_) {
        // Restrict the adapter to immutable, non-executable .rodata strings.
        bool rodata = false;
        for (const auto &section : elf.sections()) {
            if (section.name == ".rodata" && section.type == 1 && section.flags == 2 &&
                entry.offset >= section.offset &&
                static_cast<std::uint64_t>(entry.offset) + entry.capacity <= static_cast<std::uint64_t>(section.offset) + section.size)
                rodata = true;
        }
        if (!rodata) invalid("string outside read-only text data: " + entry.id);
        std::optional<std::uint32_t> address;
        for (std::size_t index = 0; index < elf.segments().size(); ++index) {
            const auto &segment = elf.segments()[index];
            if (segment.type != 1 || entry.offset < segment.offset ||
                static_cast<std::uint64_t>(entry.offset) + entry.capacity > static_cast<std::uint64_t>(segment.offset) + segment.file_size) continue;
            const auto target = static_cast<std::uint64_t>(elf.segment_runtime_address(index, load_base)) + entry.offset - segment.offset;
            if (target > UINT32_MAX || !memory.contains(static_cast<std::uint32_t>(target), entry.capacity) || address)
                invalid("ambiguous or unmapped string slot");
            address = static_cast<std::uint32_t>(target);
        }
        if (!address) invalid("unmapped string slot");
        std::vector<std::uint8_t> original(entry.capacity);
        for (std::uint32_t i = 0; i < entry.capacity; ++i) original[i] = memory.load8(*address + i);
        const auto terminator = std::find(original.begin(), original.end(), 0);
        if (terminator == original.begin() || terminator == original.end() ||
            (!padded_strings_ && terminator != original.end() - 1) ||
            std::any_of(terminator, original.end(), [](auto byte) { return byte != 0; }) ||
            psprecomp::sha256(original) != entry.source_hash)
            invalid("string source mismatch: " + entry.id);
        const std::string_view source(reinterpret_cast<const char *>(original.data()),
                                      static_cast<std::size_t>(terminator - original.begin()));
        if (placeholders(source) != placeholders(entry.replacement)) invalid("formatting tokens changed: " + entry.id);
        addresses.push_back(*address);
    }
    // Validation is transactional: a bad later entry cannot leave earlier slots changed.
    for (std::size_t index = 0; index < strings_.size(); ++index) {
        const auto &entry = strings_[index];
        for (std::uint32_t i = 0; i < entry.capacity; ++i)
            memory.store8(addresses[index] + i, i < entry.replacement.size() ? static_cast<std::uint8_t>(entry.replacement[i]) : 0);
    }
}

bool TexturePack::apply(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels) const {
    return apply_scaled(texture, pixels, 1) != 0;
}

std::uint32_t TexturePack::apply_scaled(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels,
                                       std::uint32_t requested_scale) const {
    if (requested_scale < 1 || requested_scale > 8 ||
        pixels.size() != static_cast<std::size_t>(texture.width) * texture.height) return 0;
    for (const auto &entry : entries_) {
        if (texture.width != entry.ge_width || texture.height != entry.ge_height || texture.buffer_width != entry.buffer_width) continue;
        std::vector<std::uint8_t> source;
        source.reserve(entry.rgba.size());
        for (std::size_t y = 0; y < entry.height; ++y) for (std::size_t x = 0; x < entry.width; ++x) {
            const auto value = pixels[y * texture.width + x];
            for (unsigned shift = 0; shift < 32; shift += 8) source.push_back(static_cast<std::uint8_t>(value >> shift));
        }
        if (psprecomp::sha256(source) != entry.source_hash) continue;
        if (requested_scale > 1 && entry.hd_scale) {
            // Select a mip at least as sharp as the render target. Non-power-
            // of-two render scales use the next mip; normalized UVs stay fixed.
            std::uint32_t scale = 2;
            while (scale < requested_scale && scale < entry.hd_scale) scale *= 2;
            std::vector<std::uint32_t> hd;
            hd.reserve(std::size_t(entry.width) * entry.height * entry.hd_scale * entry.hd_scale);
            Reader runs(entry.hd_runs);
            while (!runs.empty()) {
                const auto count = runs.integer(2), pixel = runs.integer(4);
                hd.insert(hd.end(), count, pixel);
            }
            const auto width = texture.width * scale, height = texture.height * scale;
            std::vector<std::uint32_t> scaled(std::size_t(width) * height);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                    scaled[std::size_t(y) * width + x] = pixels[std::size_t(y / scale) * texture.width + x / scale];
            const auto step = entry.hd_scale / scale;
            const auto hd_width = entry.width * entry.hd_scale;
            for (const auto &region : entry.regions)
                for (std::uint32_t y = region.y * scale; y < (region.y + region.height) * scale; ++y)
                    for (std::uint32_t x = region.x * scale; x < (region.x + region.width) * scale; ++x) {
                        std::uint32_t sums[4]{};
                        for (std::uint32_t dy = 0; dy < step; ++dy)
                            for (std::uint32_t dx = 0; dx < step; ++dx) {
                                const auto pixel = hd[std::size_t(y * step + dy) * hd_width + x * step + dx];
                                const auto alpha = pixel >> 24;
                                for (unsigned c = 0; c < 3; ++c) sums[c] += ((pixel >> (8 * c)) & 255u) * alpha;
                                sums[3] += alpha;
                            }
                        std::uint32_t value = ((sums[3] + step * step / 2) / (step * step)) << 24;
                        if (sums[3])
                            for (unsigned c = 0; c < 3; ++c)
                                value |= ((sums[c] + sums[3] / 2) / sums[3]) << (8 * c);
                        scaled[std::size_t(y) * width + x] = value;
                    }
            pixels = std::move(scaled);
            return scale;
        }
        // Preserve icons, backgrounds, untranslated labels and padding outside the authored regions.
        for (const auto &region : entry.regions)
        for (std::size_t y = region.y; y < region.y + region.height; ++y)
        for (std::size_t x = region.x; x < region.x + region.width; ++x) {
            const auto offset = (y * entry.width + x) * 4;
            std::uint32_t value = 0;
            for (unsigned byte = 0; byte < 4; ++byte) value |= std::uint32_t(entry.rgba[offset + byte]) << (byte * 8);
            pixels[y * texture.width + x] = value;
        }
        return 1;
    }
    return 0;
}

} // namespace sarusaru::translation
