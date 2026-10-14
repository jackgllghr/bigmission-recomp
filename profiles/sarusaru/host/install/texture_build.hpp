#pragma once

// Making an HD texture pack on the player's device (docs/hd-textures.md):
// download the recipe this build was configured with and the pinned
// Real-ESRGAN upscaler, read the textures the recipe names from the player's
// own disc (and any the game collected while they played), upscale colour and
// alpha as separate straight planes, and write the pack the renderer loads
// (textures/hd_pack.hpp). Nothing derived from the game is downloaded.
#include "install/pack_download.hpp"
#include "install/process.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sarusaru::install {

// The recipe this build offers, or nothing if it was configured without one.
[[nodiscard]] std::optional<PackSource> texture_recipe_source();

// The pinned upscaler for this platform: a zip holding the program and its
// models. Nothing where there is no build of it (Linux on Arm, Android).
struct UpscalerSource {
    PackSource archive;
    std::string name;        // the directory it is unpacked into, under <user data>/tools
    std::string executable;  // the program, inside that directory
};
[[nodiscard]] std::optional<UpscalerSource> upscaler_source();

// <user data>/textures/<scale>x-<first 16 digits of the recipe's SHA-256>.
[[nodiscard]] std::optional<std::filesystem::path> texture_pack_directory(const PackSource &recipe, std::uint32_t scale);

enum class TexturePackState {
    Missing,    // never made (or cut short)
    Ready,      // made, nothing waiting
    Collected,  // made, and the game has collected textures to upscale next
};
[[nodiscard]] TexturePackState texture_pack_state(const std::filesystem::path &directory);

struct TextureProgress {
    enum class Step { Downloading, Unpacking, Reading, Upscaling, Writing, Done, Failed, Cancelled };
    Step step{Step::Downloading};
    // Downloading: bytes of `total`; Upscaling: images of `total`; Writing: textures of `total`.
    std::uint64_t done{}, total{};
    // Done: textures in the pack, and how many the game will collect while playing.
    std::size_t textures{}, waiting{};
    std::string error;
    [[nodiscard]] bool finished() const noexcept {
        return step == Step::Done || step == Step::Failed || step == Step::Cancelled;
    }
};

// The part that needs no network: makes or adds to the pack in `pack` from
// `recipe_text`, the disc image at `disc`, and the upscaler program (with its
// models in `models`). Runs the upscaler through `processes`, so cancelling
// those stops it. Throws std::runtime_error on failure.
struct PackInputs {
    std::string recipe_text;
    std::string disc_id;  // the supported disc's, which the recipe must name
    std::filesystem::path disc;
    std::filesystem::path upscaler;
    std::filesystem::path models;
    std::uint32_t scale{4};
    std::filesystem::path pack;
};
void build_texture_pack(const PackInputs &inputs, ProcessGroup &processes,
                        const std::function<void(const TextureProgress &)> &report);

class TextureBuild {
public:
    // Starts making the pack for `scale` (4 or 8) into `destination` on its own thread.
    TextureBuild(PackSource recipe, UpscalerSource upscaler, std::filesystem::path disc, std::string disc_id,
                 std::uint32_t scale, std::filesystem::path destination);
    // Cancels a build still under way and waits for it to stop.
    ~TextureBuild();
    TextureBuild(const TextureBuild &) = delete;
    TextureBuild &operator=(const TextureBuild &) = delete;

    void cancel();
    [[nodiscard]] TextureProgress progress() const;
    [[nodiscard]] const std::filesystem::path &destination() const noexcept { return destination_; }
    [[nodiscard]] std::uint32_t scale() const noexcept { return scale_; }

private:
    PackSource recipe_;
    UpscalerSource upscaler_;
    std::filesystem::path disc_;
    std::string disc_id_;
    std::uint32_t scale_;
    std::filesystem::path destination_;
    ProcessGroup processes_;
    std::atomic<bool> cancelled_{false};
    mutable std::mutex mutex_;
    TextureProgress progress_;
    std::thread thread_;

    void run();
    void report(const TextureProgress &progress);
};

} // namespace sarusaru::install
