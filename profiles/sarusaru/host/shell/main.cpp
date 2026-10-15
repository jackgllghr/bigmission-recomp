// The windowed game: GameSession driven at real time, the GE's draws rendered
// with Vulkan, sound through SDL, and the keyboard or a gamepad as the pad.
// Started without a disc image, it opens the launcher (ui/launcher.hpp) in
// the same window first. While playing, the menu action (Escape by default)
// pauses the game under the launcher's pause menu, whose settings apply when
// the game resumes.
//
// Everything runs on the main thread. The game runs inside GameSession::run;
// at each vblank of the guest's virtual clock the kernel calls back here, and
// the shell pumps events, presents the displayed framebuffer and waits until
// real time catches up with guest time.
#include "session.hpp"
#include "game_identity.hpp"
#include "install/game_builder.hpp"
#include "install/toolchain.hpp"
#include "gpu/fov.hpp"
#include "gpu/frame_interpolator.hpp"
#include "kernel/guest_clock.hpp"
#include "movie/subtitles.hpp"
#include "psprecomp/common.hpp"
#include "shell/audio_output.hpp"
#include "shell/ge_renderer.hpp"
#include "shell/input.hpp"
#include "release_version.hpp"
#include "shell/presentation_clock.hpp"
#include "shell/vulkan_context.hpp"
#include "ui/launcher.hpp"
#include "ui/subtitles.hpp"
#include "ui/ui_script.hpp"

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
// SDLActivity starts the port by calling SDL_main in libmain.so.
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <charconv>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr const char *kUsage =
    "Usage: sarusaru [--settings FILE] [--scale N] [[prepared BOOT.elf] <game ISO>]\n"
    "  Without a game ISO the launcher opens; with one the game starts at once. The ISO's\n"
    "  own executable is used unless a prepared BOOT.elf is given as well.\n"
    "  --settings FILE  settings and bindings (default: per-user settings.ini)\n"
    "  --scale N  override the file's internal resolution (1-8 times 480x272)\n"
    "  --translation-pack FILE  select an English menu texture pack for this run\n"
    "  --texture-pack DIR  use this HD texture pack for this run (none: the original textures)\n"
    "  --dump-textures DIR  write every texture the game draws to DIR, once each, as PNG\n"
    "  --version  print the launcher release version and exit\n"
    "  --print-default-settings  print the settings file template and exit\n"
    "  --install ISO  build the game from your disc without the window, as the launcher's Install does\n"
    "Alt+Enter or F11 toggles fullscreen.\n"
    "Default keys: arrows d-pad, Z circle, X cross, A square, S triangle, Q/W L/R, Enter start,\n"
    "      Backspace select, I/J/K/L analog stick, Tab (held) fast forward, Escape pause menu.\n"
    "Environment: SARUSARU_CAPTURE=<directory> saves the picture every SARUSARU_CAPTURE_EVERY\n"
    "  seconds of game time (default 5) as BMP files, from SARUSARU_CAPTURE_FROM seconds if set;\n"
    "  SARUSARU_INPUT scripts the pad; SARUSARU_UI_SCRIPT scripts the launcher (ui/ui_script.hpp).\n";

// Real time may trail guest time by this much before pacing gives up on
// catching up (after a stall) and starts again from now.
constexpr std::uint64_t kMaxLagNs = 100'000'000u;

#include "window_icon.inc"

#if defined(__ANDROID__)
// An app has no terminal: what the port writes to stdout and stderr goes to
// the system log instead, a line at a time (adb logcat -s sarusaru).
void redirect_output_to_log() {
    static bool redirected = false;
    int ends[2];
    if (redirected || pipe(ends) != 0) return;
    redirected = true;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(ends[1], STDOUT_FILENO);
    dup2(ends[1], STDERR_FILENO);
    close(ends[1]);
    std::thread([source = ends[0]] {
        std::string line;
        char buffer[512];
        for (ssize_t length; (length = read(source, buffer, sizeof buffer)) > 0;) {
            for (ssize_t i = 0; i < length; ++i) {
                if (buffer[i] != '\n') {
                    line += buffer[i];
                    continue;
                }
                __android_log_write(ANDROID_LOG_INFO, "sarusaru", line.c_str());
                line.clear();
            }
        }
    }).detach();
}
#endif

