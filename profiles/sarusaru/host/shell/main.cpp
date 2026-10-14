// The windowed game: GameSession driven at real time, the GE's draws rendered
// with Vulkan, sound through SDL, and the keyboard or a gamepad as the pad.
//
// Everything runs on the main thread. The game runs inside GameSession::run;
// at each vblank of the guest's virtual clock the kernel calls back here, and
// the shell pumps events, presents the displayed framebuffer and waits until
// real time catches up with guest time.
#include "session.hpp"
#include "gpu/fov.hpp"
#include "gpu/frame_interpolator.hpp"
#include "kernel/guest_clock.hpp"
#include "psprecomp/common.hpp"
#include "shell/audio_output.hpp"
#include "shell/ge_renderer.hpp"
#include "shell/input.hpp"
#include "shell/presentation_clock.hpp"
#include "shell/vulkan_context.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr const char *kUsage =
    "Usage: sarusaru [--settings FILE] [--scale N] <prepared BOOT.elf> <game ISO>\n"
    "  --settings FILE  settings and bindings (default: per-user settings.ini)\n"
    "  --scale N  override the file's internal resolution (1-8 times 480x272)\n"
    "  --print-default-settings  print the settings file template and exit\n"
    "Alt+Enter or F11 toggles fullscreen.\n"
    "Default keys: arrows d-pad, Z circle, X cross, A square, S triangle, Q/W L/R, Enter start,\n"
    "      Backspace select, I/J/K/L analog stick, Tab (held) fast forward, Escape quit.\n"
    "Environment: SARUSARU_CAPTURE=<directory> saves the picture every SARUSARU_CAPTURE_EVERY\n"
    "  seconds of game time (default 5) as BMP files, from SARUSARU_CAPTURE_FROM seconds if set;\n"
    "  SARUSARU_INPUT scripts the pad.\n";

// Real time may trail guest time by this much before pacing gives up on
// catching up (after a stall) and starts again from now.
constexpr std::uint64_t kMaxLagNs = 100'000'000u;

#include "window_icon.inc"

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

} // namespace

