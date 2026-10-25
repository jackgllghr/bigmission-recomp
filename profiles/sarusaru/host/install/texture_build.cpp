#include "install/texture_build.hpp"

#include "install/download.hpp"
#include "install/texture_config.hpp"
#include "platform/paths.hpp"
#include "psprecomp/sha256.hpp"
#include "textures/disc_textures.hpp"
#include "textures/hd_pack.hpp"
#include "textures/image.hpp"
#include "textures/recipe.hpp"
#include "umd/disc_image.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace sarusaru::install {
namespace {

// Real-ESRGAN ncnn-vulkan 20220424 (Real-ESRGAN v0.2.5.0, BSD-3-Clause),
// https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0
constexpr std::string_view kUpscalerBase = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/";
constexpr std::string_view kUpscalerName = "realesrgan-ncnn-vulkan-20220424";

// Texels of border the model sees around each texture, then cropped away.
constexpr std::uint32_t kBorder = 8u;

[[noreturn]] void fail(const std::string &reason) { throw std::runtime_error(reason); }

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("cannot read " + path_argument(path));
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void write_text(const std::filesystem::path &path, const std::string &text) {
    auto partial = path;
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) fail("cannot write " + path_argument(path));
    }
    std::error_code error;
    std::filesystem::rename(partial, path, error);
    if (error) fail("cannot write " + path_argument(path));
}

// The hashes of the images already in a pack directory.
std::set<std::string> images_in(const std::filesystem::path &directory) {
    std::set<std::string> hashes;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error))
        if (entry.path().extension() == ".png" && textures::is_texture_hash(entry.path().stem().string()))
            hashes.insert(entry.path().stem().string());
    return hashes;
}

struct Work {
    textures::Image image;
    textures::UpscaleOptions options;
    textures::UpscalePlan plan;
    bool model_alpha{};
};

std::string plan_key(const textures::UpscalePlan &plan) {
    std::string key;
    for (const auto &pass : plan.passes) key += pass.model + "x" + std::to_string(pass.factor) + ";";
    return key;
}

std::uint64_t count_files(const std::filesystem::path &directory) {
    std::uint64_t count = 0;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error))
        if (entry.path().extension() == ".png") ++count;
    return count;
}

// Where the recipe is kept (by its hash) and the upscaler unpacked.
std::filesystem::path recipe_file(const std::filesystem::path &data, const PackSource &recipe) {
    return data / "textures" / ("recipe-" + recipe.sha256.substr(0u, 16u) + ".txt");
}
std::filesystem::path upscaler_directory(const std::filesystem::path &data, const UpscalerSource &upscaler) {
    return data / "tools" / upscaler.name;
}
bool have_recipe(const std::filesystem::path &path, const PackSource &recipe) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && psprecomp::sha256_file(path) == recipe.sha256;
}

} // namespace

std::optional<PackSource> texture_recipe_source() {
    if (kTextureRecipeUrl.empty() || kTextureRecipeSha256.size() != 64u) return std::nullopt;
    return PackSource{std::string(kTextureRecipeUrl), std::string(kTextureRecipeSha256),
                      std::strtoull(std::string(kTextureRecipeSize).c_str(), nullptr, 10)};
}

std::optional<UpscalerSource> upscaler_source() {
    UpscalerSource source;
    source.name = std::string(kUpscalerName);
#if defined(__APPLE__) && !defined(__ANDROID__)
    source.archive = {std::string(kUpscalerBase) + std::string(kUpscalerName) + "-macos.zip",
                      "e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a", 51817124u};
    source.executable = "realesrgan-ncnn-vulkan";
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    source.archive = {std::string(kUpscalerBase) + std::string(kUpscalerName) + "-windows.zip",
                      "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d", 45474481u};
    source.executable = "realesrgan-ncnn-vulkan.exe";
#elif defined(__linux__) && defined(__x86_64__) && !defined(__ANDROID__)
    source.archive = {std::string(kUpscalerBase) + std::string(kUpscalerName) + "-ubuntu.zip",
                      "e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96", 46931474u};
    source.executable = "realesrgan-ncnn-vulkan";
#else
    return std::nullopt;
#endif
    return source;
}

std::uint64_t texture_download_bytes(const PackSource &recipe, const UpscalerSource &upscaler) {
    const auto data = platform::user_data_directory();
    if (!data) return 0u;
    std::error_code error;
    const bool upscaler_here =
        std::filesystem::is_regular_file(upscaler_directory(*data, upscaler) / upscaler.executable, error);
    return (have_recipe(recipe_file(*data, recipe), recipe) ? 0u : recipe.bytes) + (upscaler_here ? 0u : upscaler.archive.bytes);
}

