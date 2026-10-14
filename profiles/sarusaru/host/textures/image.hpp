#pragma once

// RGBA images for upscaling textures on the player's device: the colour and
// alpha planes are split, upscaled apart as straight (unpremultiplied) values
// and combined again, and borders are padded so the model sees past the edge
// the way the texture repeats or clamps.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace sarusaru::textures {

enum class Edge;

struct Image {
    std::uint32_t width{}, height{};
    std::vector<std::uint32_t> texels;  // RGBA, red in the low byte, `width` to a row
    [[nodiscard]] std::uint32_t at(std::uint32_t x, std::uint32_t y) const { return texels[std::size_t{y} * width + x]; }
    bool operator==(const Image &) const = default;
};

// `border` texels added on every side: the far side's (wrap) or the edge's (clamp).
[[nodiscard]] Image pad(const Image &image, std::uint32_t border, Edge edge);
[[nodiscard]] Image crop(const Image &image, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);
// The colour with alpha 255, and the alpha as grey (alpha 255).
[[nodiscard]] Image colour_plane(const Image &image);
[[nodiscard]] Image alpha_plane(const Image &image);
// `colour`'s RGB with the grey level of `alpha` (the mean of its RGB) as alpha.
[[nodiscard]] Image combine(const Image &colour, const Image &alpha);
[[nodiscard]] bool opaque(const Image &image);
[[nodiscard]] Image resize_nearest(const Image &image, std::uint32_t factor);
// Bilinear, sampling past the edge as `edge` says.
[[nodiscard]] Image resize_linear(const Image &image, std::uint32_t factor, Edge edge);

// PNG through SDL. `opaque_rgb` writes RGB without alpha (for the upscaler).
[[nodiscard]] std::optional<Image> load_png(const std::filesystem::path &path);
bool save_png(const std::filesystem::path &path, const Image &image, bool opaque_rgb = false);

} // namespace sarusaru::textures
