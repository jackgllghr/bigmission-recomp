#include "translation/texture_pack.hpp"
#include "gpu/texture_hash.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <optional>
#include <stdexcept>

namespace sarusaru::translation {
namespace {
constexpr std::size_t kMaxPackBytes = 128u * 1024u * 1024u;
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

namespace {
// "SSTEXPK" and the format version, 1 to 8.
bool supported_magic(std::string_view magic) {
    return magic.size() == 8u && magic.starts_with("SSTEXPK") && magic[7] >= '1' && magic[7] <= '8';
}
} // namespace

bool TexturePack::readable_format(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    char magic[8]{};
    return in.read(magic, sizeof magic) && supported_magic(std::string_view(magic, sizeof magic));
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
    if (!supported_magic(magic)) invalid("unsupported format/version");
    const int version = magic[7] - '0';
    if (disc_id.size() != 9 || reader.text(9) != disc_id || reader.digest() != executable_hash) invalid("incompatible game");
    TexturePack pack;
    pack.padded_strings_ = version >= 3;
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
        if (version >= 2) {
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
    if (version >= 2) {
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
        if (runs > (version >= 8 ? kMaxGlyphRuns : 256u)) invalid("too many glyph-run replacements");
        // Pixel runs (u16 count, u32 RGBA) that must cover exactly `pixels`.
        const auto pixel_runs = [](std::span<const std::uint8_t> bytes, std::size_t pixels) {
            if (bytes.empty() || bytes.size() % 6) invalid("invalid help-line pixel runs");
            Reader runs(bytes);
            std::size_t total = 0;
            while (!runs.empty()) {
                const auto count = runs.integer(2);
                runs.integer(4);
                if (!count || count > pixels - total) invalid("help-line run exceeds image");
                total += count;
            }
            if (total != pixels) invalid("short help-line image");
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        };
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
            if (run.glyph_count == 0 || run.glyph_count > 64 ||
                !pack.run_index_.emplace(run.font_hash + run.signature, pack.runs_.size()).second)
                invalid("invalid or duplicate help-line identity");
            // Version 8 stores each line at its own width as pixel runs; older
            // packs store a raw 512-texel-wide image.
            run.image_width = version >= 8 ? run.width : 512u;
            const std::size_t pixels = std::size_t{run.image_width} * 16u;
            const auto size = reader.integer(4);
            if (version >= 8) {
                run.rgba_runs = pixel_runs(reader.take(size), pixels);
            } else {
                if (size != pixels * 4u) invalid("invalid help-line pixel payload");
                const auto raw = reader.take(size);
                for (std::size_t at = 0; at < raw.size();) {
                    std::uint16_t count = 0;
                    while (at + count * 4u < raw.size() && count < 0xFFFFu &&
                           std::equal(raw.begin() + at, raw.begin() + at + 4, raw.begin() + at + count * 4u)) ++count;
                    run.rgba_runs.push_back(static_cast<std::uint8_t>(count));
                    run.rgba_runs.push_back(static_cast<std::uint8_t>(count >> 8));
                    run.rgba_runs.insert(run.rgba_runs.end(), raw.begin() + at, raw.begin() + at + 4);
                    at += count * 4u;
                }
            }
            if (version >= 7) {
                const auto scale = reader.integer(2), size = reader.integer(4);
                if (scale != 2 && scale != 4 && scale != 8) invalid("invalid HD help-line layer");
                run.hd_runs = pixel_runs(reader.take(size), pixels * scale * scale);
                run.hd_scale = static_cast<std::uint16_t>(scale);
            }
            pack.runs_.push_back(std::move(run));
        }
    }
    if (version >= 4) {
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
    if (version >= 5) {
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
    // V6 adds the movies' subtitles: SubRip text, parsed by the shell.
    if (version >= 6) {
        const auto count = reader.integer(2);
        if (count > 64) invalid("too many subtitled movies");
        std::size_t total = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto key = reader.name(32);
            if (std::ranges::any_of(key, [](unsigned char c) { return !std::islower(c) && !std::isdigit(c) && c != '_' && c != '-'; }))
                invalid("invalid subtitle movie name");
            const auto size = reader.integer(4);
            if (size == 0 || size > 256u * 1024u || (total += size) > 2u * 1024u * 1024u) invalid("oversized subtitles");
            auto srt = reader.text(size);
            if (srt.find('\0') != srt.npos) invalid("subtitles must be text");
            if (!pack.subtitles_.emplace(std::move(key), std::move(srt)).second) invalid("duplicate subtitled movie");
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

const TexturePack::Entry *TexturePack::match(const gpu::TextureState &texture,
                                             std::span<const std::uint32_t> pixels) const {
    if (pixels.size() != static_cast<std::size_t>(texture.width) * texture.height) return nullptr;
    // Entries of one size share the source rectangle: hash it once per size.
    std::vector<std::pair<std::uint32_t, std::string>> hashes;
    for (const auto &entry : entries_) {
        if (texture.width != entry.ge_width || texture.height != entry.ge_height || texture.buffer_width != entry.buffer_width) continue;
        const std::uint32_t size = static_cast<std::uint32_t>(entry.width) << 16u | entry.height;
        const std::string *hash = nullptr;
        for (const auto &[known, value] : hashes)
            if (known == size) hash = &value;
        if (hash == nullptr) {
            gpu::Sha256 hasher;
            gpu::hash_texels(hasher, pixels, texture.width, entry.width, entry.height);
            hashes.emplace_back(size, hasher.hex());
            hash = &hashes.back().second;
        }
        if (*hash == entry.source_hash) return &entry;
    }
    return nullptr;
}

std::uint32_t TexturePack::output_scale(const Entry &entry, std::uint32_t requested_scale) {
    if (requested_scale <= 1 || !entry.hd_scale) return 1;
    // Select a mip at least as sharp as the render target. Non-power-of-two
    // render scales use the next mip; normalized UVs stay fixed.
    std::uint32_t scale = 2;
    while (scale < requested_scale && scale < entry.hd_scale) scale *= 2;
    return scale;
}

std::uint32_t TexturePack::scaled_match(const gpu::TextureState &texture, std::span<const std::uint32_t> pixels,
                                        std::uint32_t requested_scale) const {
    if (requested_scale < 1 || requested_scale > 8) return 0;
    const Entry *entry = match(texture, pixels);
    return entry != nullptr ? output_scale(*entry, requested_scale) : 0u;
}

std::shared_ptr<const std::vector<TexturePack::Run>> TexturePack::lettering(const Entry &entry, std::uint32_t scale) const {
    const std::pair key{static_cast<std::size_t>(&entry - entries_.data()), scale};
    {
        const std::lock_guard lock(lettering_->mutex);
        if (const auto found = lettering_->regions.find(key); found != lettering_->regions.end()) return found->second;
    }
    const std::uint32_t hd_scale = entry.hd_scale;
    const std::size_t hd_width = std::size_t(entry.width) * hd_scale;
    const std::size_t hd_height = std::size_t(entry.height) * hd_scale;
    // Only the lettering's rows the regions read are unpacked.
    std::vector<bool> needed(hd_height);
    for (const auto &region : entry.regions)
        for (std::size_t y = std::size_t(region.y) * hd_scale; y < std::size_t(region.y + region.height) * hd_scale && y < hd_height; ++y)
            needed[y] = true;
    std::vector<std::uint32_t> hd(hd_width * hd_height);
    std::size_t at = 0;
    Reader runs(entry.hd_runs);
    while (!runs.empty() && at < hd.size()) {
        const std::size_t count = runs.integer(2);
        const auto pixel = static_cast<std::uint32_t>(runs.integer(4));
        const std::size_t end = std::min(at + count, hd.size());
        for (std::size_t row = at / hd_width; row * hd_width < end; ++row) {
            if (!needed[row]) continue;
            const std::size_t from = std::max(at, row * hd_width), to = std::min(end, (row + 1u) * hd_width);
            std::fill(hd.begin() + std::ptrdiff_t(from), hd.begin() + std::ptrdiff_t(to), pixel);
        }
        at = end;
    }
    // Each output pixel averages its step x step block, weighted by alpha.
    auto out = std::make_shared<std::vector<Run>>();
    const auto push = [&](std::uint32_t texel) {
        if (!out->empty() && out->back().texel == texel) ++out->back().count;
        else out->push_back({1u, texel});
    };
    const std::uint32_t step = hd_scale / scale;
    for (const auto &region : entry.regions)
        for (std::uint32_t y = region.y * scale; y < (region.y + region.height) * scale; ++y)
            for (std::uint32_t x = region.x * scale; x < (region.x + region.width) * scale; ++x) {
                if (step == 1u) {
                    push(hd[std::size_t(y) * hd_width + x]);
                    continue;
                }
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
                push(value);
            }
    out->shrink_to_fit();
    const std::lock_guard lock(lettering_->mutex);
    return lettering_->regions.emplace(key, std::move(out)).first->second;
}

std::size_t TexturePack::prepare(std::uint32_t requested_scale) const {
    std::size_t texels = 0u;
    for (const auto &entry : entries_) {
        const std::uint32_t scale = output_scale(entry, requested_scale);
        if (scale > 1u) texels += lettering(entry, scale)->size() * sizeof(Run);
    }
    return texels;
}

std::uint32_t TexturePack::possible_scale(const gpu::TextureState &texture, std::uint32_t requested_scale) const {
    if (requested_scale < 1 || requested_scale > 8) return 0;
    std::uint32_t scale = 0u;
    for (const auto &entry : entries_)
        if (texture.width == entry.ge_width && texture.height == entry.ge_height && texture.buffer_width == entry.buffer_width)
            scale = std::max(scale, output_scale(entry, requested_scale));
    return scale;
}

std::uint32_t TexturePack::apply_scaled(const gpu::TextureState &texture, std::vector<std::uint32_t> &pixels,
                                       std::uint32_t requested_scale, const BaseArt *base) const {
    if (requested_scale < 1 || requested_scale > 8) return 0;
    const Entry *found = match(texture, pixels);
    if (found == nullptr) return 0;
    const Entry &entry = *found;
    const std::uint32_t scale = output_scale(entry, requested_scale);
    if (scale > 1) {
        const std::uint32_t width = texture.width * scale, height = texture.height * scale;
        std::vector<std::uint32_t> scaled(std::size_t(width) * height);
        const bool art = base != nullptr && base->scale && base->width && base->height &&
            base->texels.size() >= std::size_t(base->width) * base->height * base->scale * base->scale;
        const std::uint32_t art_width = art ? std::min<std::uint32_t>(base->width * scale, width) : 0u;
        const std::uint32_t art_height = art ? base->height * scale : 0u;
        for (std::uint32_t y = 0; y < height; ++y) {
            std::uint32_t *out = scaled.data() + std::size_t(y) * width;
            // A row made from the same source row (and art row) as the one
            // above is a copy of it.
            if (y % scale != 0u) {
                const bool art_row = y < art_height, art_above = y - 1u < art_height;
                const bool same_art = !art_row || (art_above && y * base->scale / scale == (y - 1u) * base->scale / scale);
                if (art_row == art_above && same_art) {
                    std::copy_n(out - width, width, out);
                    continue;
                }
            }
            std::uint32_t x = 0;
            if (y < art_height) {
                const std::uint32_t *row = base->texels.data() + std::size_t(y * base->scale / scale) * base->width * base->scale;
                if (base->scale == scale) {
                    std::copy_n(row, art_width, out);
                } else {
                    for (; x < art_width; ++x) out[x] = row[x * base->scale / scale];
                }
                x = art_width;
            }
            // Outside the art the source texels are enlarged.
            const std::uint32_t *source = pixels.data() + std::size_t(y / scale) * texture.width;
            for (std::uint32_t from = x / scale; x < width; ++from) {
                const std::uint32_t end = std::min(width, (from + 1u) * scale);
                std::fill(out + x, out + end, source[from]);
                x = end;
            }
        }
        const auto lettered = lettering(entry, scale);
        auto run = lettered->begin();
        std::uint32_t left = run != lettered->end() ? run->count : 0u;
        for (const auto &region : entry.regions)
            for (std::uint32_t y = region.y * scale; y < (region.y + region.height) * scale; ++y) {
                std::uint32_t *out = scaled.data() + std::size_t(y) * width + region.x * scale;
                for (std::uint32_t todo = region.width * scale; todo != 0u && run != lettered->end();) {
                    const std::uint32_t take = std::min(todo, left);
                    std::fill_n(out, take, run->texel);
                    out += take;
                    todo -= take;
                    if ((left -= take) == 0u && ++run != lettered->end()) left = run->count;
                }
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

} // namespace sarusaru::translation
