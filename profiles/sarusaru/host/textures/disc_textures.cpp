#include "textures/disc_textures.hpp"

#include "umd/disc_image.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <set>
#include <stdexcept>

namespace sarusaru::textures {
namespace {

[[noreturn]] void invalid(const std::string &reason) { throw std::runtime_error(reason); }

std::uint32_t le32(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint32_t>(data[at]) | static_cast<std::uint32_t>(data[at + 1]) << 8u |
           static_cast<std::uint32_t>(data[at + 2]) << 16u | static_cast<std::uint32_t>(data[at + 3]) << 24u;
}
std::uint16_t le16(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(data[at] | data[at + 1] << 8u);
}

bool safe_member_name(std::string_view name) {
    return !name.empty() && name != "." && name != ".." &&
           std::ranges::none_of(name, [](char c) { return c == '/' || c == '\\' || c == ':' || c == '|' || c == '!' ||
                                                          c == '#' || static_cast<unsigned char>(c) < 33 ||
                                                          static_cast<unsigned char>(c) > 126; });
}

} // namespace

std::vector<std::uint8_t> decode_lzs(std::span<const std::uint8_t> data) {
    if (data.size() < 4u) invalid("truncated LZS header");
    const std::uint32_t size = le32(data, 0);
    if (size == 0u || size > kMaxAssetBytes) invalid("invalid LZS output size");
    std::array<std::uint8_t, 4096> ring{};
    std::uint32_t cursor = 4078u;
    std::size_t at = 4u;
    std::vector<std::uint8_t> result;
    result.reserve(size);
    const auto put = [&](std::uint8_t value) {
        result.push_back(value);
        ring[cursor] = value;
        cursor = (cursor + 1u) & 4095u;
    };
    while (result.size() < size) {
        if (at >= data.size()) invalid("truncated LZS flags");
        const std::uint8_t flags = data[at++];
        for (unsigned bit = 0; bit < 8u && result.size() < size; ++bit) {
            if ((flags & (1u << bit)) != 0u) {
                if (at >= data.size()) invalid("truncated LZS token");
                put(data[at++]);
                continue;
            }
            if (at + 2u > data.size()) invalid("truncated LZS token");
            const std::uint8_t low = data[at], high = data[at + 1u];
            at += 2u;
            const std::uint32_t length = (high & 15u) + 3u;
            const std::uint32_t start = low | ((high & 240u) << 4u);
            if (result.size() + length > size) invalid("LZS token exceeds declared output");
            for (std::uint32_t j = 0; j < length; ++j) put(ring[(start + j) & 4095u]);
        }
    }
    if (at != data.size()) invalid("trailing LZS bytes");
    return result;
}

std::map<std::string, std::span<const std::uint8_t>> pac_members(std::span<const std::uint8_t> data) {
    if (data.size() < 48u || data.size() > kMaxAssetBytes) invalid("invalid PAC size");
    const std::uint32_t count = le16(data, 0);
    if (count == 0u || count > 4096u || count * 48u > data.size()) invalid("invalid PAC table count");
    std::map<std::string, std::span<const std::uint8_t>> result;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = i * 48u;
        const std::uint32_t offset = le32(data, entry + 4u), size = le32(data, entry + 8u), ordinal = le32(data, entry + 12u);
        const auto raw = data.subspan(entry + 16u, 32u);
        const auto end = std::find(raw.begin(), raw.end(), std::uint8_t{0});
        const std::string name(raw.begin(), end);
        if (!safe_member_name(name) || result.contains(name) || ordinal != i) invalid("invalid or duplicate PAC member");
        if (offset < count * 48u || size == 0u || static_cast<std::uint64_t>(offset) + size > data.size())
            invalid("PAC member outside payload");
        ranges.emplace_back(offset, offset + size);
        result.emplace(name, data.subspan(offset, size));
    }
    std::ranges::sort(ranges);
    for (std::size_t i = 1; i < ranges.size(); ++i)
        if (ranges[i - 1].second > ranges[i].first) invalid("overlapping PAC payloads");
    return result;
}

gpu::TextureIdentity Picture::identity() const {
    gpu::TextureState state;
    state.width = width;
    state.height = height;
    state.buffer_width = width;
    return gpu::identify_texture(state, texels);
}

