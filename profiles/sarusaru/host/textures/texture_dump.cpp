#include "textures/texture_dump.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace sarusaru::textures {

bool write_png(const std::filesystem::path &path, std::span<const std::uint32_t> texels, std::uint32_t width,
               std::uint32_t height, std::uint32_t stride) {
    if (width == 0u || height == 0u || stride < width || texels.size() < static_cast<std::size_t>(stride) * (height - 1u) + width)
        return false;
    // SDL's RGBA32 is byte order R, G, B, A: the texels' layout as bytes.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
            const std::uint32_t value = texels[y * stride + x];
            for (unsigned byte = 0; byte < 4u; ++byte)
                bytes[(y * width + x) * 4u + byte] = static_cast<std::uint8_t>(value >> (byte * 8u));
        }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32,
                                                 bytes.data(), static_cast<int>(width * 4u));
    if (surface == nullptr) return false;
    // Written beside the target and renamed, so a dump cut short never
    // leaves a truncated image under a valid name.
    auto partial = path;
    partial += ".partial";
    const bool saved = SDL_SavePNG(surface, partial.string().c_str());
    SDL_DestroySurface(surface);
    std::error_code error;
    if (saved) std::filesystem::rename(partial, path, error);
    if (!saved || error) {
        std::filesystem::remove(partial, error);
        return false;
    }
    return true;
}

TextureDump::TextureDump(std::filesystem::path directory, std::optional<std::unordered_set<std::string>> wanted)
    : directory_(std::move(directory)), wanted_(std::move(wanted)) {
    std::filesystem::create_directories(directory_);
    std::ifstream index(directory_ / kIndexName);
    for (std::string line; std::getline(index, line);) {
        const auto hash = line.substr(0, line.find('\t'));
        if (hash.size() == 32u && std::filesystem::exists(directory_ / (hash + ".png"))) seen_.insert(hash);
    }
}

void TextureDump::record(const gpu::TextureState &texture, const gpu::TextureIdentity &identity,
                         std::span<const std::uint32_t> texels) {
    const std::lock_guard lock(mutex_);
    if (identity.hash.empty() || (wanted_ && !wanted_->contains(identity.hash)) || !seen_.insert(identity.hash).second) return;
    const bool saved = write_png(directory_ / (identity.hash + ".png"), texels, identity.width, identity.height, texture.width);
    if (saved) {
        const auto index_path = directory_ / kIndexName;
        const bool fresh = !std::filesystem::exists(index_path);
        std::ofstream index(index_path, std::ios::app);
        if (fresh) index << kIndexHeader << '\n';
        index << identity.hash << '\t' << identity.width << '\t' << identity.height << '\t' << texture.width << '\t'
              << texture.height << '\t' << texture.buffer_width << '\t' << static_cast<unsigned>(texture.format) << '\t'
              << (texture.swizzled ? 1 : 0) << '\n';
        if (index) {
            ++written_;
            return;
        }
    }
    if (!failed_) std::cerr << "sarusaru: cannot write textures to " << directory_.string() << ": " << SDL_GetError() << '\n';
    failed_ = true;
}

} // namespace sarusaru::textures
