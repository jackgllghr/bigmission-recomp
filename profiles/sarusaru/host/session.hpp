#pragma once

// One running game: the verified executable and disc, the runtime with the
// generated code, and every HLE module this profile provides, wired together
// and ready to start. Shared by the diagnostic bootstrap and the windowed shell.
#include "game_disc.hpp"
#include "game_module.hpp"
#include "startup_hle.hpp"
#include "audio/atrac.hpp"
#include "audio/audio.hpp"
#include "ctrl/controller.hpp"
#include "display/display.hpp"
#include "ge/ge.hpp"
#include "game/frame_rate.hpp"
#include "game/gameplay_camera.hpp"
#include "game/intro_logos.hpp"
#include "io/io_manager.hpp"
#include "kernel/early_kernel.hpp"
#include "kernel/interrupts.hpp"
#include "kernel/sync.hpp"
#include "modules/module_manager.hpp"
#include "modules/utility_modules.hpp"
#include "movie/psmf_player.hpp"
#include "rtc/rtc.hpp"
#include "sas/sas.hpp"
#include "umd/disc_image.hpp"
#include "umd/umd_drive.hpp"
#include "utility/savedata_dialog.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace sarusaru {

struct SessionOptions {
    // Disables AOT call chaining so every guest call is a counted dispatch,
    // which keeps a diagnostic dispatch budget meaningful. Slower.
    bool count_every_dispatch{false};
    std::filesystem::path memory_stick;
    // 0 = original cadence. Other supported rates opt into uncompensated game timing.
    std::uint32_t experimental_game_frame_rate{};
    // Watches for the gameplay cameras (game/gameplay_camera.hpp), for
    // overrides that leave menus and cutscenes native.
    bool track_gameplay_camera{false};
    bool skip_intro_logos{false};
    // The game module to load; empty finds it (find_game_module()).
    std::filesystem::path game_module;
};

// The disc this build plays (config/supported_game.json).
[[nodiscard]] DiscIdentity supported_disc();

class GameSession {
public:
    // Verifies the executable and the disc (throws psprecomp::Error) and builds everything.
    // Without a prepared executable, the one on the verified disc is used.
    GameSession(const std::filesystem::path &iso, const SessionOptions &options);
    GameSession(const std::filesystem::path &executable, const std::filesystem::path &iso, const SessionOptions &options);

    ~GameSession();

    // Runs the game from its entry point until it stops or `dispatch_limit` is reached.
    void run(std::uint64_t dispatch_limit);

    [[nodiscard]] const std::string &module_name() const noexcept { return module_name_; }
    [[nodiscard]] std::uint32_t entry() const noexcept { return entry_; }

    // First, so that it outlives the runtime holding its functions.
    GameModule game_module;
    psprecomp::Elf32Image elf;
    umd::DiscImage disc;
    // Applied before the runtime is built, which reads it once.
    bool chaining_configured;
    psprecomp::Runtime runtime;
    StartupHle startup_hle;
    kernel::EarlyKernel kernel;
    // Installed here, before any module that registers with the kernel:
    // install() resets the kernel's IDs and hooks.
    bool kernel_installed{install_kernel()};
    kernel::SyncObjects sync{kernel};
    kernel::InterruptManager interrupts{kernel};
    umd::UmdDrive umd{disc};
    io::IoManager io{disc, runtime};
    ctrl::Controller controller;
    display::Display display;
    game::FrameRateOverride frame_rate;
    game::GameplayCamera gameplay_camera{display};
    game::IntroLogos intro_logos;
    ge::Ge ge{kernel};
    rtc::Rtc rtc{kernel, rtc::Rtc::host_boot_local_unix_us()};
    modules::UtilityModules utility_modules;
    modules::ModuleManager module_manager{kernel};
    audio::Audio audio{kernel};
    sas::SasCore sas;
    audio::AtracLibrary atrac{audio::make_ffmpeg_decoder};
    movie::PsmfPlayer psmf_player{kernel, [this](const std::string &path) { return io.read_disc_file(path); }};
    utility::SavedataUtility savedata;

private:
    bool install_kernel();
    bool track_gameplay_camera_{};
    static GameSession *observed_session_;
    static void observe_call(psprecomp::Runtime &, psprecomp::AllegrexContext &, std::uint32_t, std::uint32_t);
    static void observe_dispatch(psprecomp::Runtime &, psprecomp::AllegrexContext &, std::uint32_t, std::int32_t);
    std::string module_name_;
    std::uint32_t entry_{};
};

} // namespace sarusaru