std::optional<std::filesystem::path> texture_pack_directory(const PackSource &recipe, std::uint32_t scale) {
    const auto data = platform::user_data_directory();
    if (!data || recipe.sha256.size() < 16u) return std::nullopt;
    return *data / "textures" / (std::to_string(scale) + "x-" + recipe.sha256.substr(0u, 16u));
}

std::optional<PackSource> prebuilt_pack_source(std::uint32_t scale) {
    const auto [url, sha256, size] = scale == 2u ? std::array{kPrebuiltPack2Url, kPrebuiltPack2Sha256, kPrebuiltPack2Size}
                                   : scale == 3u ? std::array{kPrebuiltPack3Url, kPrebuiltPack3Sha256, kPrebuiltPack3Size}
                                   : scale == 4u ? std::array{kPrebuiltPackUrl, kPrebuiltPackSha256, kPrebuiltPackSize}
                                                 : std::array<std::string_view, 3>{};
    if (url.empty() || sha256.size() != 64u) return std::nullopt;
    return PackSource{std::string(url), std::string(sha256), std::strtoull(std::string(size).c_str(), nullptr, 10)};
}

std::optional<std::filesystem::path> prebuilt_pack_directory(const PackSource &pack, std::uint32_t scale) {
    const auto data = platform::user_data_directory();
    if (!data || pack.sha256.size() < 16u) return std::nullopt;
    return *data / "textures" / (std::to_string(scale) + "x-prebuilt-" + pack.sha256.substr(0u, 16u));
}

std::uint32_t prebuilt_pack_scale(const std::filesystem::path &directory) {
    auto path = directory.lexically_normal();
    if (path.filename().empty()) path = path.parent_path();
    const auto name = path.filename().string();
    for (const std::uint32_t scale : {2u, 3u, 4u})
        if (name.starts_with(std::to_string(scale) + "x-prebuilt-")) return scale;
    return 0u;
}

TexturePackState texture_pack_state(const std::filesystem::path &directory) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(directory / textures::HdTexturePack::kManifestName, error))
        return TexturePackState::Missing;
    return count_files(directory / textures::HdTexturePack::kCollectedName) != 0u ? TexturePackState::Collected
                                                                                   : TexturePackState::Ready;
}

