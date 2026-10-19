#include "textures/hd_pack.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <zstd.h>

#include <charconv>
#include <functional>
#include <thread>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace sarusaru::textures {
namespace {

[[noreturn]] void invalid(const std::string &reason) { throw std::runtime_error("HD texture pack: " + reason); }

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// decoded/<hash>.zst: "SSHC", version, width, height, the PNG's size and
// modification time (so a changed PNG is not answered from the cache), then
// the texels (red in the low byte) as one zstd frame.
constexpr std::uint32_t kCacheMagic = 0x43485353u, kCacheVersion = 1u;
constexpr std::size_t kCacheHeader = 32u;

struct PngStamp {
    std::uint64_t size{}, time{};
};

std::optional<PngStamp> stamp(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    return PngStamp{size, static_cast<std::uint64_t>(time.time_since_epoch().count())};
}

void put32(std::uint8_t *at, std::uint32_t value) { std::memcpy(at, &value, 4u); }
void put64(std::uint8_t *at, std::uint64_t value) { std::memcpy(at, &value, 8u); }
std::uint32_t get32(const std::uint8_t *at) { std::uint32_t value; std::memcpy(&value, at, 4u); return value; }
std::uint64_t get64(const std::uint8_t *at) { std::uint64_t value; std::memcpy(&value, at, 8u); return value; }

std::optional<std::vector<std::uint32_t>> read_cache(const std::filesystem::path &path, std::uint64_t width,
                                                     std::uint64_t height, const PngStamp &png) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const auto size = static_cast<std::size_t>(in.tellg());
    std::vector<std::uint8_t> bytes(size);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size))) return std::nullopt;
    if (bytes.size() <= kCacheHeader || get32(bytes.data()) != kCacheMagic || get32(bytes.data() + 4) != kCacheVersion ||
        get32(bytes.data() + 8) != width || get32(bytes.data() + 12) != height || get64(bytes.data() + 16) != png.size ||
        get64(bytes.data() + 24) != png.time)
        return std::nullopt;
    std::vector<std::uint32_t> texels(static_cast<std::size_t>(width * height));
    const std::size_t written = ZSTD_decompress(texels.data(), texels.size() * 4u, bytes.data() + kCacheHeader,
                                                bytes.size() - kCacheHeader);
    if (ZSTD_isError(written) || written != texels.size() * 4u) return std::nullopt;
    return texels;
}

bool write_cache(const std::filesystem::path &path, std::span<const std::uint32_t> texels, std::uint64_t width,
                 std::uint64_t height, const PngStamp &png) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::vector<std::uint8_t> bytes(kCacheHeader + ZSTD_compressBound(texels.size_bytes()));
    put32(bytes.data(), kCacheMagic);
    put32(bytes.data() + 4, kCacheVersion);
    put32(bytes.data() + 8, static_cast<std::uint32_t>(width));
    put32(bytes.data() + 12, static_cast<std::uint32_t>(height));
    put64(bytes.data() + 16, png.size);
    put64(bytes.data() + 24, png.time);
    const std::size_t size = ZSTD_compress(bytes.data() + kCacheHeader, bytes.size() - kCacheHeader, texels.data(),
                                           texels.size_bytes(), 3);
    if (ZSTD_isError(size)) return false;
    // Written beside and renamed over: a reader never sees half a file, and
    // two threads decoding one image do not write into each other's.
    auto temporary = path;
    temporary += "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(kCacheHeader + size));
        if (!out) {
            out.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) std::filesystem::remove(temporary, error);
    return !error;
}

bool is_hash(std::string_view name) {
    return name.size() == 32u && std::ranges::all_of(name, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

} // namespace

HdManifest parse_hd_manifest(std::string_view text) {
    HdManifest manifest;
    bool format = false;
    std::unordered_set<std::string> keys;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1u);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) invalid("expected key = value: " + std::string(line));
        const std::string key(trim(line.substr(0, equals)));
        const auto value = trim(line.substr(equals + 1u));
        if (!keys.insert(key).second) invalid("duplicate key: " + key);
        const auto number = [&](std::uint32_t low, std::uint32_t high) {
            std::uint32_t parsed{};
            const auto [last, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || last != value.data() + value.size() || parsed < low || parsed > high)
                invalid(key + " must be " + std::to_string(low) + "-" + std::to_string(high));
            return parsed;
        };
        if (key == "format") {
            number(1u, 1u);
            format = true;
        } else if (key == "disc") {
            if (value.size() != 9u) invalid("disc must be a 9-character disc ID");
            manifest.disc_id = value;
        } else if (key == "scale") {
            manifest.scale = number(2u, 8u);
        } else {
            invalid("unknown key: " + key);
        }
    }
    if (!format || manifest.disc_id.empty() || manifest.scale == 0u) invalid("needs format, disc and scale");
    return manifest;
}