// Gives the window the logo, which the task bar or Dock shows. A failure only
// leaves the default icon.
void set_window_icon(SDL_Window *window) {
    SDL_IOStream *stream = SDL_IOFromConstMem(kWindowIconPng, sizeof(kWindowIconPng));
    SDL_Surface *icon = stream != nullptr ? SDL_LoadPNG_IO(stream, true) : nullptr;
    if (icon == nullptr) {
        std::cerr << "sarusaru: cannot load the window icon: " << SDL_GetError() << '\n';
        return;
    }
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
}

// The settings the game runs with: the file's, with the command line's
// overrides and paths relative to the settings file resolved.
struct GameOverrides {
    std::optional<std::uint32_t> scale;
    std::filesystem::path translation_pack;
    std::optional<std::filesystem::path> texture_pack;
};
sarusaru::shell::Settings game_settings(const sarusaru::shell::Settings &settings, const GameOverrides &overrides,
                                        const std::filesystem::path &settings_path) {
    sarusaru::shell::Settings game = settings;
    if (overrides.scale) game.internal_resolution = *overrides.scale;
    if (!overrides.translation_pack.empty()) {
        game.translation_language = "en";
        game.translation_pack = overrides.translation_pack;
    } else if (!game.translation_pack.empty() && game.translation_pack.is_relative()) {
        game.translation_pack = settings_path.parent_path() / game.translation_pack;
    }
    if (overrides.texture_pack)
        game.texture_pack = *overrides.texture_pack;
    else if (game.texture_pack.is_relative() && !game.texture_pack.empty())
        game.texture_pack = settings_path.parent_path() / game.texture_pack;
    return game;
}

