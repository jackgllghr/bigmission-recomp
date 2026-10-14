#include "textures/recipe.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace sarusaru::textures {
namespace {

constexpr std::string_view kMagic = "sarusaru-texture-recipe";
constexpr std::size_t kMaxTextures = 65536u;

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
        const auto start = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') ++at;
        if (at > start) result.push_back(line.substr(start, at - start));
    }
    return result;
}

void check_option(std::string_view key, std::string_view value) {
    if (key == "model") {
        if (value != "realesrgan-x4plus" && value != "realesrgan-x4plus-anime" && value != "realesr-animevideov3" &&
            value != "nearest")
            throw std::runtime_error("unknown model " + std::string(value));
    } else if (key == "scale") {
        if (value != "1" && value != "2" && value != "4" && value != "8") throw std::runtime_error("scale must be 1, 2, 4 or 8");
    } else if (key == "alpha") {
        if (value != "model" && value != "linear" && value != "nearest" && value != "opaque")
            throw std::runtime_error("alpha must be model, linear, nearest or opaque");
    } else if (key == "edge") {
        if (value != "clamp" && value != "wrap") throw std::runtime_error("edge must be clamp or wrap");
    } else {
        throw std::runtime_error("unknown option " + std::string(key));
    }
}

std::unordered_map<std::string, std::string> options_from(std::span<const std::string_view> parts) {
    std::unordered_map<std::string, std::string> result;
    for (const auto part : parts) {
        const auto equals = part.find('=');
        if (equals == std::string_view::npos) throw std::runtime_error("expected key=value, got " + std::string(part));
        const std::string key(part.substr(0, equals));
        const auto value = part.substr(equals + 1u);
        check_option(key, value);
        if (!result.emplace(key, value).second) throw std::runtime_error("option " + key + " given twice");
    }
    return result;
}

std::string options_text(const std::unordered_map<std::string, std::string> &options) {
    const std::map<std::string, std::string> sorted(options.begin(), options.end());
    std::string text;
    for (const auto &[key, value] : sorted) text += "\t" + key + "=" + value;
    return text;
}

void apply(UpscaleOptions &options, const std::unordered_map<std::string, std::string> &values) {
    for (const auto &[key, value] : values) {
        if (key == "model")
            options.model = value == "realesrgan-x4plus" ? Model::X4Plus
                          : value == "realesr-animevideov3" ? Model::AnimeVideoV3
                          : value == "nearest" ? Model::Nearest : Model::X4PlusAnime;
        else if (key == "scale")
            options.max_scale = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "alpha")
            options.alpha = value == "linear" ? AlphaMode::Linear : value == "nearest" ? AlphaMode::Nearest
                          : value == "opaque" ? AlphaMode::Opaque : AlphaMode::Model;
        else if (key == "edge")
            options.edge = value == "wrap" ? Edge::Wrap : Edge::Clamp;
    }
}

} // namespace

