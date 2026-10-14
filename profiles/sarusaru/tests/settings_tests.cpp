#include "shell/settings.hpp"
#include "shell/input.hpp"
#include "shell/presentation_clock.hpp"
#include "kernel/guest_clock.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void rejects(std::string_view text) {
    try { (void)sarusaru::shell::parse_settings(text); }
    catch (const std::runtime_error &) { return; }
    require(false, "malformed settings accepted");
}
}
int main() {
    using namespace sarusaru::shell;
    using namespace sarusaru::ctrl;
    const Settings defaults = default_settings();
    require(parse_settings(serialize_settings(defaults)) == defaults, "default settings do not round-trip");
    require(parse_settings("# empty file inherits defaults\n") == defaults, "defaults not inherited");
    require(!defaults.cel_shading, "cel shading must default to off");
    const auto cel = parse_settings("[graphics]\ncel_shading = 1\n");
    require(cel.cel_shading && parse_settings(serialize_settings(cel)) == cel, "cel shading does not round-trip");
    require(!parse_settings("[graphics]\ncel_shading = 0\n").cel_shading, "cel shading off not parsed");
    const auto outlined = parse_settings("[graphics]\ncel_shading = 2\n");
    require(outlined.cel_shading == 2 && parse_settings(serialize_settings(outlined)) == outlined, "outlined preset does not round-trip");
    rejects("[graphics]\ncel_shading = 3");
    rejects("[graphics]\ncel_shading = -1");
    rejects("[graphics]\ncel_shading = yes");
    const auto english = parse_settings("[translation]\nlanguage = en\npack = packs/English menu.sstp\n");
    require(english.translation_language == "en" && english.translation_pack == "packs/English menu.sstp", "translation selection not parsed");
    const auto textured = parse_settings("[graphics]\ntexture_pack = textures/hd 4x\n");
    require(textured.texture_pack == "textures/hd 4x" && parse_settings(serialize_settings(textured)) == textured,
            "texture pack setting does not round-trip");
    require(parse_settings("[graphics]\ntexture_pack = none\n").texture_pack.empty(), "texture_pack none not parsed");
    require(parse_settings(serialize_settings(english)) == english, "translation settings do not round-trip");
    const auto independent = parse_settings(
        "[translation]\nlanguage = en\npack = packs/menu.sstp\n[interface]\nlanguage = ja\n");
    require(independent.translation_language == "en" && independent.language == Language::Japanese,
            "game and launcher languages must be independent");
    require(parse_settings(serialize_settings(independent)) == independent, "both languages do not round-trip");
    rejects("[translation]\nlanguage = fr");
    rejects("[translation]\npack = ");
    const auto custom = parse_settings(
        "[graphics]\r\ninternal_resolution = 4\r\nwindow_width = 1920\nwindow_height = 1080\nfullscreen = 1\nframe_rate = 30\nexperimental_game_frame_rate = 60\nframe_interpolation = 1\nfov_degrees = 75\n"
        "[input]\ndead_zone = 0.3\n[keyboard]\ncircle = C, V\nquit = none\nfast_forward = F\n"
        "[gamepad]\nanalog_left = axis:rightx+\nanalog_right = axis:rightx-\nl = axis:righttrigger+\n");
    require(custom.internal_resolution == 4 && custom.frame_rate == 30 && custom.fov_degrees == 75 && custom.experimental_game_frame_rate == 60 &&
                custom.frame_interpolation && custom.window_width == 1920 && custom.window_height == 1080 &&
                custom.fullscreen,
            "graphics settings not parsed");
    require(parse_settings(serialize_settings(custom)) == custom, "custom bindings do not round-trip");
    for (const auto text : {
        "[graphics]\ninternal_resolution = 0", "[graphics]\nframe_rate = 61",
        "[graphics]\nexperimental_game_frame_rate = 24", "[graphics]\nexperimental_game_frame_rate = 120",
        "[graphics]\nframe_rate = 30junk", "[graphics]\nframe_interpolation = 2", "[graphics]\nwindow_width = 100",
        "[graphics]\nwindow_height = 99999", "[graphics]\nfullscreen = yes", "[graphics]\nfov_degrees = 10",
        "[graphics]\nfov_degrees = nan", "[graphics]\nframe_rate = 30\nframe_rate = 60",
        "[input]\ndead_zone = 1", "[input]\ndead_zone = -0.1", "[input]\nunknown = 1",
        "[keyboard]\ncircle = NotAKey", "[keyboard]\nunknown = C", "[keyboard]\ncircle = C,",
        "[gamepad]\nl = axis:leftx", "[gamepad]\nl = axis:unknown+", "[gamepad]\nl = unknown",
        "[keyboard]\ncircle = scancode:9999", "[future]\nvalue = 1", "frame_rate = 30",
        "[graphics]\nframe_rate", "[keyboard]\ncircle = "}) rejects(text);

    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    std::array<GamepadState, 2> pads{};
    auto state = map_input(defaults.controls, keys, pads);
    require(state.pad.buttons == 0 && state.pad.lx == 128 && state.pad.ly == 128 && !state.quit,
            "neutral controls not centered");
    keys[SDL_SCANCODE_Z] = keys[SDL_SCANCODE_J] = keys[SDL_SCANCODE_I] = true;
    state = map_input(defaults.controls, keys, pads);
    require(state.pad.buttons == kButtonCircle && state.pad.lx == 1 && state.pad.ly == 1, "default keyboard bindings changed");
    keys.fill(false);
    keys[SDL_SCANCODE_Z] = true;
    require(map_input(custom.controls, keys, pads).pad.buttons == 0, "old keyboard binding still active");
    keys[SDL_SCANCODE_V] = keys[SDL_SCANCODE_F] = keys[SDL_SCANCODE_ESCAPE] = true;
    state = map_input(custom.controls, keys, pads);
    require(state.pad.buttons == kButtonCircle && state.fast_forward && !state.quit, "alternatives/unbound host actions failed");
    keys.fill(false);
    pads[0].buttons[SDL_GAMEPAD_BUTTON_SOUTH] = true;
    pads[1].buttons[SDL_GAMEPAD_BUTTON_START] = true;
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 20000;
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFTX] = -32768;
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFTY] = 32767;
    state = map_input(defaults.controls, keys, pads);
    require(state.pad.buttons == (kButtonCross | kButtonStart | kButtonL) && state.pad.lx == 1 && state.pad.ly == 255,
            "multi-pad buttons, triggers or axis extremes failed");
    pads = {};
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFTX] = 6000;
    require(map_input(defaults.controls, keys, pads).pad.lx == 128, "stick noise escaped dead zone");
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFTX] = 32767;
    require(map_input(custom.controls, keys, pads).pad.lx == 128, "old gamepad axis binding still active");
    pads[0].axes[SDL_GAMEPAD_AXIS_RIGHTX] = 32767;
    require(map_input(custom.controls, keys, pads).pad.lx == 1, "rebound/inverted gamepad axis failed");
    pads[0].axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 16000;
    require(map_input(custom.controls, keys, pads).pad.buttons == 0, "trigger fired below threshold");
    pads[0].axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 20000;
    require(map_input(custom.controls, keys, pads).pad.buttons == (kButtonL | kButtonR), "shared trigger rebinding failed");
    auto numeric = parse_settings("[keyboard]\ncircle = scancode:54");
    require(parse_settings(serialize_settings(numeric)) == numeric, "delimiter key did not round-trip");

    // The launcher's own settings: the disc image (any path, including the
    // comment mark, escapes, edge spaces and non-ASCII) and the confirm button.
    Settings launcher = defaults;
    launcher.confirm_south = false;
    launcher.language = Language::Japanese;
    for (const auto *name : {u8"/Games/PSP #1/100% サル.iso", u8" leading and trailing ", u8"C:\\Games\\%23.iso"}) {
        launcher.game_disc = std::filesystem::path(name);
        require(parse_settings(serialize_settings(launcher)) == launcher, "game disc path did not round-trip");
    }
    require(parse_settings("[game]\ndisc = /a/b%23c.iso\n").game_disc == std::filesystem::path("/a/b#c.iso"),
            "escaped game disc path not decoded");
    require(!parse_settings("[interface]\nconfirm = east\n").confirm_south, "confirm button not parsed");
    require(defaults.confirm_south && defaults.game_disc.empty() && defaults.language == Language::Auto,
            "launcher defaults changed");
    require(parse_settings("[interface]\nlanguage = en\n").language == Language::English, "language not parsed");
    for (const auto text : {"[interface]\nconfirm = north", "[game]\ndisc = bad%2", "[interface]\nlanguage = fr", "[game]\ndisc = bad%zz"}) rejects(text);

    PresentationClock native(0), thirty(30), sixty(60), one(1);
    unsigned n = 0, t = 0, s = 0, o = 0;
    for (std::uint64_t i = 0; i <= 600; ++i) {
        const auto time = sarusaru::kernel::vblank_time_us(i);
        n += native.due(time); t += thirty.due(time); s += sixty.due(time); o += one.due(time);
    }
    require(n == 601 && s == 601 && t == 301 && o == 11, "presentation cap drifted or altered native rate");
    require(!thirty.due(sarusaru::kernel::vblank_time_us(600)), "presented twice at the same time");
    require(thirty.due(100'000'000) && !thirty.due(100'000'000), "long stall caused repeated catch-up frames");

    const auto directory = std::filesystem::temp_directory_path() /
        ("sarusaru-settings-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path = directory / "nested" / "settings.ini";
    require(load_settings(path) == defaults && load_settings(path) == defaults, "missing file creation/reload failed");
    { std::ofstream out(path); out << "[graphics]\ninternal_resolution = 5\n"; }
    require(load_settings(path).internal_resolution == 5, "existing settings not loaded");
    { std::ofstream out(path); out << "broken file\n"; }
    try { (void)load_settings(path); require(false, "invalid existing file accepted"); }
    catch (const std::runtime_error &) {}
    std::ifstream in(path); std::string line; std::getline(in, line);
    require(line == "broken file", "invalid existing file overwritten");
    in.close();
    save_settings(path, launcher);
    require(load_settings(path) == launcher && !std::filesystem::exists(path.string() + ".new"),
            "saved settings did not reload");
    std::filesystem::remove_all(directory);
    std::cout << "Settings, control mapping and presentation clock tests passed\n";
}