// Plays the game in `window` until the player quits or it stops. Returns the
// exit status; a game that could not start sets `start_failure` and returns 1.
// `settings` is the file's, which the pause menu edits and saves.
int play(SDL_Window *window, sarusaru::shell::VulkanContext &vulkan, sarusaru::ui::Layer &layer,
         sarusaru::shell::Settings &settings, const std::filesystem::path &settings_path, const GameOverrides &overrides,
         const std::filesystem::path &executable, const std::filesystem::path &iso,
         const std::filesystem::path &texture_dump_directory, sarusaru::ui::UiScript *script, std::string &start_failure) {
    int status = 0;
    std::string error;
    // What the game runs with now; the pause menu's changes are applied to it as the game resumes.
    sarusaru::shell::Settings current = game_settings(settings, overrides, settings_path);
    std::unique_ptr<sarusaru::shell::GeRenderer> renderer;
    sarusaru::shell::AudioOutput audio;
    std::optional<sarusaru::shell::Input> input;
    input.emplace(current.controls);
    sarusaru::shell::ButtonPrompts button_prompts;
    auto prompt_device = sarusaru::ui::BindingDevice::Keyboard;
    const auto update_prompts = [&] {
        layer.set_prompt_style(current.prompt_style);
        if (layer.prompt_device() == sarusaru::ui::Device::Gamepad) prompt_device = sarusaru::ui::BindingDevice::Gamepad;
        else if (layer.prompt_device() == sarusaru::ui::Device::Keyboard) prompt_device = sarusaru::ui::BindingDevice::Keyboard;
        button_prompts.configure(current.controls, prompt_device, layer.pad_type());
    };
    update_prompts();
    // 60 FPS motion shows a picture at every vblank: the game's frames and those in between.
    bool interpolating = sarusaru::shell::interpolates(current);
    sarusaru::shell::PresentationClock presentation(interpolating ? 0u : current.frame_rate);
    sarusaru::shell::Buffer capture;
    try {
        if (!audio.open(error)) std::cerr << "sarusaru: no sound: " << error << '\n';

        sarusaru::SessionOptions options;
        options.memory_stick = sarusaru::utility::default_memory_stick();
        options.skip_intro_logos = current.skip_intro_logos;
        options.camera_control = true;
        // Always watched, so the pause menu can turn the FOV override on.
        sarusaru::gpu::FovOverride fov(current.fov_degrees);
        options.track_gameplay_camera = true;
        // A game that cannot start (a moved or changed disc image) goes back
        // to the launcher with the reason.
        std::unique_ptr<sarusaru::GameSession> session;
        try {
            session = executable.empty() ? std::make_unique<sarusaru::GameSession>(iso, options)
                                         : std::make_unique<sarusaru::GameSession>(executable, iso, options);
        } catch (const std::exception &failure) {
            start_failure = failure.what();
            throw;
        }
        auto &game = *session;
        // The game text is patched into the game as it starts: a change waits for the next start.
        std::optional<sarusaru::translation::TexturePack> translation_pack;
        if (current.translation_language == "en") {
            translation_pack = sarusaru::translation::TexturePack::load(current.translation_pack,
                sarusaru::disc_id, sarusaru::executable_sha256, [&](std::string_view path) {
                    const auto entry = game.disc.find(path);
                    if (!entry || entry->directory || entry->size > 32u * 1024u * 1024u)
                        throw std::runtime_error("translation pack: missing or oversized source " + std::string(path));
                    return game.disc.read_file(*entry, 32u * 1024u * 1024u);
                });
            translation_pack->apply_strings(game.runtime.memory(), game.elf, sarusaru::load_base);
            translation_pack->apply_dialogue(game.runtime.memory(), game.elf, sarusaru::load_base,
                [&](std::uint32_t size) {
                    const auto uid = game.kernel.allocate_partition_memory("English dialogue", 1u, size, 0u);
                    if (uid < 0) return 0u;
                    return game.kernel.block_head_address(uid);
                });
            std::cout << "Translation: " << translation_pack->locale() << " (" << translation_pack->size()
                      << " menu textures, " << translation_pack->string_count()
                      << " text strings, " << translation_pack->dialogue_count()
                      << " compact messages); untranslated screens retain Japanese\n";
        }
        // The movies' English subtitles: from SARUSARU_SUBTITLES, else the
        // translation pack's own, else the subtitles folder beside the pack
        // or the settings file.
        sarusaru::movie::SubtitleLibrary subtitles;
        const char *subtitle_override = std::getenv("SARUSARU_SUBTITLES");
        const bool overridden = subtitle_override != nullptr && *subtitle_override != '\0';
        if (!overridden && translation_pack && !translation_pack->subtitles().empty()) {
            for (const auto &[key, srt] : translation_pack->subtitles()) {
                try {
                    subtitles.add(key, sarusaru::movie::parse_srt(srt));
                } catch (const std::exception &failure) {
                    std::cerr << "sarusaru: subtitles " << key << " in the translation pack: " << failure.what() << '\n';
                }
            }
            std::cout << "Subtitles: " << subtitles.size() << " movies from the translation pack\n";
        } else {
            std::vector<std::filesystem::path> folders;
            if (overridden) folders.emplace_back(subtitle_override);
            if (!current.translation_pack.empty()) folders.push_back(current.translation_pack.parent_path() / "subtitles");
            folders.push_back(settings_path.parent_path() / "subtitles");
            for (const auto &folder : folders) {
                std::error_code missing;
                if (!std::filesystem::is_directory(folder, missing)) continue;
                subtitles.load_directory(folder, "en");
                std::cout << "Subtitles: " << subtitles.size() << " movies from " << folder.string() << '\n';
                break;
            }
        }
        std::optional<sarusaru::textures::HdTexturePack> hd_textures;
        std::optional<sarusaru::textures::TextureDump> texture_dump;
        // The HD pack, and where textures it lacks are collected. At startup
        // a pack that cannot load stops the game; from the pause menu it
        // leaves the original textures.
        const auto load_textures = [&](const std::filesystem::path &pack) {
            texture_dump.reset();
            hd_textures.reset();
            if (!pack.empty()) {
                hd_textures = sarusaru::textures::HdTexturePack::load(pack, sarusaru::disc_id);
                std::cout << "HD textures: " << hd_textures->size() << " at " << hd_textures->scale() << "x from "
                          << pack.string() << '\n';
            }
            if (!texture_dump_directory.empty()) {
                texture_dump.emplace(texture_dump_directory);
                std::cout << "Dumping textures to " << texture_dump_directory.string() << " (" << texture_dump->known()
                          << " already there)\n";
            } else if (hd_textures) {
                // Textures the pack's recipe could not find on the disc are kept
                // as they are drawn, for the launcher to upscale next time.
                if (auto wanted = hd_textures->wanted()) {
                    const auto count = wanted->size();
                    texture_dump.emplace(hd_textures->directory() / sarusaru::textures::HdTexturePack::kCollectedName,
                                         std::move(wanted));
                    std::cout << "Collecting up to " << count << " textures for the HD pack\n";
                }
            }
        };
        load_textures(current.texture_pack);
        // A renderer for `current`; another replaces it when the resolution or textures change.
        const auto make_renderer = [&] {
            if (renderer) {
                vulkan.vkDeviceWaitIdle(vulkan.device);
                renderer->destroy();
            }
            renderer = std::make_unique<sarusaru::shell::GeRenderer>(vulkan, current.internal_resolution);
            renderer->set_cel_shading(current.cel_shading);
            if (translation_pack) renderer->set_translation_pack(&*translation_pack);
            if (hd_textures) renderer->set_hd_textures(&*hd_textures);
            if (texture_dump) renderer->set_texture_dump(&*texture_dump);
            renderer->set_button_prompts(&button_prompts);
            if (!renderer->create(error)) throw psprecomp::Error(error);
        };
        make_renderer();
        auto &memory = game.runtime.memory();
        // Frame interpolation: each draw is also blended with its match in
        // the previous game frame, for the in-between picture. A game frame
        // ends when the game hands the display its next framebuffer.
        sarusaru::gpu::FrameInterpolator interpolator;
        sarusaru::gpu::DrawCall between;
        std::uint64_t frame_requests = game.display.framebuffer_requests();
        // FOV: while a gameplay camera drives the frame, every perspective
        // projection is rescaled before the draw reaches the renderer (and
        // the interpolator, which then eases the change over a frame).
        // The title, menus and cutscenes keep the native projection.
        game.ge.state().set_view_hook([&fov, &game](sarusaru::gpu::DrawCall &call) {
            if (fov.enabled() && game.gameplay_camera.active()) fov.apply(call);
        });
        const auto render_draw = [&](const sarusaru::gpu::DrawCall &call) {
            renderer->draw(memory, call);
            if (!interpolating) return;
            if (const auto requests = game.display.framebuffer_requests(); requests != frame_requests) {
                frame_requests = requests;
                interpolator.end_frame();
            }
            interpolator.interpolate(call, 0.5f, between);
            renderer->queue_interpolated(memory, between);
        };
        game.ge.state().set_draw_sink([&](const sarusaru::gpu::DrawCall &call) {
            if (translation_pack) translation_pack->translate_draw(memory, call, render_draw);
            else render_draw(call);
        });
        std::uint64_t latches = game.display.framebuffer_latches(game.kernel.now_us()), latch_vblank = 0u;
        game.audio.set_sink([&](std::int32_t channel, std::span<const std::int16_t> samples, bool mono,
                                std::uint32_t left, std::uint32_t right) {
            audio.play(channel, samples, mono, left, right);
        });

        // Applies what the pause menu changed, between two frames.
        const auto apply_settings = [&] {
            const sarusaru::shell::Settings next = game_settings(settings, overrides, settings_path);
            if (next.controls != current.controls) input.emplace(next.controls);
            if (next.fov_degrees != current.fov_degrees) fov = sarusaru::gpu::FovOverride(next.fov_degrees);
            if (next.frame_rate != current.frame_rate) {
                interpolating = sarusaru::shell::interpolates(next);
                presentation = sarusaru::shell::PresentationClock(interpolating ? 0u : next.frame_rate);
                // The next game frame starts the in-between pictures afresh.
                interpolator.reset();
                frame_requests = game.display.framebuffer_requests();
                latches = game.display.framebuffer_latches(game.kernel.now_us());
                latch_vblank = sarusaru::kernel::vblanks_by(game.kernel.now_us());
            }
            const bool new_textures = next.texture_pack != current.texture_pack;
            const bool new_renderer = new_textures || next.internal_resolution != current.internal_resolution;
            // Kept for the next start: the game text and the intro logos.
            const auto translation_language = current.translation_language;
            const auto translation_pack_path = current.translation_pack;
            const bool skip_intro_logos = current.skip_intro_logos;
            current = next;
            current.translation_language = translation_language;
            current.translation_pack = translation_pack_path;
            current.skip_intro_logos = skip_intro_logos;
            if (new_textures) {
                if (renderer) {
                    vulkan.vkDeviceWaitIdle(vulkan.device);
                    renderer->destroy();
                    renderer.reset();
                }
                try {
                    load_textures(current.texture_pack);
                } catch (const std::exception &failure) {
                    std::cerr << "sarusaru: " << failure.what() << "; the original textures are used\n";
                    load_textures({});
                }
            }
            if (new_renderer) make_renderer();
            else renderer->set_cel_shading(current.cel_shading);
        };

        // SARUSARU_INPUT scripts the pad, as in the bootstrap; the
        // keyboard and gamepads are then ignored.
        const bool scripted = std::getenv("SARUSARU_INPUT") != nullptr && *std::getenv("SARUSARU_INPUT") != '\0';
        if (scripted) {
            auto script = sarusaru::ctrl::parse_input_script(std::getenv("SARUSARU_INPUT"));
            if (!script) throw psprecomp::Error("Malformed SARUSARU_INPUT script");
            game.controller.set_script(std::move(*script));
        }
        // SARUSARU_CAPTURE saves frames for checking the picture unattended.
        const char *capture_directory = std::getenv("SARUSARU_CAPTURE");
        std::uint64_t capture_every_us = 5'000'000u;
        if (const char *every = std::getenv("SARUSARU_CAPTURE_EVERY"); every != nullptr && *every != '\0')
            capture_every_us = static_cast<std::uint64_t>(std::strtod(every, nullptr) * 1e6);
        std::uint64_t next_capture_us = capture_every_us;
        if (const char *from = std::getenv("SARUSARU_CAPTURE_FROM"); from != nullptr && *from != '\0')
            next_capture_us = static_cast<std::uint64_t>(std::strtod(from, nullptr) * 1e6);
        std::uint32_t capture_width = 0u, capture_height = 0u, captures = 0u;
        if (capture_directory != nullptr &&
            !vulkan.create_buffer(4096u * 4096u * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT, capture))
            throw psprecomp::Error("cannot allocate the capture buffer");

        // SARUSARU_TRACE_DRAWS=<seconds> prints the draws of the two vblanks after that game time.
        std::uint64_t trace_at_us = std::numeric_limits<std::uint64_t>::max();
        if (const char *trace = std::getenv("SARUSARU_TRACE_DRAWS"); trace != nullptr && *trace != '\0')
            trace_at_us = static_cast<std::uint64_t>(std::strtod(trace, nullptr) * 1e6);

        // The pause menu, made once for the session.
        sarusaru::ui::Launcher menu(layer, settings, settings_path, sarusaru::supported_disc(),
                                    sarusaru::ui::Launcher::Mode::InGame);
        // The menu opens when its action is pressed, not while it stays held,
        // and the game sees nothing until whatever closed the menu is let go.
        bool menu_held = true, settling = false;

        renderer->begin_frame(vulkan.begin_frame());
        std::uint64_t real_origin_ns = SDL_GetTicksNS();
        std::uint64_t guest_origin_us = game.kernel.now_us();
        bool quit = false;
        game.kernel.set_frame_observer([&] {
            if (quit) return;
            if (translation_pack) translation_pack->flush_draws(render_draw);
            // A UI script carries on while the game runs (to open the pause menu).
            if (script != nullptr) {
                layer.update_time();
                script->tick(layer);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) quit = true;
                // Alt+Enter or F11 toggles fullscreen (at the desktop's
                // resolution); the swapchain follows the new size.
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                    (event.key.scancode == SDL_SCANCODE_F11 ||
                     (event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT) != 0))) {
                    SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0);
                    continue;
                }
                input->handle(event);
                // Dear ImGui's backend reopens its gamepads after these; anything else it would only queue.
                if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED)
                    layer.handle(event);
                else
                    layer.observe(event);
            }
            update_prompts();
            auto controls = input->sample();
            const bool open_menu = controls.menu && !menu_held && !quit;
            menu_held = controls.menu;
            // What opened the menu (L3+R3, say) does nothing in the game.
            if (open_menu) controls = {};
            if (settling) {
                settling = controls.pad.buttons != 0u || controls.menu || controls.fast_forward || controls.camera_reset;
                controls = {};
            }
            if (!scripted) game.controller.set_pad(controls.pad);
            game.orbit_camera.set_input({controls.camera_x, controls.camera_y, controls.camera_reset});

            std::uint32_t width = 0u, height = 0u;
            const auto framebuffer = game.display.framebuffer(1u, game.kernel.now_us());
            // A newly displayed game frame first shows as the picture
            // between it and the one before, then as itself at the next
            // vblank. Only for frames of two or more vblanks: a game frame
            // of one leaves no vblank to show the in-between picture at.
            bool in_between = false;
            if (interpolating) {
                renderer->draw_interpolated();
                const auto now = game.kernel.now_us();
                if (const auto latched = game.display.framebuffer_latches(now); latched != latches) {
                    const auto vblank = sarusaru::kernel::vblanks_by(now);
                    in_between = vblank - latch_vblank >= 2u;
                    latches = latched;
                    latch_vblank = vblank;
                }
            }
            const auto *image = renderer->presentable(memory, framebuffer, width, height, in_between);
            const bool capturing = capture.buffer != VK_NULL_HANDLE && image != nullptr &&
                                   game.kernel.now_us() >= next_capture_us;
            if (capturing) {
                capture_width = width;
                capture_height = height;
                vulkan.copy_to_buffer(*image, width, height, capture);
                next_capture_us = game.kernel.now_us() + capture_every_us;
            }
            // A movie's subtitle line is drawn over the picture as it is presented.
            const sarusaru::movie::Cue *cue = nullptr;
            if (current.subtitles && subtitles.size() != 0u)
                if (const auto movie = game.psmf_player.playing()) cue = subtitles.at(movie->path, movie->position_ms);
            const bool due = presentation.due(game.kernel.now_us());
            if (cue != nullptr && due && image != nullptr) {
                vulkan.end_frame(image, width, height, false);
                if (capturing) {
                    char name[64];
                    std::snprintf(name, sizeof name, "/subtitled_%04u_%08llums.bmp", captures,
                                  static_cast<unsigned long long>(game.kernel.now_us() / 1000u));
                    layer.capture_next(std::string(capture_directory) + name);
                }
                layer.present_over(image, width, height, [cue](ImVec2 origin, ImVec2 size) {
                    sarusaru::ui::draw_subtitle(cue->text, origin, size);
                });
            } else {
                vulkan.end_frame(image, width, height, due);
            }
            if (open_menu) {
                // The game waits in this callback until the menu closes, over
                // its last picture; nothing it queued plays meanwhile.
                audio.clear();
                game.controller.set_pad({});
                layer.reset_input();
                layer.set_backdrop(image, width, height);
                if (menu.run(script) == sarusaru::ui::Launcher::Outcome::Quit) quit = true;
                layer.set_backdrop(nullptr, 0u, 0u);
                if (!quit) apply_settings();
                settling = true;
                menu_held = true;
                // Pacing starts again from now rather than catching up.
                real_origin_ns = SDL_GetTicksNS();
                guest_origin_us = game.kernel.now_us();
            }
            renderer->begin_frame(vulkan.begin_frame());
            if (game.kernel.now_us() >= trace_at_us) {
                std::fprintf(stderr, "frame at %llu us\n", static_cast<unsigned long long>(game.kernel.now_us()));
                renderer->trace_frames(2u);
                trace_at_us = std::numeric_limits<std::uint64_t>::max();
            }
            if (capturing) {
                char name[64];
                std::snprintf(name, sizeof name, "/frame_%04u_%08llums.bmp", captures++,
                              static_cast<unsigned long long>(game.kernel.now_us() / 1000u));
                SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(capture_width),
                    static_cast<int>(capture_height), SDL_PIXELFORMAT_ABGR8888, capture.mapped,
                    static_cast<int>(capture_width * 4u));
                if (surface != nullptr) {
                    SDL_SaveBMP(surface, (std::string(capture_directory) + name).c_str());
                    SDL_DestroySurface(surface);
                }
            }

            // Pace guest time to real time; Tab runs as fast as it can.
            const std::uint64_t guest_ns = (game.kernel.now_us() - guest_origin_us) * 1000u;
            const std::uint64_t target_ns = real_origin_ns + guest_ns;
            const std::uint64_t now_ns = SDL_GetTicksNS();
            const bool fast = controls.fast_forward;
            if (fast || now_ns > target_ns + kMaxLagNs) {
                real_origin_ns = now_ns;
                guest_origin_us = game.kernel.now_us();
                if (!fast) audio.clear();
            } else if (target_ns > now_ns) {
                SDL_DelayPrecise(target_ns - now_ns);
            }
            if (quit) game.runtime.stop("Window closed");
        });
        game.run(std::numeric_limits<std::uint64_t>::max());
        if (!quit) {
            std::cerr << "sarusaru: the game stopped: " << game.runtime.stop_reason() << '\n';
            status = 3;
        }
    } catch (const std::exception &failure) {
        std::cerr << "sarusaru: " << failure.what() << '\n';
        status = 1;
    }
    vulkan.destroy_buffer(capture);
    if (renderer) renderer->destroy();
    return status;
}