bool is_texture_hash(std::string_view text) {
    return text.size() == 32u && std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string_view model_name(Model model) {
    switch (model) {
    case Model::X4Plus: return "realesrgan-x4plus";
    case Model::X4PlusAnime: return "realesrgan-x4plus-anime";
    case Model::AnimeVideoV3: return "realesr-animevideov3";
    case Model::Nearest: return "nearest";
    }
    return "nearest";
}

const RecipeTexture *Recipe::find(std::string_view hash) const {
    const auto found = textures.find(std::string(hash));
    return found == textures.end() ? nullptr : &found->second;
}

UpscaleOptions Recipe::options(std::string_view hash) const {
    UpscaleOptions result;
    apply(result, defaults);
    if (const auto *texture = find(hash)) apply(result, texture->overrides);
    return result;
}

Recipe parse_recipe(std::string_view text) {
    Recipe recipe;
    bool header = false;
    std::unordered_set<std::string> listed;
    std::size_t number = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1u);
        ++number;
        const auto parts = fields(line);
        if (parts.empty() || parts[0].front() == '#') continue;
        try {
            const auto key = parts[0];
            if (!header) {
                if (key != kMagic || parts.size() != 2u || parts[1] != "1")
                    throw std::runtime_error("expected \"sarusaru-texture-recipe 1\"");
                header = true;
            } else if (key == "disc") {
                if (parts.size() != 2u || parts[1].size() != 9u || !recipe.disc_id.empty())
                    throw std::runtime_error("disc takes one 9-character disc ID, once");
                recipe.disc_id = parts[1];
            } else if (key == "defaults") {
                if (!recipe.defaults.empty()) throw std::runtime_error("defaults given twice");
                recipe.defaults = options_from(std::span(parts).subspan(1));
            } else if (key == "scan") {
                if (parts.size() != 2u || parts[1].find("..") != std::string_view::npos || parts[1].front() == '/')
                    throw std::runtime_error("scan takes one disc directory");
                recipe.scans.emplace_back(parts[1]);
            } else if (key == "texture") {
                if (parts.size() < 3u || !is_texture_hash(parts[1])) throw std::runtime_error("texture takes a hash and a source");
                RecipeTexture texture;
                texture.hash = parts[1];
                if (parts[2] != "-") texture.source = parse_locator(parts[2]);
                texture.overrides = options_from(std::span(parts).subspan(3));
                if (!listed.insert(texture.hash).second) throw std::runtime_error("texture listed twice");
                const auto hash = texture.hash;
                recipe.textures.emplace(hash, std::move(texture));
            } else if (key == "skip") {
                if (parts.size() != 2u || !is_texture_hash(parts[1])) throw std::runtime_error("skip takes one hash");
                recipe.skip.emplace(parts[1]);
            } else {
                throw std::runtime_error("unknown line " + std::string(key));
            }
            if (recipe.textures.size() + recipe.skip.size() > kMaxTextures) throw std::runtime_error("too many textures");
        } catch (const std::runtime_error &error) {
            throw std::runtime_error("texture recipe line " + std::to_string(number) + ": " + error.what());
        }
    }
    if (!header || recipe.disc_id.empty()) throw std::runtime_error("texture recipe: needs its header and disc");
    for (const auto &hash : recipe.skip)
        if (listed.contains(hash)) throw std::runtime_error("texture recipe: " + hash + " is both listed and skipped");
    return recipe;
}

std::string serialize_recipe(const Recipe &recipe) {
    std::ostringstream out;
    out << "# SaruSaru texture recipe: what to upscale and how. No pixels.\n"
        << kMagic << "\t1\ndisc\t" << recipe.disc_id << '\n';
    if (!recipe.defaults.empty()) out << "defaults" << options_text(recipe.defaults) << '\n';
    for (const auto &scan : recipe.scans) out << "scan\t" << scan << '\n';
    for (const auto &[hash, texture] : recipe.textures)
        out << "texture\t" << texture.hash << '\t' << (texture.source ? format_locator(*texture.source) : "-")
            << options_text(texture.overrides) << '\n';
    std::vector<std::string> skipped(recipe.skip.begin(), recipe.skip.end());
    std::ranges::sort(skipped);
    for (const auto &hash : skipped) out << "skip\t" << hash << '\n';
    return out.str();
}

std::uint32_t UpscalePlan::model_factor() const {
    std::uint32_t factor = 1;
    for (const auto &pass : passes) factor *= pass.factor;
    return factor;
}

UpscalePlan plan_upscale(const UpscaleOptions &options, std::uint32_t pack_scale) {
    if (pack_scale < 2u || pack_scale > 8u || (pack_scale & (pack_scale - 1u)) != 0u)
        throw std::runtime_error("pack scale must be 2, 4 or 8");
    UpscalePlan plan;
    const std::uint32_t target = options.model == Model::Nearest ? 1u : std::min(options.max_scale, pack_scale);
    std::uint32_t reached = 1;
    // The x4plus models only enlarge 4x; animevideov3 also has 2x, which
    // finishes an 8x and does a 2x on its own.
    if (target >= 4u) {
        plan.passes.push_back({std::string(model_name(options.model)), 4u});
        reached = 4u;
    }
    while (reached < target) {
        plan.passes.push_back({"realesr-animevideov3", 2u});
        reached *= 2u;
    }
    plan.nearest = pack_scale / reached;
    return plan;
}

} // namespace sarusaru::textures
