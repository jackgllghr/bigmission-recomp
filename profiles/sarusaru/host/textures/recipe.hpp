#pragma once

// A texture recipe: what to upscale and how, never pixels. It is published
// apart from the port and downloaded by the launcher, which reads the
// textures from the player's own disc (or collects them while they play) and
// upscales them on their device (install/texture_build.hpp).
//
//   sarusaru-texture-recipe  1
//   disc      UCJS10060
//   defaults  model=realesrgan-x4plus-anime  scale=8  alpha=model  edge=clamp
//   scan      PSP_GAME/USRDIR/data
//   texture   <hash>  <source locator or ->  [key=value...]
//   skip      <hash>
//
// Fields are separated by spaces or tabs; # starts a comment line.
//   defaults  the options for every texture that does not set its own.
//   scan      also every TIM2 picture found under this disc directory.
//   texture   one texture by gpu::TextureIdentity hash, where it is on the
//             disc (textures/disc_textures.hpp), or - when it is not stored
//             as a picture: the game then collects it while the player plays.
//   skip      never upscale this texture (text drawn at runtime, gradients...).
//
// Options:
//   model  realesrgan-x4plus | realesrgan-x4plus-anime | realesr-animevideov3
//          | nearest (no model: pixel art, fonts)
//   scale  the most the model may enlarge it: 1, 2, 4 or 8; a pack with a
//          larger scale enlarges the rest with nearest-neighbour
//   alpha  model (the alpha plane through the model too) | linear | nearest
//          | opaque (alpha is all 255; checked, else linear)
//   edge   clamp | wrap (a tiling texture: padded with its other side so the
//          model sees no seam)
// Colour and alpha are always upscaled as separate, straight planes, so the
// colour of transparent texels survives (the game draws some with alpha off).
#include "textures/disc_textures.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sarusaru::textures {

enum class Model { X4Plus, X4PlusAnime, AnimeVideoV3, Nearest };
enum class AlphaMode { Model, Linear, Nearest, Opaque };
enum class Edge { Clamp, Wrap };

struct UpscaleOptions {
    Model model{Model::X4PlusAnime};
    std::uint32_t max_scale{8};
    AlphaMode alpha{AlphaMode::Model};
    Edge edge{Edge::Clamp};
    bool operator==(const UpscaleOptions &) const = default;
};

struct RecipeTexture {
    std::string hash;
    std::optional<SourceLocator> source;
    // Options this texture sets; the rest come from the defaults.
    std::unordered_map<std::string, std::string> overrides;
    bool operator==(const RecipeTexture &) const = default;
};

struct Recipe {
    std::string disc_id;
    std::unordered_map<std::string, std::string> defaults;
    std::vector<std::string> scans;
    // By hash, so a recipe reads back in a stable order.
    std::map<std::string, RecipeTexture> textures;
    std::unordered_set<std::string> skip;

    // The options for `hash`: its own entry over the defaults.
    [[nodiscard]] UpscaleOptions options(std::string_view hash) const;
    [[nodiscard]] const RecipeTexture *find(std::string_view hash) const;
};

// Throws std::runtime_error naming the line for anything it does not know.
[[nodiscard]] Recipe parse_recipe(std::string_view text);
[[nodiscard]] std::string serialize_recipe(const Recipe &recipe);
[[nodiscard]] std::string_view model_name(Model model);
[[nodiscard]] bool is_texture_hash(std::string_view text);

// One run of the upscaler over a set of images.
struct UpscalePass {
    std::string model;  // the upscaler's -n
    std::uint32_t factor{};  // its -s
    bool operator==(const UpscalePass &) const = default;
};
// How a texture reaches the pack's scale: model passes, then nearest-neighbour.
struct UpscalePlan {
    std::vector<UpscalePass> passes;
    std::uint32_t nearest{1};
    [[nodiscard]] std::uint32_t model_factor() const;
    bool operator==(const UpscalePlan &) const = default;
};
// `pack_scale` is a power of two from 2 to 8.
[[nodiscard]] UpscalePlan plan_upscale(const UpscaleOptions &options, std::uint32_t pack_scale);

} // namespace sarusaru::textures