std::optional<Picture> decode_tim2(std::span<const std::uint8_t> data) {
    if (data.size() < 64u || data[0] != 'T' || data[1] != 'I' || data[2] != 'M' || data[3] != '2')
        invalid("not a TIM2 file");
    // Version 4, the PSP's alignment, one picture.
    if (data[4] != 4u || data[5] != 0u || le16(data, 6) != 1u) return std::nullopt;
    const auto picture = data.subspan(16u);
    const std::uint32_t total = le32(picture, 0), palette_size = le32(picture, 4), image_size = le32(picture, 8);
    const std::uint16_t header_size = le16(picture, 12), colors = le16(picture, 14);
    const std::uint8_t mips = picture[17], clut_type = picture[18], image_type = picture[19];
    const std::uint16_t width = le16(picture, 20), height = le16(picture, 22);
    if (image_type != 4u && image_type != 5u) return std::nullopt;  // 4- or 8-bit indexed only
    if ((clut_type & 0x3Fu) != 3u) return std::nullopt;               // 32-bit palette entries only
    const unsigned bits = image_type == 4u ? 4u : 8u;
    if (width == 0u || height == 0u || width > 512u || height > 512u || mips == 0u || header_size < 48u)
        return std::nullopt;
    const std::uint32_t row_bytes = width * bits / 8u;
    if (row_bytes % 16u != 0u || height % 8u != 0u) return std::nullopt;  // swizzled in 16x8-byte blocks
    if (colors < (1u << bits) || palette_size < colors * 4u || image_size < row_bytes * height ||
        static_cast<std::uint64_t>(header_size) + image_size + palette_size > total || 16u + std::uint64_t{total} > data.size())
        invalid("inconsistent TIM2 sizes");
    const auto raw = picture.subspan(header_size, row_bytes * height);
    const auto palette = picture.subspan(header_size + image_size, palette_size);
    // CSM1 palettes (bit 6 clear) store 8-bit entries in blocks of 8 swapped
    // in pairs; the game unswaps them when it loads the palette.
    const bool csm1 = (clut_type & 0x40u) == 0u;
    Picture result;
    result.width = width;
    result.height = height;
    result.texels.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t byte_x = x * bits / 8u;
            const std::uint32_t index = ((y / 8u) * (row_bytes / 16u) + byte_x / 16u) * 128u + (y % 8u) * 16u + byte_x % 16u;
            std::uint32_t color = bits == 4u ? (raw[index] >> ((x % 2u) * 4u)) & 15u : raw[index];
            if (bits == 8u && csm1) color = (color & ~24u) | ((color & 8u) << 1u) | ((color & 16u) >> 1u);
            result.texels[static_cast<std::size_t>(y) * width + x] = le32(palette, color * 4u);
        }
    return result;
}

SourceLocator parse_locator(std::string_view text) {
    const auto hash = text.rfind('#');
    if (hash == std::string_view::npos || hash + 1u == text.size()) invalid("source locator needs #offset: " + std::string(text));
    SourceLocator locator;
    const auto offset_text = text.substr(hash + 1u);
    const auto [end, error] = std::from_chars(offset_text.data(), offset_text.data() + offset_text.size(), locator.offset, 16);
    if (error != std::errc{} || end != offset_text.data() + offset_text.size() || locator.offset >= kMaxAssetBytes)
        invalid("invalid source offset: " + std::string(text));
    std::string_view rest = text.substr(0, hash);
    const auto first = rest.find_first_of("!|");
    locator.path = std::string(rest.substr(0, first));
    if (locator.path.empty() || locator.path.front() == '/' || locator.path.find("..") != std::string::npos ||
        locator.path.find('\\') != std::string::npos || locator.path.find("//") != std::string::npos)
        invalid("invalid source path: " + std::string(text));
    rest = first == std::string_view::npos ? std::string_view{} : rest.substr(first);
    while (!rest.empty()) {
        const char kind = rest.front();
        rest.remove_prefix(1);
        const auto next = rest.find_first_of("!|");
        const auto part = rest.substr(0, next);
        rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next);
        if (kind == '!' && part == "lzs") {
            locator.steps.push_back({SourceLocator::Step::Kind::Lzs, {}});
        } else if (kind == '|' && safe_member_name(part)) {
            locator.steps.push_back({SourceLocator::Step::Kind::Member, std::string(part)});
        } else {
            invalid("invalid source step: " + std::string(text));
        }
        if (locator.steps.size() > 8u) invalid("too many source steps: " + std::string(text));
    }
    return locator;
}

std::string format_locator(const SourceLocator &locator) {
    std::string text = locator.path;
    for (const auto &step : locator.steps) text += step.kind == SourceLocator::Step::Kind::Lzs ? "!lzs" : "|" + step.member;
    char offset[16];
    const auto [end, error] = std::to_chars(offset, offset + sizeof offset, locator.offset, 16);
    return text + "#" + std::string(offset, end);
}