std::string serialize_hd_manifest(const HdManifest &manifest) {
    std::ostringstream out;
    out << "# SaruSaru HD textures, upscaled on this device from the player's disc.\n"
        << "format = 1\ndisc = " << manifest.disc_id << "\nscale = " << manifest.scale << '\n';
    return out.str();
}

HdTexturePack HdTexturePack::load(const std::filesystem::path &directory, std::string_view disc_id) {
    std::ifstream in(directory / kManifestName, std::ios::binary);
    if (!in) invalid("no " + std::string(kManifestName) + " in " + directory.string());
    std::stringstream text;
    text << in.rdbuf();
    HdTexturePack pack;
    pack.directory_ = directory;
    pack.manifest_ = parse_hd_manifest(text.str());
    if (pack.manifest_.disc_id != disc_id) invalid("made for " + pack.manifest_.disc_id + ", not " + std::string(disc_id));
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        const auto &path = entry.path();
        if (!entry.is_regular_file() || path.extension() != ".png" || !is_hash(path.stem().string())) continue;
        pack.images_.insert(path.stem().string());
        // The PNG signature, then IHDR: width and height, big-endian.
        std::ifstream png(path, std::ios::binary);
        unsigned char header[24]{};
        const auto scale = pack.manifest_.scale;
        if (!png.read(reinterpret_cast<char *>(header), sizeof header) || std::memcmp(header + 12, "IHDR", 4) != 0) {
            pack.any_size_ = true;
            continue;
        }
        const auto word = [&](int at) {
            return std::uint32_t{header[at]} << 24u | std::uint32_t{header[at + 1]} << 16u |
                   std::uint32_t{header[at + 2]} << 8u | header[at + 3];
        };
        const std::uint32_t width = word(16), height = word(20);
        // Sized wrongly, find() rejects it anyway.
        if (width % scale == 0u && height % scale == 0u && width / scale <= 0xFFFFu && height / scale <= 0xFFFFu)
            pack.sizes_.insert((width / scale) << 16u | height / scale);
    }
    return pack;
}

std::optional<std::unordered_set<std::string>> HdTexturePack::wanted() const {
    std::ifstream in(directory_ / kWantedName);
    if (!in) return std::nullopt;
    std::unordered_set<std::string> hashes;
    for (std::string line; std::getline(in, line);) {
        const auto hash = std::string(trim(line));
        if (is_hash(hash) && !images_.contains(hash)) hashes.insert(hash);
    }
    if (hashes.empty()) return std::nullopt;
    return hashes;
}

std::optional<std::vector<std::uint32_t>> HdTexturePack::find(const gpu::TextureIdentity &identity) const {
    if (identity.hash.empty() || !images_.contains(identity.hash)) return std::nullopt;
    const std::uint64_t width = static_cast<std::uint64_t>(identity.width) * manifest_.scale;
    const std::uint64_t height = static_cast<std::uint64_t>(identity.height) * manifest_.scale;
    const auto reject = [&](const std::string &reason) -> std::optional<std::vector<std::uint32_t>> {
        const std::lock_guard lock(rejected_->mutex);
        if (rejected_->hashes.insert(identity.hash).second)
            std::cerr << "sarusaru: HD texture " << identity.hash << " skipped: " << reason << '\n';
        return std::nullopt;
    };
    if (width > kMaxDimension || height > kMaxDimension) return reject("larger than " + std::to_string(kMaxDimension));
    const auto path = directory_ / (identity.hash + ".png");
    const auto png = stamp(path);
    const auto cached = directory_ / kDecodedName / (identity.hash + ".zst");
    if (png)
        if (auto texels = read_cache(cached, width, height, *png)) return texels;
    SDL_Surface *loaded = SDL_LoadPNG(path.string().c_str());
    if (loaded == nullptr) return reject(SDL_GetError());
    SDL_Surface *surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (surface == nullptr) return reject(SDL_GetError());
    if (static_cast<std::uint64_t>(surface->w) != width || static_cast<std::uint64_t>(surface->h) != height) {
        const auto size = std::to_string(surface->w) + "x" + std::to_string(surface->h);
        SDL_DestroySurface(surface);
        return reject(size + ", expected " + std::to_string(width) + "x" + std::to_string(height));
    }
    std::vector<std::uint32_t> texels(static_cast<std::size_t>(width * height));
    const auto *rows = static_cast<const std::uint8_t *>(surface->pixels);
    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t *row = rows + y * static_cast<std::size_t>(surface->pitch);
        for (std::size_t x = 0; x < width; ++x) {
            std::uint32_t value = 0;
            for (unsigned byte = 0; byte < 4u; ++byte) value |= static_cast<std::uint32_t>(row[x * 4u + byte]) << (byte * 8u);
            texels[y * width + x] = value;
        }
    }
    SDL_DestroySurface(surface);
    if (png && !cache_failed_->load(std::memory_order_relaxed) && !write_cache(cached, texels, width, height, *png))
        cache_failed_->store(true, std::memory_order_relaxed);
    return texels;
}

} // namespace sarusaru::textures
