#include "textures/hd_pack.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
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
        if (entry.is_regular_file() && path.extension() == ".png" && is_hash(path.stem().string()))
            pack.images_.insert(path.stem().string());
    }
    return pack;
}

std::optional<std::vector<std::uint32_t>> HdTexturePack::find(const gpu::TextureIdentity &identity) const {
    if (identity.hash.empty() || !images_.contains(identity.hash)) return std::nullopt;
    const std::uint64_t width = static_cast<std::uint64_t>(identity.width) * manifest_.scale;
    const std::uint64_t height = static_cast<std::uint64_t>(identity.height) * manifest_.scale;
    const auto reject = [&](const std::string &reason) -> std::optional<std::vector<std::uint32_t>> {
        if (rejected_.insert(identity.hash).second)
            std::cerr << "sarusaru: HD texture " << identity.hash << " skipped: " << reason << '\n';
        return std::nullopt;
    };
    if (width > kMaxDimension || height > kMaxDimension) return reject("larger than " + std::to_string(kMaxDimension));
    const auto path = directory_ / (identity.hash + ".png");
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
    return texels;
}

} // namespace sarusaru::textures
