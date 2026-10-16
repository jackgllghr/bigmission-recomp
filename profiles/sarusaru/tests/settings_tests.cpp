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
    require(defaults.state_slot==1,"default state slot changed");
    for (unsigned slot=1;slot<=5;++slot) {
        auto selected=parse_settings("[game]\nstate_slot = "+std::to_string(slot));
        require(selected.state_slot==slot && parse_settings(serialize_settings(selected))==selected,"selected slot did not persist");
    }
    rejects("[game]\nstate_slot = 0"); rejects("[game]\nstate_slot = 6");
    require(parse_settings(serialize_settings(defaults)) == defaults, "default settings do not round-trip");
    // A file from before modern_downloads inherits every other default, but is never offered Modern's downloads.
    require(defaults.modern_downloads_pending, "a new settings file must offer Modern's downloads");
    auto inherited = parse_settings("# empty file inherits defaults\n");
    require(!inherited.modern_downloads_pending, "an earlier file was offered Modern's downloads");
    inherited.modern_downloads_pending = true;
    require(inherited == defaults, "defaults not inherited");
    require(defaults.internal_resolution == 4u && defaults.draw_distance == 8.0f && defaults.skip_intro_logos &&
                defaults.translation_language == "ja" && defaults.texture_pack.empty(),
            "defaults are not Modern's rows that need no download");
    const auto offered = parse_settings("[game]\nmodern_downloads = 0\n");
    require(!offered.modern_downloads_pending && parse_settings(serialize_settings(offered)) == offered,
            "modern_downloads does not round-trip");
    rejects("[game]\nmodern_downloads = 2");
    require(defaults.prompt_style == PromptStyle::Auto, "controller prompts must default to auto");
    for (std::size_t i = 0; i < kPromptStyleNames.size(); ++i) {
        const auto settings = parse_settings(std::string("[interface]\ncontroller_buttons = ") + std::string(kPromptStyleNames[i]));
        require(settings.prompt_style == static_cast<PromptStyle>(i) && parse_settings(serialize_settings(settings)) == settings,
                "controller prompt override did not round-trip");
    }
    rejects("[interface]\ncontroller_buttons = invalid");
    const auto auto_pad = resolve_prompts(PromptStyle::Auto, true, SDL_GAMEPAD_TYPE_PS4);
    const auto auto_keys = resolve_prompts(PromptStyle::Auto, false, SDL_GAMEPAD_TYPE_PS4);
    require(auto_pad.gamepad && auto_pad.type == SDL_GAMEPAD_TYPE_PS4 && !auto_keys.gamepad, "auto stopped following detected input");
    const auto xbox_prompts = resolve_prompts(PromptStyle::Xbox, false, SDL_GAMEPAD_TYPE_PS4);
    const auto switch_prompts = resolve_prompts(PromptStyle::Switch, true, SDL_GAMEPAD_TYPE_XBOXONE);
    const auto ps_prompts = resolve_prompts(PromptStyle::PlayStation, false, SDL_GAMEPAD_TYPE_STANDARD);
    require(xbox_prompts.gamepad && xbox_prompts.type == SDL_GAMEPAD_TYPE_XBOXONE &&
            switch_prompts.gamepad && switch_prompts.type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO &&
            ps_prompts.gamepad && ps_prompts.type == SDL_GAMEPAD_TYPE_PS5 &&
            !resolve_prompts(PromptStyle::Keyboard, true, SDL_GAMEPAD_TYPE_XBOXONE).gamepad,
            "explicit prompt style did not override detected input");
    const auto updates = parse_settings("[interface]\ncheck_for_updates = 0\nlast_update_check = 1234567890\n");
    require(defaults.check_for_updates && !updates.check_for_updates && updates.last_update_check == 1234567890 &&
            parse_settings(serialize_settings(updates)) == updates, "update preferences do not round-trip");
    rejects("[interface]\ncheck_for_updates = 2");
    rejects("[interface]\nlast_update_check = -1");
    rejects("[interface]\nlast_update_check = 9223372036854775808");
    require(defaults.skip_intro_logos, "intro logos must be skipped by default (Modern)");
    require(defaults.subtitles, "movie subtitles must default to on");
    const auto unsubtitled = parse_settings("[translation]\nsubtitles = 0\n");
    require(!unsubtitled.subtitles && parse_settings(serialize_settings(unsubtitled)) == unsubtitled,
            "subtitle option does not round-trip");
    rejects("[translation]\nsubtitles = 2");
    const auto skipped = parse_settings("[game]\nskip_intro_logos = 1\n");
    require(skipped.skip_intro_logos && parse_settings(serialize_settings(skipped)) == skipped,
            "intro logo option does not round-trip");
    rejects("[game]\nskip_intro_logos = 2");
    rejects("[game]\nskip_intro_logos = yes");
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
        "[graphics]\r\ninternal_resolution = 4\r\nwindow_width = 1920\nwindow_height = 1080\nfullscreen = 1\nframe_rate = 30\nfov_degrees = 75\ndraw_distance = 2.5\n"
        "[input]\ndead_zone = 0.3\n[keyboard]\ncircle = C, V\nquit = none\nfast_forward = F\n"
        "[gamepad]\nanalog_left = axis:rightx+\nanalog_right = axis:rightx-\nl = axis:righttrigger+\n");
    require(custom.internal_resolution == 4 && custom.frame_rate == 30 && custom.fov_degrees == 75 && custom.draw_distance == 2.5f &&
                custom.window_width == 1920 && custom.window_height == 1080 &&
                custom.fullscreen,
            "graphics settings not parsed");
    require(parse_settings(serialize_settings(custom)) == custom, "custom bindings do not round-trip");
    // Earlier versions' files: interpolation became frame_rate = 60, the game update rate is gone.
    require(parse_settings("[graphics]\nframe_rate = 0\nexperimental_game_frame_rate = 60\nframe_interpolation = 1").frame_rate == 60,
            "old interpolation not carried over");
    require(parse_settings("[graphics]\nframe_interpolation = 0\nframe_rate = 60").frame_rate == 0,
            "old cap of 60 without interpolation turned interpolation on");
    require(parse_settings("[graphics]\nframe_rate = 30\nframe_interpolation = 0").frame_rate == 30, "old cap of 30 lost");
    // Earlier versions' quit action: the pause menu took its place and its key.
    {
        constexpr auto menu = static_cast<std::size_t>(Action::Menu);
        const auto key = [](const char *name) {
            return std::vector{Binding{Binding::Kind::Key, static_cast<int>(SDL_GetScancodeFromName(name))}};
        };
        const Binding l3_r3{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_LEFT_STICK, 1, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
        require(defaults.controls.keyboard[menu] == key("Escape") &&
                    defaults.controls.gamepad[menu] ==
                        std::vector{Binding{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_GUIDE}, l3_r3},
                "default menu bindings changed");
        // Button chords: either order, written lower button first, and round-tripped.
        const auto chord = parse_settings("[gamepad]\nmenu = rightstick + leftstick");
        require(chord.controls.gamepad[menu] == std::vector{l3_r3}, "a button chord not parsed");
        require(serialize_settings(defaults).find("\nmenu = guide, leftstick+rightstick") != std::string::npos,
                "a button chord not written");
        require(parse_settings(serialize_settings(chord)) == chord, "a button chord does not round-trip");
        rejects("[gamepad]\nmenu = leftstick+leftstick");
        rejects("[gamepad]\nmenu = leftstick+");
        rejects("[gamepad]\nmenu = leftstick+nothing");
        rejects("[keyboard]\nmenu = A+B");
        require(parse_settings("[keyboard]\nquit = F10").controls.keyboard[menu] == key("F10"), "old quit key not carried over");
        require(parse_settings("[keyboard]\nquit = F10\nmenu = M").controls.keyboard[menu] == key("M") &&
                    parse_settings("[keyboard]\nmenu = M\nquit = F10").controls.keyboard[menu] == key("M"),
                "old quit key replaced an explicit menu key");
        require(parse_settings("[gamepad]\nquit = none").controls.gamepad[menu] == defaults.controls.gamepad[menu],
                "an unbound old quit unbound the menu");
        require(parse_settings("[gamepad]\nquit = back").controls.gamepad[menu] ==
                    std::vector{Binding{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_BACK}},
                "old quit button not carried over");
        const auto written = serialize_settings(defaults);
        require(written.find("\nquit =") == std::string::npos && written.find("\nmenu = Escape") != std::string::npos,
                "the menu action is not written in place of quit");
        rejects("[keyboard]\nquit = A\nquit = B");
    }
    require(serialize_settings(default_settings()).find("frame_interpolation") == std::string::npos &&
                serialize_settings(default_settings()).find("experimental_game_frame_rate") == std::string::npos,
            "removed settings still written");
    for (const auto text : {
        "[graphics]\ninternal_resolution = 0", "[graphics]\nframe_rate = 61",
        "[graphics]\nframe_rate = 30junk", "[graphics]\nframe_interpolation = 2", "[graphics]\nwindow_width = 100",
        "[graphics]\nwindow_height = 99999", "[graphics]\nfullscreen = yes", "[graphics]\nfov_degrees = 10",
        "[graphics]\nfov_degrees = nan", "[graphics]\ndraw_distance = 0.5", "[graphics]\ndraw_distance = 9", "[graphics]\nframe_rate = 30\nframe_rate = 60",
        "[input]\ndead_zone = 1", "[input]\ndead_zone = -0.1", "[input]\nunknown = 1",
        "[keyboard]\ncircle = NotAKey", "[keyboard]\nunknown = C", "[keyboard]\ncircle = C,",
        "[gamepad]\nl = axis:leftx", "[gamepad]\nl = axis:unknown+", "[gamepad]\nl = unknown",
        "[keyboard]\ncircle = scancode:9999", "[future]\nvalue = 1", "frame_rate = 30",
        "[graphics]\nframe_rate", "[keyboard]\ncircle = "}) rejects(text);

    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    std::array<GamepadState, 2> pads{};
    auto state = map_input(defaults.controls, keys, pads);
    require(state.pad.buttons == 0 && state.pad.lx == 128 && state.pad.ly == 128 && !state.menu,
            "neutral controls not centered");
    keys[SDL_SCANCODE_F5]=true;
    state=map_input(defaults.controls,keys,pads);
    require(state.save_state && !state.load_state && state.pad.buttons==0,"F5 did not save alone");
    keys={}; keys[SDL_SCANCODE_F8]=true;
    state=map_input(defaults.controls,keys,pads);
    require(state.load_state && !state.save_state,"F8 did not load alone");
    keys={}; pads[0].buttons[SDL_GAMEPAD_BUTTON_BACK]=true;
    pads[0].buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]=true;
    state=map_input(defaults.controls,keys,pads);
    require(state.load_state && !state.save_state && state.pad.buttons==0,"Select+L1 leaked guest buttons or did not load");
    pads[0].buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]=false;
    pads[0].buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER]=true;
    state=map_input(defaults.controls,keys,pads);
    require(state.save_state && !state.load_state && state.pad.buttons==0,"Select+R1 leaked guest buttons or did not save");
    // A chord requires both buttons on the same controller.
    pads={}; pads[0].buttons[SDL_GAMEPAD_BUTTON_BACK]=true; pads[1].buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER]=true;
    state=map_input(defaults.controls,keys,pads);
    require(!state.save_state && !state.load_state,"chord combined different controllers");
    const auto remapped=parse_settings("[keyboard]\nsave_state = F6\nload_state = F7\n[gamepad]\nsave_state = back+leftstick\nload_state = back+rightstick\n");
    require(parse_settings(serialize_settings(remapped))==remapped,"state bindings did not round trip");
    const auto legacy=parse_settings("[keyboard]\ncross = C\n");
    require(legacy.controls.gamepad[static_cast<std::size_t>(Action::SaveState)]==defaults.controls.gamepad[static_cast<std::size_t>(Action::SaveState)],"legacy file lost new state defaults");
    pads={};
    keys[SDL_SCANCODE_Z] = keys[SDL_SCANCODE_J] = keys[SDL_SCANCODE_I] = true;
    state = map_input(defaults.controls, keys, pads);
    require(state.pad.buttons == kButtonCircle && state.pad.lx == 1 && state.pad.ly == 1, "default keyboard bindings changed");
    keys.fill(false);
    keys[SDL_SCANCODE_Z] = true;
    require(map_input(custom.controls, keys, pads).pad.buttons == 0, "old keyboard binding still active");
    keys[SDL_SCANCODE_V] = keys[SDL_SCANCODE_F] = keys[SDL_SCANCODE_ESCAPE] = true;
    state = map_input(custom.controls, keys, pads);
    // quit = none from an earlier version leaves the pause menu on Escape.
    require(state.pad.buttons == kButtonCircle && state.fast_forward && state.menu, "alternatives/legacy quit failed");
    keys.fill(false);
    pads[0].buttons[SDL_GAMEPAD_BUTTON_SOUTH] = true;
    pads[1].buttons[SDL_GAMEPAD_BUTTON_START] = true;
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 20000;
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFTX] = -32768;
    pads[0].axes[SDL_GAMEPAD_AXIS_LEFTY] = 32767;
    state = map_input(defaults.controls, keys, pads);
    require(state.pad.buttons == (kButtonCross | kButtonStart | kButtonL) && state.pad.lx == 1 && state.pad.ly == 255,
            "multi-pad buttons, triggers or axis extremes failed");
    pads = {}; keys.fill(false);
    // L3+R3 opens the menu without also resetting the camera (R3 alone);
    // R3 alone still resets it, and a chord on one pad and half on another is no chord.
    pads[0].buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = pads[0].buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = true;
    state = map_input(defaults.controls, keys, pads);
    require(state.menu && !state.camera_reset, "L3+R3 did not open the menu alone");
    pads[0].buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = false;
    state = map_input(defaults.controls, keys, pads);
    require(!state.menu && state.camera_reset, "R3 alone did not reset the camera");
    pads[1].buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = true;
    require(!map_input(defaults.controls, keys, pads).menu, "a chord split across pads opened the menu");
    pads = {};
    pads[0].axes[SDL_GAMEPAD_AXIS_RIGHTX] = 6000;
    require(map_input(defaults.controls, keys, pads).camera_x == 0.0f, "camera stick drift escaped dead zone");
    pads[0].axes[SDL_GAMEPAD_AXIS_RIGHTX] = 19660;
    pads[0].axes[SDL_GAMEPAD_AXIS_RIGHTY] = -32768;
    pads[0].buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = true;
    state = map_input(defaults.controls, keys, pads);
    require(std::abs(state.camera_x - 0.5f) < 0.001f && state.camera_y == -1 && state.camera_reset &&
                state.pad.buttons == 0 && state.pad.lx == 128 && state.pad.ly == 128,
            "camera stick was not proportional or changed the PSP pad");
    pads = {}; keys[SDL_SCANCODE_KP_4] = true; keys[SDL_SCANCODE_KP_8] = true;
    state = map_input(defaults.controls, keys, pads);
    require(state.camera_x == -1 && state.camera_y == -1 && state.pad.buttons == 0,
            "default camera keyboard mapping failed");
    const auto camera_keys = parse_settings("[keyboard]\ncamera_left = U\ncamera_reset = O\n");
    require(parse_settings(serialize_settings(camera_keys)) == camera_keys, "camera bindings did not round-trip");
    keys.fill(false); keys[SDL_SCANCODE_U] = true; keys[SDL_SCANCODE_O] = true;
    state = map_input(camera_keys.controls, keys, pads);
    require(state.camera_x == -1 && state.camera_reset, "camera remapping failed");
    keys.fill(false);
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