void build_texture_pack(const PackInputs &inputs, ProcessGroup &processes,
                        const std::function<void(const TextureProgress &)> &report) {
    using Step = TextureProgress::Step;
    const auto cancelled = [&] {
        if (processes.cancelled()) throw std::runtime_error("cancelled");
    };
    TextureProgress progress;
    progress.step = Step::Reading;
    report(progress);

    const auto recipe = textures::parse_recipe(inputs.recipe_text);
    if (recipe.disc_id != inputs.disc_id) fail("the texture recipe is for " + recipe.disc_id + ", not " + inputs.disc_id);
    std::filesystem::create_directories(inputs.pack);
    const auto made = images_in(inputs.pack);
    const auto skipped = [&](const std::string &hash) { return recipe.skip.contains(hash) || made.contains(hash); };

    // What to upscale: the recipe's textures found on the disc, every picture
    // in its scanned directories, and what the game collected while playing.
    std::map<std::string, Work> work;
    const auto add = [&](const std::string &hash, textures::Image image) {
        Work item;
        item.options = recipe.options(hash);
        if (item.options.alpha == textures::AlphaMode::Opaque && !textures::opaque(image))
            item.options.alpha = textures::AlphaMode::Linear;
        item.plan = textures::plan_upscale(item.options, inputs.scale);
        item.model_alpha = item.options.alpha == textures::AlphaMode::Model && !item.plan.passes.empty() &&
                           !textures::opaque(image);
        item.image = std::move(image);
        work.emplace(hash, std::move(item));
    };
    {
        umd::DiscImage disc(inputs.disc);
        textures::DiscTextureReader reader(disc);
        for (const auto &[hash, texture] : recipe.textures) {
            if (!texture.source || skipped(hash) || work.contains(hash)) continue;
            cancelled();
            try {
                auto picture = reader.read(*texture.source);
                // A locator that names anything else is never upscaled.
                if (picture.identity().hash == hash) add(hash, {picture.width, picture.height, std::move(picture.texels)});
            } catch (const std::runtime_error &) {
            }
        }
        for (const auto &directory : recipe.scans) {
            textures::scan_disc(disc, directory, [&](const textures::SourceLocator &, textures::Picture &&picture) {
                if (processes.cancelled()) return false;
                const auto hash = picture.identity().hash;
                const auto *listed = recipe.find(hash);
                if (!skipped(hash) && !work.contains(hash) && (listed == nullptr || !listed->source))
                    add(hash, {picture.width, picture.height, std::move(picture.texels)});
                return true;
            });
        }
    }
    cancelled();
    const auto collected = inputs.pack / textures::HdTexturePack::kCollectedName;
    std::vector<std::filesystem::path> consumed;
    std::error_code ignored;
    for (const auto &entry : std::filesystem::directory_iterator(collected, ignored)) {
        const auto hash = entry.path().stem().string();
        if (entry.path().extension() != ".png" || !textures::is_texture_hash(hash)) continue;
        consumed.push_back(entry.path());
        if (skipped(hash) || work.contains(hash) || recipe.find(hash) == nullptr) continue;
        auto image = textures::load_png(entry.path());
        if (!image) continue;
        gpu::TextureState state;
        state.width = static_cast<std::uint16_t>(image->width);
        state.height = static_cast<std::uint16_t>(image->height);
        state.buffer_width = image->width;
        if (image->width <= 512u && image->height <= 512u && gpu::identify_texture(state, image->texels).hash == hash)
            add(hash, std::move(*image));
    }

    // The upscaler runs once per pass over a directory of every texture that
    // shares its passes; colour and alpha planes go through as separate images.
    const auto staging = std::filesystem::path(inputs.pack.string() + ".work");
    std::filesystem::remove_all(staging, ignored);
    std::map<std::string, std::vector<std::string>> groups;
    std::uint64_t images = 0;
    for (const auto &[hash, item] : work) {
        groups[plan_key(item.plan)].push_back(hash);
        images += item.plan.passes.size() * (item.model_alpha ? 2u : 1u);
    }
    progress.step = Step::Upscaling;
    progress.total = images;
    report(progress);
    std::uint64_t upscaled = 0;
    std::map<std::string, std::filesystem::path> results;
    std::size_t group_index = 0;
    for (const auto &[key, hashes] : groups) {
        const auto &plan = work.at(hashes.front()).plan;
        auto input = staging / ("group" + std::to_string(group_index++)) / "in";
        std::filesystem::create_directories(input);
        for (const auto &hash : hashes) {
            cancelled();
            const auto &item = work.at(hash);
            const auto padded = textures::pad(item.image, kBorder, item.options.edge);
            if (!textures::save_png(input / (hash + ".png"), textures::colour_plane(padded), true))
                fail("cannot write " + path_argument(input));
            if (item.model_alpha && !textures::save_png(input / (hash + ".alpha.png"), textures::alpha_plane(padded), true))
                fail("cannot write " + path_argument(input));
        }
        for (std::size_t pass = 0; pass < plan.passes.size(); ++pass) {
            cancelled();
            const auto output = input.parent_path() / ("pass" + std::to_string(pass));
            std::filesystem::create_directories(output);
            const std::uint64_t before = upscaled, expected = count_files(input);
            std::atomic<bool> finished{false};
            std::thread watcher([&] {
                while (!finished) {
                    TextureProgress now = progress;
                    now.done = before + std::min(count_files(output), expected);
                    report(now);
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
            });
            const auto result = processes.run({path_argument(inputs.upscaler), "-i", path_argument(input), "-o",
                                               path_argument(output), "-n", plan.passes[pass].model, "-s",
                                               std::to_string(plan.passes[pass].factor), "-m", path_argument(inputs.models),
                                               "-f", "png"});
            finished = true;
            watcher.join();
            cancelled();
            if (!result.ok() || count_files(output) < expected) {
                auto output_text = result.output;
                if (output_text.size() > 600u) output_text = "..." + output_text.substr(output_text.size() - 600u);
                fail("the upscaler stopped: " + output_text);
            }
            upscaled = before + expected;
            progress.done = upscaled;
            report(progress);
            input = output;
        }
        for (const auto &hash : hashes) results[hash] = input;
    }

    // Crop the border, finish with nearest-neighbour, and join the planes.
    progress.step = Step::Writing;
    progress.done = 0;
    progress.total = work.size();
    report(progress);
    for (auto &[hash, item] : work) {
        cancelled();
        const auto &directory = results.at(hash);
        const std::uint32_t model = item.plan.model_factor();
        const auto load = [&](const std::string &name) {
            auto image = textures::load_png(directory / name);
            if (!image || image->width != (item.image.width + 2u * kBorder) * model ||
                image->height != (item.image.height + 2u * kBorder) * model)
                fail("the upscaler wrote an unexpected " + name);
            return textures::crop(*image, kBorder * model, kBorder * model, item.image.width * model, item.image.height * model);
        };
        auto colour = load(hash + ".png");
        if (item.plan.nearest > 1u) colour = textures::resize_nearest(colour, item.plan.nearest);
        textures::Image alpha;
        if (item.model_alpha) {
            alpha = load(hash + ".alpha.png");
            if (item.plan.nearest > 1u) alpha = textures::resize_nearest(alpha, item.plan.nearest);
        } else if (item.options.alpha == textures::AlphaMode::Linear || item.options.alpha == textures::AlphaMode::Model) {
            alpha = textures::resize_linear(textures::alpha_plane(item.image), inputs.scale, item.options.edge);
        } else {
            alpha = textures::resize_nearest(textures::alpha_plane(item.image), inputs.scale);
        }
        if (!textures::save_png(inputs.pack / (hash + ".png"), textures::combine(colour, alpha)))
            fail("cannot write " + path_argument(inputs.pack));
        ++progress.done;
        report(progress);
    }
    std::filesystem::remove_all(staging, ignored);
    for (const auto &path : consumed) std::filesystem::remove(path, ignored);

    // What the game should collect while playing: listed, not on the disc, not made.
    const auto now_made = images_in(inputs.pack);
    std::string wanted;
    progress.waiting = 0;
    for (const auto &[hash, texture] : recipe.textures)
        if (!now_made.contains(hash) && !recipe.skip.contains(hash)) {
            wanted += hash + "\n";
            ++progress.waiting;
        }
    write_text(inputs.pack / textures::HdTexturePack::kWantedName, wanted);
    write_text(inputs.pack / textures::HdTexturePack::kManifestName,
               textures::serialize_hd_manifest({inputs.disc_id, inputs.scale}));
    progress.textures = now_made.size();
    progress.step = Step::Done;
    report(progress);
}

void install_prebuilt_pack(const std::filesystem::path &archive, std::string_view disc_id,
                           const std::filesystem::path &destination, ProcessGroup &processes) {
    std::error_code ignored;
    auto partial = destination;
    partial += ".partial";
    std::filesystem::remove_all(partial, ignored);
    std::filesystem::create_directories(partial);
    std::string error;
    if (!unpack_zip(processes, archive, partial, error)) {
        std::filesystem::remove_all(partial, ignored);
        fail(processes.cancelled() ? "cancelled" : "cannot unpack the texture pack: " + error);
    }
    // pack.ini at the top, or in the zip's only folder.
    auto root = partial;
    if (!std::filesystem::is_regular_file(root / textures::HdTexturePack::kManifestName, ignored)) {
        std::vector<std::filesystem::path> folders;
        for (const auto &entry : std::filesystem::directory_iterator(partial, ignored))
            if (entry.is_directory()) folders.push_back(entry.path());
        if (folders.size() == 1u) root = folders.front();
    }
    try {
        (void)textures::HdTexturePack::load(root, disc_id);
    } catch (const std::exception &bad) {
        std::filesystem::remove_all(partial, ignored);
        fail(std::string("the downloaded texture pack is not usable: ") + bad.what());
    }
    std::filesystem::remove_all(destination, ignored);
    std::filesystem::create_directories(destination.parent_path(), ignored);
    std::filesystem::rename(root, destination, ignored);
    std::filesystem::remove_all(partial, ignored);
    if (ignored) fail("cannot install the texture pack in " + path_argument(destination));
}

PrebuiltPackDownload::PrebuiltPackDownload(PackSource pack, std::string disc_id, std::filesystem::path destination)
    : pack_(std::move(pack)), disc_id_(std::move(disc_id)), destination_(std::move(destination)) {
    progress_.total = pack_.bytes;
    thread_ = std::thread([this] { run(); });
}

PrebuiltPackDownload::~PrebuiltPackDownload() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void PrebuiltPackDownload::cancel() {
    cancelled_ = true;
    processes_.cancel();
}

TextureProgress PrebuiltPackDownload::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

void PrebuiltPackDownload::run() {
    using Step = TextureProgress::Step;
    std::error_code ignored;
    auto archive = destination_;
    archive += ".zip";
    try {
        std::string error;
        if (!download_verified(processes_, pack_.url, pack_.sha256, archive,
                               [this](std::uint64_t bytes) {
                                   std::lock_guard lock(mutex_);
                                   progress_.done = bytes;
                               },
                               error))
            fail(error);
        {
            std::lock_guard lock(mutex_);
            progress_.step = Step::Unpacking;
        }
        install_prebuilt_pack(archive, disc_id_, destination_, processes_);
        std::filesystem::remove(archive, ignored);
        std::lock_guard lock(mutex_);
        progress_.step = Step::Done;
        progress_.done = progress_.total;
    } catch (const std::exception &failure) {
        std::filesystem::remove(archive, ignored);
        std::lock_guard lock(mutex_);
        progress_.step = cancelled_ ? Step::Cancelled : Step::Failed;
        progress_.error = failure.what();
    }
}

TextureBuild::TextureBuild(PackSource recipe, UpscalerSource upscaler, std::filesystem::path disc, std::string disc_id,
                           std::uint32_t scale, std::filesystem::path destination)
    : recipe_(std::move(recipe)), upscaler_(std::move(upscaler)), disc_(std::move(disc)), disc_id_(std::move(disc_id)),
      scale_(scale), destination_(std::move(destination)) {
    thread_ = std::thread([this] { run(); });
}

TextureBuild::~TextureBuild() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void TextureBuild::cancel() {
    cancelled_ = true;
    processes_.cancel();
}

TextureProgress TextureBuild::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

void TextureBuild::report(const TextureProgress &progress) {
    std::lock_guard lock(mutex_);
    progress_ = progress;
}

void TextureBuild::run() {
    using Step = TextureProgress::Step;
    const auto failed = [this](const std::string &error) {
        std::lock_guard lock(mutex_);
        progress_.step = cancelled_ ? Step::Cancelled : Step::Failed;
        progress_.error = error;
    };
    try {
        const auto data = platform::user_data_directory();
        if (!data) fail("there is no per-user data directory");
        std::error_code ignored;

        // The recipe, kept by its hash, and the upscaler, unpacked once.
        const auto recipe_path = recipe_file(*data, recipe_);
        const auto tools = upscaler_directory(*data, upscaler_);
        const auto program = tools / upscaler_.executable;
        const bool need_recipe = !have_recipe(recipe_path, recipe_);
        const bool need_upscaler = !std::filesystem::is_regular_file(program, ignored);
        const std::uint64_t download = (need_recipe ? recipe_.bytes : 0u) + (need_upscaler ? upscaler_.archive.bytes : 0u);
        std::uint64_t received = 0;
        const auto downloading = [&](std::uint64_t bytes) {
            std::lock_guard lock(mutex_);
            progress_.step = Step::Downloading;
            progress_.done = received + bytes;
            progress_.total = download;
        };
        std::string error;
        if (need_recipe) {
            if (!download_verified(processes_, recipe_.url, recipe_.sha256, recipe_path, downloading, error)) fail(error);
            received += recipe_.bytes;
        }
        if (need_upscaler) {
            const auto archive = tools.parent_path() / (upscaler_.name + ".zip");
            if (!download_verified(processes_, upscaler_.archive.url, upscaler_.archive.sha256, archive, downloading, error))
                fail(error);
            {
                std::lock_guard lock(mutex_);
                progress_.step = Step::Unpacking;
            }
            auto partial = tools;
            partial += ".partial";
            std::filesystem::remove_all(partial, ignored);
            std::filesystem::create_directories(partial);
            if (!unpack_zip(processes_, archive, partial, error) ||
                !std::filesystem::is_regular_file(partial / upscaler_.executable, ignored))
                fail("cannot unpack the upscaler: " + error);
            std::filesystem::permissions(partial / upscaler_.executable,
                                         std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add, ignored);
            std::filesystem::remove_all(tools, ignored);
            std::filesystem::rename(partial, tools, ignored);
            if (ignored) fail("cannot install the upscaler in " + path_argument(tools));
            std::filesystem::remove(archive, ignored);
        }

        PackInputs inputs;
        inputs.recipe_text = read_text(recipe_path);
        inputs.disc_id = disc_id_;
        inputs.disc = disc_;
        inputs.upscaler = program;
        inputs.models = tools / "models";
        inputs.scale = scale_;
        inputs.pack = destination_;
        build_texture_pack(inputs, processes_, [this](const TextureProgress &progress) { report(progress); });
    } catch (const std::exception &failure) {
        failed(failure.what());
    }
}

} // namespace sarusaru::install