// --install: builds the game module from `iso` with the launcher's builder,
// reporting progress on the terminal.
int install_from_command_line(const std::filesystem::path &iso) {
    using sarusaru::install::Progress;
    using sarusaru::install::ToolchainCheck;
    sarusaru::install::ProcessGroup probe;
    const auto check = sarusaru::install::check_toolchain(probe);
    switch (check.status) {
    case ToolchainCheck::Status::Ready: break;
    case ToolchainCheck::Status::Download:
        std::cout << "Downloading the compiler first (" << check.download_bytes / 1'000'000u << " MB)\n";
        break;
    case ToolchainCheck::Status::NeedXcode:
        std::cerr << "sarusaru: install Xcode's Command Line Tools first (xcode-select --install)\n";
        return 1;
    case ToolchainCheck::Status::NeedCompiler:
        std::cerr << "sarusaru: install a C++ compiler first (g++ or clang++ from your distribution)\n";
        return 1;
    case ToolchainCheck::Status::Unsupported:
        std::cerr << "sarusaru: this build cannot install the game; build it with SARUSARU_GAME_MODULE\n";
        return 1;
    }
    sarusaru::install::GameBuilder builder(iso, sarusaru::supported_disc(), check.download_bytes);
    std::cout << "Installing from " << iso.string() << " with " << builder.jobs() << " compiler jobs\n";
    constexpr const char *kSteps[] = {"Starting", "Preparing the compiler", "Reading the disc", "Generating the code",
                                      "Compiling", "Linking", "Done", "Failed", "Cancelled"};
    Progress last;
    last.step = Progress::Step::Done;
    for (;;) {
        const Progress now = builder.progress();
        if (now.step != last.step || (now.step == Progress::Step::Compiling && now.done != last.done)) {
            std::cout << kSteps[static_cast<int>(now.step)];
            if (now.step == Progress::Step::Compiling) std::cout << ' ' << now.done << '/' << now.total;
            std::cout << std::endl;
            last = now;
        }
        if (now.finished()) {
            if (now.step == Progress::Step::Failed) std::cerr << "sarusaru: " << now.error << '\n';
            return now.step == Progress::Step::Done ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace

int main(int argc, char **argv) {
#if defined(__ANDROID__)
    redirect_output_to_log();
#endif
    GameOverrides overrides;
    std::filesystem::path texture_dump_directory;
    std::filesystem::path settings_path;
    std::vector<std::string_view> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--version") {
            std::cout << "SaruSaru Recomp " << sarusaru::release_version << '\n';
            return 0;
        }
        if (arg == "--help") {
            std::cout << kUsage;
            return 0;
        }
        if (arg == "--print-default-settings") {
            std::cout << sarusaru::shell::serialize_settings(sarusaru::shell::default_settings());
            return 0;
        }
        if (arg == "--install" && i + 1 < argc) return install_from_command_line(argv[i + 1]);
        if (arg == "--settings" && i + 1 < argc) {
            settings_path = argv[++i];
            continue;
        }
        if (arg == "--translation-pack" && i + 1 < argc) {
            overrides.translation_pack = std::filesystem::absolute(argv[++i]);
            continue;
        }
        if (arg == "--texture-pack" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            overrides.texture_pack = value == "none" ? std::filesystem::path{} : std::filesystem::absolute(value);
            continue;
        }
        if (arg == "--dump-textures" && i + 1 < argc) {
            texture_dump_directory = std::filesystem::absolute(argv[++i]);
            continue;
        }
        if (arg == "--scale" && i + 1 < argc) {
            const std::string_view value = argv[++i];
            std::uint32_t parsed{};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size() || parsed < 1u || parsed > 8u) {
                std::cerr << "sarusaru: --scale takes 1 to 8\n";
                return 2;
            }
            overrides.scale = parsed;
            continue;
        }
        if (arg.starts_with("--")) {
            std::cerr << "sarusaru: unknown option or missing value: " << arg << '\n';
            return 2;
        }
        paths.push_back(arg);
    }
    if (paths.size() > 2u) {
        std::cerr << kUsage;
        return 2;
    }
    const bool launcher = paths.empty();
    std::filesystem::path executable, iso;
    if (paths.size() == 2u) executable = paths[0];
    if (!paths.empty()) iso = paths.back();

    sarusaru::shell::Settings settings;
    std::optional<std::string> ui_script;
    try {
        if (settings_path.empty()) settings_path = sarusaru::shell::default_settings_path();
        settings = sarusaru::shell::load_settings(settings_path);
        std::cout << "Settings: " << settings_path.string() << '\n';
        ui_script = sarusaru::ui::UiScript::from_environment();
    } catch (const std::exception &error) {
        std::cerr << "sarusaru: " << error.what() << '\n';
        // Started from Finder or Explorer there is no terminal for the error.
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SaruSaru Recomp", error.what(), nullptr);
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::cerr << "sarusaru: SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    int status = 0;
    {
        sarusaru::shell::VulkanContext vulkan;
        std::string error;
        if (!vulkan.vk.load(error)) {
            std::cerr << "sarusaru: " << error << '\n';
            SDL_Quit();
            return 1;
        }
        SDL_Window *window = SDL_CreateWindow("Saru! Get You! SaruSaru Daisakusen", static_cast<int>(settings.window_width),
            static_cast<int>(settings.window_height),
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                (settings.fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WindowFlags{0}));
        try {
            if (window == nullptr) throw psprecomp::Error(std::string("cannot create the window: ") + SDL_GetError());
            set_window_icon(window);
#if defined(__ANDROID__)
            // Started behind the lock screen or another app, the window has
            // no surface to draw on until the app is shown, and it can lose
            // the one it had at any moment: wait for it, and try again if it
            // went while Vulkan was being set up.
            const auto has_surface = [window] {
                return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,
                                              nullptr) != nullptr;
            };
            for (;;) {
                while (!has_surface()) {
                    SDL_Event event;
                    if (SDL_WaitEventTimeout(&event, 100) && event.type == SDL_EVENT_QUIT)
                        throw psprecomp::Error("closed before the window was shown");
                }
                if (vulkan.create(window, error)) break;
                if (has_surface()) throw psprecomp::Error(error);
                vulkan.destroy();
            }
#else
            if (!vulkan.create(window, error)) throw psprecomp::Error(error);
#endif
            std::string start_failure;
            // The interface lives for the whole session: the launcher's, then the game's pause menu.
            sarusaru::ui::Layer layer(window, vulkan);
            if (!layer.create(error)) throw psprecomp::Error(error);
            // The script carries on where it left off if the launcher returns.
            std::optional<sarusaru::ui::UiScript> script;
            if (ui_script) script.emplace(*ui_script);
            for (;;) {
                if (launcher) {
                    sarusaru::ui::Launcher menu(layer, settings, settings_path, sarusaru::supported_disc());
                    if (!start_failure.empty()) menu.show_start_failure(start_failure);
                    if (menu.run(script ? &*script : nullptr) == sarusaru::ui::Launcher::Outcome::Quit) {
                        status = 0;
                        break;
                    }
                    iso = settings.game_disc;
                }
                const auto resolved = game_settings(settings, overrides, settings_path);
                if (resolved.translation_language == "en" && resolved.translation_pack.empty())
                    throw std::runtime_error("English needs a translation pack; use --translation-pack FILE or [translation] pack");
                start_failure.clear();
                status = play(window, vulkan, layer, settings, settings_path, overrides, executable, iso,
                              texture_dump_directory, script ? &*script : nullptr, start_failure);
                if (!launcher || start_failure.empty()) break;
            }
        } catch (const std::exception &failure) {
            std::cerr << "sarusaru: " << failure.what() << '\n';
            status = 1;
        }
        vulkan.destroy();
        if (window != nullptr) SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return status;
}