Picture DiscTextureReader::read(const SourceLocator &locator) {
    if (locator.path != path_) {
        const auto entry = disc_.find(locator.path);
        if (!entry || entry->directory || entry->size > kMaxAssetBytes) invalid("not on the disc: " + locator.path);
        file_ = disc_.read_file(*entry, static_cast<std::uint32_t>(kMaxAssetBytes));
        path_ = locator.path;
        expanded_.clear();
    }
    std::span<const std::uint8_t> bytes = file_;
    std::string prefix = locator.path;
    for (const auto &step : locator.steps) {
        if (step.kind == SourceLocator::Step::Kind::Lzs) {
            prefix += "!lzs";
            auto found = expanded_.find(prefix);
            if (found == expanded_.end()) found = expanded_.emplace(prefix, decode_lzs(bytes)).first;
            bytes = found->second;
        } else {
            prefix += "|" + step.member;
            const auto members = pac_members(bytes);
            const auto member = members.find(step.member);
            if (member == members.end()) invalid("no member " + step.member + " in " + locator.path);
            bytes = member->second;
        }
    }
    if (locator.offset >= bytes.size()) invalid("source offset outside " + format_locator(locator));
    auto picture = decode_tim2(bytes.subspan(locator.offset));
    if (!picture) invalid("unsupported TIM2 layout at " + format_locator(locator));
    return std::move(*picture);
}

namespace {

bool ends_with(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.begin(), suffix.end(), text.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

struct Scan {
    const std::function<bool(const SourceLocator &, Picture &&)> &found;
    bool stopped{};

    // Pictures in `bytes`, reached by `locator` (its offset not yet set).
    void visit(std::span<const std::uint8_t> bytes, SourceLocator locator, int depth) {
        if (stopped) return;
        std::optional<std::map<std::string, std::span<const std::uint8_t>>> members;
        try {
            members = pac_members(bytes);
        } catch (const std::runtime_error &) {
        }
        if (members) {
            if (depth >= 4) return;
            for (const auto &[name, member] : *members) {
                auto inner = locator;
                inner.steps.push_back({SourceLocator::Step::Kind::Member, name});
                if (ends_with(name, ".lzs")) {
                    try {
                        const auto expanded = decode_lzs(member);
                        inner.steps.push_back({SourceLocator::Step::Kind::Lzs, {}});
                        visit(expanded, inner, depth + 1);
                        continue;
                    } catch (const std::runtime_error &) {
                    }
                }
                visit(member, std::move(inner), depth + 1);
            }
            return;
        }
        constexpr std::array<std::uint8_t, 4> kMagic{'T', 'I', 'M', '2'};
        for (auto at = std::search(bytes.begin(), bytes.end(), kMagic.begin(), kMagic.end()); at != bytes.end() && !stopped;
             at = std::search(at + 4, bytes.end(), kMagic.begin(), kMagic.end())) {
            const auto offset = static_cast<std::uint32_t>(at - bytes.begin());
            std::optional<Picture> picture;
            try {
                picture = decode_tim2(bytes.subspan(offset));
            } catch (const std::runtime_error &) {
            }
            if (!picture || (picture->height & (picture->height - 1u)) != 0u) continue;
            locator.offset = offset;
            if (!found(locator, std::move(*picture))) stopped = true;
        }
    }
};

void scan_directory(umd::DiscImage &disc, const umd::DiscImage::Entry &directory, const std::string &prefix, Scan &scan,
                    int depth) {
    if (depth > 16) return;
    for (const auto &entry : disc.list(directory)) {
        if (scan.stopped) return;
        const std::string path = prefix + "/" + entry.name;
        if (entry.directory) {
            scan_directory(disc, entry, path, scan, depth + 1);
            continue;
        }
        if (entry.size < 64u || entry.size > kMaxAssetBytes) continue;
        // Movies, music, sound banks and tables hold no pictures.
        if (ends_with(path, ".pmf") || ends_with(path, ".at3") || ends_with(path, ".sgb") || ends_with(path, ".sgh") ||
            ends_with(path, ".csv") || ends_with(path, ".png") || ends_with(path, ".bin") || ends_with(path, ".prx"))
            continue;
        std::vector<std::uint8_t> bytes;
        try {
            bytes = disc.read_file(entry, static_cast<std::uint32_t>(kMaxAssetBytes));
        } catch (const std::exception &) {
            continue;
        }
        SourceLocator locator;
        locator.path = path;
        if (ends_with(path, ".lzs") || ends_with(path, ".pac")) {
            try {
                const auto expanded = decode_lzs(bytes);
                locator.steps.push_back({SourceLocator::Step::Kind::Lzs, {}});
                scan.visit(expanded, std::move(locator), 0);
                continue;
            } catch (const std::runtime_error &) {
            }
        }
        scan.visit(bytes, std::move(locator), 0);
    }
}

} // namespace

void scan_disc(umd::DiscImage &disc, std::string_view directory,
               const std::function<bool(const SourceLocator &, Picture &&)> &found) {
    const auto entry = disc.find(directory);
    if (!entry || !entry->directory) invalid("no directory " + std::string(directory) + " on the disc");
    Scan scan{found};
    std::string prefix(directory);
    while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    scan_directory(disc, *entry, prefix, scan, 0);
}

Picture read_picture(umd::DiscImage &disc, const SourceLocator &locator) {
    DiscTextureReader reader(disc);
    return reader.read(locator);
}

} // namespace sarusaru::textures
