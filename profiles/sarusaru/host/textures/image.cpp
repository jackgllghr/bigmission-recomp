#include "textures/image.hpp"

#include "textures/recipe.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace sarusaru::textures {
namespace {

std::uint32_t channel(std::uint32_t texel, unsigned index) { return (texel >> (index * 8u)) & 0xFFu; }

std::uint32_t source_index(std::int64_t at, std::uint32_t size, Edge edge) {
    const auto n = static_cast<std::int64_t>(size);
    if (edge == Edge::Wrap) return static_cast<std::uint32_t>(((at % n) + n) % n);
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(at, 0, n - 1));
}

} // namespace

Image pad(const Image &image, std::uint32_t border, Edge edge) {
    Image result{image.width + 2u * border, image.height + 2u * border, {}};
    result.texels.resize(std::size_t{result.width} * result.height);
    for (std::uint32_t y = 0; y < result.height; ++y)
        for (std::uint32_t x = 0; x < result.width; ++x)
            result.texels[std::size_t{y} * result.width + x] =
                image.at(source_index(std::int64_t{x} - border, image.width, edge),
                         source_index(std::int64_t{y} - border, image.height, edge));
    return result;
}

Image crop(const Image &image, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
    Image result{width, height, {}};
    result.texels.resize(std::size_t{width} * height);
    for (std::uint32_t row = 0; row < height; ++row)
        std::copy_n(image.texels.begin() + static_cast<std::ptrdiff_t>(std::size_t{y + row} * image.width + x), width,
                    result.texels.begin() + static_cast<std::ptrdiff_t>(std::size_t{row} * width));
    return result;
}

Image colour_plane(const Image &image) {
    Image result = image;
    for (auto &texel : result.texels) texel |= 0xFF000000u;
    return result;
}

Image alpha_plane(const Image &image) {
    Image result = image;
    for (auto &texel : result.texels) {
        const std::uint32_t a = texel >> 24u;
        texel = 0xFF000000u | a << 16u | a << 8u | a;
    }
    return result;
}

Image combine(const Image &colour, const Image &alpha) {
    Image result = colour;
    for (std::size_t i = 0; i < result.texels.size(); ++i) {
        const std::uint32_t grey = alpha.texels[i];
        const std::uint32_t a = (channel(grey, 0) + channel(grey, 1) + channel(grey, 2) + 1u) / 3u;
        result.texels[i] = (result.texels[i] & 0x00FFFFFFu) | a << 24u;
    }
    return result;
}

bool opaque(const Image &image) {
    return std::ranges::all_of(image.texels, [](std::uint32_t texel) { return (texel >> 24u) == 0xFFu; });
}

Image resize_nearest(const Image &image, std::uint32_t factor) {
    Image result{image.width * factor, image.height * factor, {}};
    result.texels.resize(std::size_t{result.width} * result.height);
    for (std::uint32_t y = 0; y < result.height; ++y)
        for (std::uint32_t x = 0; x < result.width; ++x)
            result.texels[std::size_t{y} * result.width + x] = image.at(x / factor, y / factor);
    return result;
}

Image resize_linear(const Image &image, std::uint32_t factor, Edge edge) {
    Image result{image.width * factor, image.height * factor, {}};
    result.texels.resize(std::size_t{result.width} * result.height);
    for (std::uint32_t y = 0; y < result.height; ++y) {
        const double sy = (y + 0.5) / factor - 0.5;
        const auto y0 = static_cast<std::int64_t>(std::floor(sy));
        const double fy = sy - static_cast<double>(y0);
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const double sx = (x + 0.5) / factor - 0.5;
            const auto x0 = static_cast<std::int64_t>(std::floor(sx));
            const double fx = sx - static_cast<double>(x0);
            const std::uint32_t a = image.at(source_index(x0, image.width, edge), source_index(y0, image.height, edge));
            const std::uint32_t b = image.at(source_index(x0 + 1, image.width, edge), source_index(y0, image.height, edge));
            const std::uint32_t c = image.at(source_index(x0, image.width, edge), source_index(y0 + 1, image.height, edge));
            const std::uint32_t d = image.at(source_index(x0 + 1, image.width, edge), source_index(y0 + 1, image.height, edge));
            std::uint32_t texel = 0;
            for (unsigned i = 0; i < 4u; ++i) {
                const double top = channel(a, i) * (1.0 - fx) + channel(b, i) * fx;
                const double bottom = channel(c, i) * (1.0 - fx) + channel(d, i) * fx;
                texel |= static_cast<std::uint32_t>(std::lround(top * (1.0 - fy) + bottom * fy)) << (i * 8u);
            }
            result.texels[std::size_t{y} * result.width + x] = texel;
        }
    }
    return result;
}

std::optional<Image> load_png(const std::filesystem::path &path) {
    SDL_Surface *loaded = SDL_LoadPNG(path.string().c_str());
    if (loaded == nullptr) return std::nullopt;
    SDL_Surface *surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (surface == nullptr) return std::nullopt;
    Image image{static_cast<std::uint32_t>(surface->w), static_cast<std::uint32_t>(surface->h), {}};
    image.texels.resize(std::size_t{image.width} * image.height);
    const auto *rows = static_cast<const std::uint8_t *>(surface->pixels);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const std::uint8_t *row = rows + std::size_t{y} * static_cast<std::size_t>(surface->pitch);
        for (std::uint32_t x = 0; x < image.width; ++x) {
            std::uint32_t value = 0;
            for (unsigned byte = 0; byte < 4u; ++byte) value |= static_cast<std::uint32_t>(row[x * 4u + byte]) << (byte * 8u);
            image.texels[std::size_t{y} * image.width + x] = value;
        }
    }
    SDL_DestroySurface(surface);
    return image;
}

bool save_png(const std::filesystem::path &path, const Image &image, bool opaque_rgb) {
    if (image.width == 0u || image.height == 0u) return false;
    const unsigned channels = opaque_rgb ? 3u : 4u;
    std::vector<std::uint8_t> bytes(std::size_t{image.width} * image.height * channels);
    for (std::size_t i = 0; i < image.texels.size(); ++i)
        for (unsigned c = 0; c < channels; ++c) bytes[i * channels + c] = static_cast<std::uint8_t>(channel(image.texels[i], c));
    SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(image.width), static_cast<int>(image.height),
                                                 opaque_rgb ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_RGBA32, bytes.data(),
                                                 static_cast<int>(image.width * channels));
    if (surface == nullptr) return false;
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

} // namespace sarusaru::textures