int main(int argc, char **argv) {
    std::optional<std::uint32_t> scale;
    std::filesystem::path settings_path;
    std::vector<std::string_view> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help") {
            std::cout << kUsage;
            return 0;
        }
        if (arg == "--print-default-settings") {
            std::cout << sarusaru::shell::serialize_settings(sarusaru::shell::default_settings());
            return 0;
        }
        if (arg == "--settings" && i + 1 < argc) {
            settings_path = argv[++i];
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
            scale = parsed;
            continue;
        }
        if (arg.starts_with("--")) {
            std::cerr << "sarusaru: unknown option or missing value: " << arg << '\n';
            return 2;
        }
        paths.push_back(arg);
    }
    if (paths.size() != 2u) {
        std::cerr << kUsage;
        return 2;
    }

    sarusaru::shell::Settings settings;
    try {
        if (settings_path.empty()) settings_path = sarusaru::shell::default_settings_path();
        settings = sarusaru::shell::load_settings(settings_path);
        if (scale) settings.internal_resolution = *scale;
        std::cout << "Settings: " << settings_path.string() << '\n';
        if (settings.experimental_game_frame_rate != 0)
            std::cerr << "sarusaru: experimental game frame rate " << settings.experimental_game_frame_rate
                      << "; fixed-step gameplay/animation speed is not compensated\n";
        if (settings.frame_interpolation && settings.experimental_game_frame_rate != 0) {
            std::cerr << "sarusaru: frame_interpolation needs the native game rate "
                         "(experimental_game_frame_rate = 0); interpolation is off\n";
            settings.frame_interpolation = false;
        }
    } catch (const std::exception &error) {
        std::cerr << "sarusaru: " << error.what() << '\n';
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
        sarusaru::shell::GeRenderer renderer(vulkan, settings.internal_resolution);
        sarusaru::shell::AudioOutput audio;
        sarusaru::shell::Input input(settings.controls);
        sarusaru::shell::PresentationClock presentation(settings.frame_rate);
        sarusaru::shell::Buffer capture;
        try {
            if (window == nullptr) throw psprecomp::Error(std::string("cannot create the window: ") + SDL_GetError());
            set_window_icon(window);
            if (!vulkan.create(window, error) || !renderer.create(error)) throw psprecomp::Error(error);
            if (!audio.open(error)) std::cerr << "sarusaru: no sound: " << error << '\n';

            sarusaru::SessionOptions options;
            options.memory_stick = sarusaru::utility::default_memory_stick();
            options.experimental_game_frame_rate = settings.experimental_game_frame_rate;
            sarusaru::GameSession game(std::string(paths[0]), std::string(paths[1]), options);
            auto &memory = game.runtime.memory();
            // Frame interpolation: each draw is also blended with its match in
            // the previous game frame, for the in-between picture. A game frame
            // ends when the game hands the display its next framebuffer.
            const bool interpolating = settings.frame_interpolation;
            sarusaru::gpu::FrameInterpolator interpolator;
            sarusaru::gpu::DrawCall between;
            std::uint64_t frame_requests = game.display.framebuffer_requests();
            // FOV: every perspective projection is rescaled before the draw
            // reaches the renderer (and the interpolator).
            const sarusaru::gpu::FovOverride fov(settings.fov_degrees);
            if (fov.enabled()) game.ge.state().set_view_hook([&fov](sarusaru::gpu::DrawCall &call) { fov.apply(call); });
            game.ge.state().set_draw_sink([&](const sarusaru::gpu::DrawCall &call) {
                renderer.draw(memory, call);
                if (!interpolating) return;
                if (const auto requests = game.display.framebuffer_requests(); requests != frame_requests) {
                    frame_requests = requests;
                    interpolator.end_frame();
                }
                interpolator.interpolate(call, 0.5f, between);
                renderer.queue_interpolated(memory, between);
            });
            std::uint64_t latches = game.display.framebuffer_latches(game.kernel.now_us()), latch_vblank = 0u;
            game.audio.set_sink([&](std::int32_t channel, std::span<const std::int16_t> samples, bool mono,
                                    std::uint32_t left, std::uint32_t right) {
                audio.play(channel, samples, mono, left, right);
            });

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

            renderer.begin_frame(vulkan.begin_frame());
            std::uint64_t real_origin_ns = SDL_GetTicksNS();
            std::uint64_t guest_origin_us = game.kernel.now_us();
            bool quit = false;
            game.kernel.set_frame_observer([&] {
                if (quit) return;
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
                    input.handle(event);
                }
                const auto controls = input.sample();
                quit = quit || controls.quit;
                if (!scripted) game.controller.set_pad(controls.pad);

                std::uint32_t width = 0u, height = 0u;
                const auto framebuffer = game.display.framebuffer(1u, game.kernel.now_us());
                // A newly displayed game frame first shows as the picture
                // between it and the one before, then as itself at the next
                // vblank. Only for frames of two or more vblanks: a game frame
                // of one leaves no vblank to show the in-between picture at.
                bool in_between = false;
                if (interpolating) {
                    renderer.draw_interpolated();
                    const auto now = game.kernel.now_us();
                    if (const auto latched = game.display.framebuffer_latches(now); latched != latches) {
                        const auto vblank = sarusaru::kernel::vblanks_by(now);
                        in_between = vblank - latch_vblank >= 2u;
                        latches = latched;
                        latch_vblank = vblank;
                    }
                }
                const auto *image = renderer.presentable(memory, framebuffer, width, height, in_between);
                const bool capturing = capture.buffer != VK_NULL_HANDLE && image != nullptr &&
                                       game.kernel.now_us() >= next_capture_us;
                if (capturing) {
                    capture_width = width;
                    capture_height = height;
                    vulkan.copy_to_buffer(*image, width, height, capture);
                    next_capture_us = game.kernel.now_us() + capture_every_us;
                }
                vulkan.end_frame(image, width, height, presentation.due(game.kernel.now_us()));
                renderer.begin_frame(vulkan.begin_frame());
                if (game.kernel.now_us() >= trace_at_us) {
                    std::fprintf(stderr, "frame at %llu us\n", static_cast<unsigned long long>(game.kernel.now_us()));
                    renderer.trace_frames(2u);
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
        renderer.destroy();
        vulkan.destroy();
        if (window != nullptr) SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return status;
}
