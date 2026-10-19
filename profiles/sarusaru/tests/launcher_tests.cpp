// The launcher's logic without a window: its settings rows, the file
// browser's listings, and recognising the player's disc image (with invented
// ISO images; no game data).
#include "game_disc.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/sha256.hpp"
#include "synthetic_iso.hpp"
#include "ui/bindings.hpp"
#include "ui/settings_rows.hpp"
#include "ui/text.hpp"

#include <set>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

using sarusaru::test::kSectorSize;
using sarusaru::test::record;

// An invented 26-sector disc: /UMD_DATA.BIN naming `id`, and
// /PSP_GAME/SYSDIR/BOOT.BIN holding `boot`.
std::vector<std::uint8_t> disc_image(std::string_view umd_data, const std::vector<std::uint8_t> &boot) {
    constexpr std::uint32_t sectors = 26u;
    std::vector<std::uint8_t> image(sectors * kSectorSize, 0u);
    const std::size_t pvd = 16u * kSectorSize;
    image[pvd] = 1u;
    for (std::size_t i = 0; i < 5u; ++i) image[pvd + 1u + i] = static_cast<std::uint8_t>("CD001"[i]);
    image[pvd + 6u] = 1u;
    sarusaru::test::both_endian(image, pvd + 80u, sectors, 4u);
    sarusaru::test::both_endian(image, pvd + 128u, kSectorSize, 2u);
    const std::string_view self("\0", 1), parent("\1", 1);
    record(image, pvd + 156u, self, 18u, kSectorSize, true);
    auto offset = record(image, 18u * kSectorSize, self, 18u, kSectorSize, true);
    offset = record(image, offset, parent, 18u, kSectorSize, true);
    offset = record(image, offset, "PSP_GAME", 19u, kSectorSize, true);
    record(image, offset, "UMD_DATA.BIN;1", 21u, static_cast<std::uint32_t>(umd_data.size()), false);
    offset = record(image, 19u * kSectorSize, self, 19u, kSectorSize, true);
    offset = record(image, offset, parent, 18u, kSectorSize, true);
    record(image, offset, "SYSDIR", 20u, kSectorSize, true);
    offset = record(image, 20u * kSectorSize, self, 20u, kSectorSize, true);
    offset = record(image, offset, parent, 19u, kSectorSize, true);
    record(image, offset, "BOOT.BIN;1", 22u, static_cast<std::uint32_t>(boot.size()), false);
    std::copy(umd_data.begin(), umd_data.end(), image.begin() + 21 * kSectorSize);
    std::copy(boot.begin(), boot.end(), image.begin() + 22 * kSectorSize);
    return image;
}

// Every text exists in both languages with the same placeholders, and
// formatting fills them in.
void texts() {
    using namespace sarusaru::ui;
    using sarusaru::shell::Language;
    const auto placeholders = [](std::string_view text) {
        std::multiset<char> found;
        for (std::size_t i = 0; i + 2 < text.size(); ++i)
            if (text[i] == '{' && text[i + 2] == '}') found.insert(text[i + 1]);
        return found;
    };
    for (int i = 0; i < static_cast<int>(Text::Count); ++i) {
        const auto id = static_cast<Text>(i);
        const auto english = tr(id, Language::English), japanese = tr(id, Language::Japanese);
        require(!english.empty() && !japanese.empty(), "a text is missing in a language");
        require(placeholders(english) == placeholders(japanese), "a translation's placeholders differ");
    }
    set_language(Language::Japanese);
    require(format(Text::DiscOtherGame, {"ULUS12345", "UCJS10060"}) ==
                "\u5225\u306E PSP \u30B2\u30FC\u30E0\uFF08ULUS12345\uFF09\u3067\u3059\u3002"
                "\u5BFE\u5FDC\u3057\u3066\u3044\u308B\u306E\u306F UCJS10060 \u3067\u3059\u3002",
            "Japanese text not formatted");
    require(tr(Text::Play) == "\u3042\u305D\u3076", "Japanese not selected");
    set_language(Language::Auto);
    require(language() == Language::English, "Auto must be resolved before it is set");
    require(resolve_language(Language::Japanese) == Language::Japanese &&
                resolve_language(Language::English) == Language::English,
            "explicit language not kept");
    set_language(Language::English);
    require(format(Text::StatusChoose, {"UCJS10060"}) == "Locate your disc image of UCJS10060 to play",
            "English text not formatted");
}

void rows() {
    using namespace sarusaru::ui;
    set_language(sarusaru::shell::Language::English);
    using sarusaru::shell::Settings;
    // The stepping rule: wraps at both ends, and an off-step value moves to
    // the nearest step in the direction pressed.
    constexpr double steps[] = {0, 15, 20, 30, 60};
    require(step_value(steps, 30, 1) == 60 && step_value(steps, 60, 1) == 0, "forward step or wrap wrong");
    require(step_value(steps, 0, -1) == 60 && step_value(steps, 20, -1) == 15, "backward step or wrap wrong");
    require(step_value(steps, 25, 1) == 30 && step_value(steps, 25, -1) == 20, "off-step value not snapped");
    require(step_value(steps, 75, 1) == 0 && step_value(steps, -5, -1) == 60, "out-of-range value not wrapped");

    Settings settings = sarusaru::shell::default_settings();
    settings.internal_resolution = 8;
    step(SettingRow::InternalResolution, settings, 1);
    require(settings.internal_resolution == 1, "resolution did not wrap up");
    step(SettingRow::InternalResolution, settings, -1);
    require(settings.internal_resolution == 8, "resolution did not wrap down");
    require(describe(SettingRow::InternalResolution, settings).value == "8× (3840×2176)", "resolution label wrong");

    // Textures: the choice comes from the pack directory's name; the launcher
    // makes or finds the pack, and stepping to the original clears it.
    require(texture_scale(settings) == 0u && describe(SettingRow::Textures, settings).value == "Original",
            "original textures not the default");
    settings.texture_pack = "/data/SaruSaruRecomp/textures/4x-0123456789abcdef";
    require(texture_scale(settings) == 4u && describe(SettingRow::Textures, settings).value == "HD (4×)", "HD pack not named");
    settings.texture_pack = "/data/SaruSaruRecomp/textures/8x-0123456789abcdef/";
    require(texture_scale(settings) == 8u && describe(SettingRow::Textures, settings).value == "4K (8×)", "4K pack not named");
    require(next_texture_scale(texture_scale(settings), 1) == 0u && next_texture_scale(0u, 1) == 4u &&
                next_texture_scale(0u, -1) == 8u && next_texture_scale(std::nullopt, 1) == 4u, "texture choices do not cycle");
    step(SettingRow::Textures, settings, 1);
    require(settings.texture_pack.empty(), "stepping to the original kept the pack");
    settings.texture_pack = "my-pack";
    require(!texture_scale(settings) && describe(SettingRow::Textures, settings).value == "Custom pack", "custom pack not named");
    step(SettingRow::Textures, settings, 1);
    require(settings.texture_pack == "my-pack", "stepping towards HD changed the pack without the launcher");
    settings.texture_pack.clear();

    require(!settings.cel_shading && describe(SettingRow::CelShading, settings).value == "Off", "cel shading not off by default");
    step(SettingRow::CelShading, settings, 1);
    require(settings.cel_shading == 1 && describe(SettingRow::CelShading, settings).value == "Banded", "cel shading did not turn on");
    step(SettingRow::CelShading, settings, -1);
    require(!settings.cel_shading, "cel shading did not turn off");

    step(SettingRow::CelShading, settings, -1);
    require(settings.cel_shading == 2 && describe(SettingRow::CelShading, settings).value == "Outlined" &&
                describe(SettingRow::CelShading, settings).experimental,
            "cel preset did not wrap backwards to an experimental Outlined");
    step(SettingRow::CelShading, settings, 1);
    require(settings.cel_shading == 0, "outlined preset did not wrap to off");
    step(SettingRow::CelShading, settings, 1);
    step(SettingRow::CelShading, settings, 1);
    require(settings.cel_shading == 2, "banded preset did not step to outlined");

    settings.fov_degrees = 52.5f;
    step(SettingRow::Fov, settings, 1);
    require(settings.fov_degrees == 55.0f, "hand-set FOV not snapped to the next step");
    settings.fov_degrees = 120.0f;
    step(SettingRow::Fov, settings, 1);
    require(settings.fov_degrees == 0.0f && describe(SettingRow::Fov, settings).value.starts_with("Native"),
            "FOV did not wrap to native");
    step(SettingRow::Fov, settings, 1);
    require(settings.fov_degrees == 30.0f, "FOV after native is not 30");

    require(describe(SettingRow::DrawDistance, settings).value == "8\u00D7", "draw distance must start at 8x (Modern)");
    settings.draw_distance = 1.0f;
    require(describe(SettingRow::DrawDistance, settings).value == "Native", "native draw distance not named");
    step(SettingRow::DrawDistance, settings, 1);
    require(settings.draw_distance == 1.5f && describe(SettingRow::DrawDistance, settings).value == "1.5\u00D7",
            "draw distance after native is not 1.5x");
    step(SettingRow::DrawDistance, settings, -1);
    step(SettingRow::DrawDistance, settings, -1);
    require(settings.draw_distance == 8.0f, "draw distance did not wrap to 8x");

    settings.window_width = 1000;
    step(SettingRow::WindowSize, settings, 1);
    require(settings.window_width == 1440 && settings.window_height == 816, "window size step wrong");
    require(describe(SettingRow::WindowSize, settings).enabled, "window size disabled in a window");
    settings.fullscreen = true;
    step(SettingRow::WindowSize, settings, 1);
    require(!describe(SettingRow::WindowSize, settings).enabled && settings.window_width == 1440,
            "window size still steps while fullscreen");
    settings.fullscreen = false;

    settings.controls.dead_zone = 0.2f;
    settings.controls.aim_sensitivity = 1.0f;
    step(SettingRow::AimSensitivity, settings, -1);
    require(settings.controls.aim_sensitivity == 0.75f && describe(SettingRow::AimSensitivity, settings).value == "75%" &&
        !restart_only(SettingRow::AimSensitivity), "aim sensitivity row updates and applies in game");
    step(SettingRow::DeadZone, settings, 1);
    require(std::abs(settings.controls.dead_zone - 0.25f) < 1e-6f && describe(SettingRow::DeadZone, settings).value == "25%",
            "dead zone step wrong");

    // Frame rate: native, then 60 and 120 FPS motion, both experimental.
    settings.frame_rate = 0;
    require(describe(SettingRow::FrameRate, settings).value == "Native" && !interpolates(settings) &&
                !describe(SettingRow::FrameRate, settings).experimental,
            "frame rate not native, or native marked experimental");
    step(SettingRow::FrameRate, settings, 1);
    require(settings.frame_rate == 60 && interpolates(settings) && interpolated_pictures(settings) == 1u &&
                describe(SettingRow::FrameRate, settings).value == "60 FPS" &&
                describe(SettingRow::FrameRate, settings).experimental,
            "60 FPS after native is not experimental 60 FPS motion");
    step(SettingRow::FrameRate, settings, 1);
    require(settings.frame_rate == 120 && interpolated_pictures(settings) == 3u &&
                describe(SettingRow::FrameRate, settings).value == "120 FPS" &&
                describe(SettingRow::FrameRate, settings).experimental,
            "120 FPS after 60 is not experimental 120 FPS motion");
    step(SettingRow::FrameRate, settings, 1);
    require(settings.frame_rate == 0, "frame rate did not wrap to native");
    step(SettingRow::FrameRate, settings, -1);
    require(settings.frame_rate == 120, "frame rate did not wrap back to 120");
    settings.frame_rate = 30;
    require(!interpolates(settings), "an earlier cap of 30 interpolates");
    step(SettingRow::FrameRate, settings, 1);
    require(settings.frame_rate == 60, "an earlier cap of 30 did not step up to 60");
    settings.frame_rate = 20;
    step(SettingRow::FrameRate, settings, -1);
    require(settings.frame_rate == 0, "hand-set frame rate not snapped down to native");

    require(describe(SettingRow::ControllerButtons, settings).value == "Auto", "controller buttons row must default to Auto");
    constexpr const char *prompt_labels[] = {"Xbox", "Switch", "PlayStation", "Keyboard", "Auto"};
    for (const auto *label : prompt_labels) {
        step(SettingRow::ControllerButtons, settings, 1);
        require(describe(SettingRow::ControllerButtons, settings).value == label, "prompt style row order wrong");
    }
    step(SettingRow::ControllerButtons, settings, -1);
    require(settings.prompt_style == sarusaru::shell::PromptStyle::Keyboard && !restart_only(SettingRow::ControllerButtons),
            "prompt style must wrap backwards and apply live");
    settings.prompt_style = sarusaru::shell::PromptStyle::Auto;
    settings.confirm_south = true;
    require(describe(SettingRow::ConfirmButton, settings).face == SDL_GAMEPAD_BUTTON_SOUTH &&
                describe(SettingRow::ConfirmButton, settings).value == "South",
            "confirm row does not show the south button");
    step(SettingRow::ConfirmButton, settings, 1);
    require(describe(SettingRow::ConfirmButton, settings).face == SDL_GAMEPAD_BUTTON_EAST, "confirm row glyph did not follow");
    require(describe(SettingRow::Language, settings).face == SDL_GAMEPAD_BUTTON_INVALID, "a row without a button shows one");
    settings.confirm_south = true;

    require(describe(SettingRow::GameText, settings).value == "Japanese (original)", "game text not Japanese by default");
    step(SettingRow::GameText, settings, 1);
    require(settings.translation_language == "en" && describe(SettingRow::GameText, settings).value == "English (unofficial)",
            "game text row did not step to English");
    step(SettingRow::GameText, settings, -1);
    require(settings.translation_language == "ja", "game text row did not step back to Japanese");

    settings.language = sarusaru::shell::Language::Auto;
    step(SettingRow::Language, settings, 1);
    require(settings.language == sarusaru::shell::Language::English &&
                describe(SettingRow::Language, settings).value == "English",
            "language row did not step");
    step(SettingRow::Language, settings, 1);
    require(describe(SettingRow::Language, settings).value == "\u65E5\u672C\u8A9E", "Japanese not named in Japanese");
    step(SettingRow::Language, settings, 1);
    require(settings.language == sarusaru::shell::Language::Auto &&
                describe(SettingRow::Language, settings).value.starts_with("Automatic ("),
            "language row did not wrap to automatic");
    step(SettingRow::Language, settings, -1);
    require(settings.language == sarusaru::shell::Language::Japanese, "language row did not step back");

    step(SettingRow::ConfirmButton, settings, -1);
    require(!settings.confirm_south, "confirm button did not toggle");

    // Every row is on exactly one tab, and describes itself.
    int seen = 0;
    for (const auto &tab : settings_tabs())
        for (const auto row : tab.rows) {
            ++seen;
            const auto view = describe(row, settings);
            require(!view.label.empty() && !view.value.empty() && !view.description.empty(), "row without text");
            // In the pause menu, only the rows the game applies as it starts say they wait for it.
            const auto in_game = describe(row, settings, true);
            const std::string_view note = tr(Text::AppliesNextStart);
            require(in_game.value == view.value && in_game.label == view.label, "pause menu row reads differently");
            require(in_game.description.ends_with(note) == restart_only(row),
                    "a row in the pause menu says it waits for the next start, or does not and should");
            require(!view.description.ends_with(note), "a launcher row says it waits for the next start");
        }
    require(restart_only(SettingRow::GameText) && restart_only(SettingRow::SkipIntroLogos) &&
                !restart_only(SettingRow::InternalResolution) && !restart_only(SettingRow::Textures) &&
                !restart_only(SettingRow::FrameRate) && !restart_only(SettingRow::Fov) &&
                !restart_only(SettingRow::DrawDistance),
            "rows applied live or at the next start changed");
    settings.skip_intro_logos = false;
    require(describe(SettingRow::SkipIntroLogos, settings).value == "Off", "logo skip off not named");
    step(SettingRow::SkipIntroLogos, settings, 1);
    require(settings.skip_intro_logos && describe(SettingRow::SkipIntroLogos, settings).value == "On",
            "logo skip did not turn on");
    step(SettingRow::SkipIntroLogos, settings, -1);
    require(!settings.skip_intro_logos, "logo skip did not turn off");
    require(!restart_only(SettingRow::Subtitles) && describe(SettingRow::Subtitles, settings).value == "On",
            "subtitles must start on and apply live");
    step(SettingRow::Subtitles, settings, 1);
    require(!settings.subtitles && describe(SettingRow::Subtitles, settings).value == "Off", "subtitles did not turn off");
    step(SettingRow::Subtitles, settings, 1);
    require(seen == 18, "settings rows missing from the tabs");
    const auto graphics = settings_tabs().front().rows;
    require(settings_tabs().size() == 4u && graphics.size() == 9u && graphics[0] == SettingRow::Preset &&
                graphics[1] == SettingRow::Fullscreen && graphics[2] == SettingRow::WindowSize &&
                graphics[6] == SettingRow::DrawDistance && graphics[7] == SettingRow::CelShading &&
                graphics[8] == SettingRow::Textures,
            "Graphics rows not in order: preset, then full screen and window size, cel shading and textures last");
}

// Native, Modern Low and Modern set their rows together; anything else reads Custom.
void presets() {
    using namespace sarusaru::ui;
    using sarusaru::shell::Settings;
    Settings settings = sarusaru::shell::default_settings();
    // The defaults are Modern without its downloads: Modern once a build offers neither.
    require(preset_of(settings) == Preset::Custom, "the defaults are Modern before its downloads");
    set_offered_downloads(false, false);
    require(preset_of(settings) == Preset::Modern, "the defaults are not Modern when nothing is offered to download");
    set_offered_downloads(true, false);
    settings.translation_language = "en";
    require(preset_of(settings) == Preset::Modern, "Modern without an HD offer still asks for HD textures");
    settings.texture_pack = "/data/textures/4x-0123456789abcdef";
    require(preset_of(settings) == Preset::Custom, "an HD pack the build does not offer reads as Modern");
    set_offered_downloads(true, true);
    settings = sarusaru::shell::default_settings();
    require(next_preset(Preset::Custom, 1) == Preset::Native && next_preset(Preset::Custom, -1) == Preset::Modern &&
                next_preset(Preset::Native, 1) == Preset::ModernLow && next_preset(Preset::ModernLow, 1) == Preset::Modern &&
                next_preset(Preset::Modern, 1) == Preset::Native && next_preset(Preset::Native, -1) == Preset::Modern &&
                next_preset(Preset::Modern, -1) == Preset::ModernLow,
            "presets do not cycle Native, Modern Low, Modern");

    settings.texture_pack = "/data/textures/8x-0123456789abcdef";
    settings.translation_language = "en";
    settings.translation_pack = "/data/translations/english.sstp";
    step(SettingRow::Preset, settings, 1);
    require(preset_of(settings) == Preset::Native && settings.internal_resolution == 1u && settings.texture_pack.empty() &&
                settings.translation_language == "ja" && settings.draw_distance == 1.0f && !settings.skip_intro_logos &&
                settings.fov_degrees == 0.0f && settings.frame_rate == 0u && settings.controls.aim_sensitivity == 1.0f,
            "Native did not set its rows");
    require(settings.translation_pack == "/data/translations/english.sstp", "Native forgot the downloaded English pack");
    require(describe(SettingRow::Preset, settings).value == "Native", "Native not named");

    // Modern Low: 1080p, 45 degrees, twice the draw distance, the native frame rate and 150% aiming.
    step(SettingRow::Preset, settings, 1);
    require(settings.internal_resolution == kModernResolution && settings.draw_distance == 2.0f &&
                settings.fov_degrees == 45.0f && settings.frame_rate == 0u && settings.skip_intro_logos &&
                settings.controls.aim_sensitivity == 1.5f && settings.translation_language == "ja" &&
                settings.texture_pack.empty(),
            "Modern Low did not set its rows without downloads");
    require(preset_of(settings) == Preset::Custom, "Modern Low without English and HD is not Custom");
    settings.translation_language = "en";
    settings.texture_pack = "/data/textures/4x-prebuilt-0123456789abcdef";
    require(preset_of(settings) == Preset::ModernLow && describe(SettingRow::Preset, settings).value == "Modern Low",
            "Modern Low with English and HD not recognised");
    step(SettingRow::Fov, settings, 1);
    require(preset_of(settings) == Preset::Custom, "a wider FOV still reads as Modern Low");
    step(SettingRow::Fov, settings, -1);
    step(SettingRow::FrameRate, settings, 1);
    require(preset_of(settings) == Preset::Custom, "60 FPS still reads as Modern Low");
    step(SettingRow::FrameRate, settings, -1);
    require(preset_of(settings) == Preset::ModernLow, "Modern Low not recognised again");
    {
        Settings wide = settings;
        wide.fov_degrees = 75.0f;
        wide.frame_rate = 120u;
        apply_preset(wide, Preset::ModernLow);
        require(wide.fov_degrees == 45.0f && wide.frame_rate == 0u, "Modern Low kept another FOV or frame rate");
    }

    // Modern's own step sets what needs no download; the launcher adds English and HD.
    step(SettingRow::Preset, settings, 1);
    require(preset_of(settings) == Preset::Modern, "Modern does not follow Modern Low");
    settings = sarusaru::shell::default_settings();
    apply_preset(settings, Preset::Native);
    apply_preset(settings, Preset::Modern);
    require(settings.internal_resolution == kModernResolution && settings.draw_distance == kModernDrawDistance &&
                settings.controls.aim_sensitivity == 1.5f &&
                settings.skip_intro_logos && settings.translation_language == "ja" && settings.texture_pack.empty(),
            "Modern did not set its rows without downloads");
    require(describe(SettingRow::InternalResolution, settings).value == "4× (1920×1088)", "Modern is not 1080p");
    require(preset_of(settings) == Preset::Custom, "Modern without English and HD is not Custom");
    settings.translation_language = "en";
    settings.texture_pack = "/data/textures/4x-prebuilt-0123456789abcdef";
    require(preset_of(settings) == Preset::Modern && describe(SettingRow::Preset, settings).value == "Modern",
            "Modern with the prebuilt HD pack not recognised");
    settings.texture_pack = "/data/textures/4x-0123456789abcdef";
    require(preset_of(settings) == Preset::Modern, "Modern with an HD pack made on the device not recognised");

    // Changing any one of its rows makes it Custom.
    step(SettingRow::DrawDistance, settings, -1);
    require(preset_of(settings) == Preset::Custom && describe(SettingRow::Preset, settings).value == "Custom",
            "a changed row did not make the preset Custom");
    step(SettingRow::DrawDistance, settings, 1);
    step(SettingRow::AimSensitivity, settings, 1);
    require(preset_of(settings) == Preset::Custom, "a changed aiming sensitivity did not make Modern Custom");
    step(SettingRow::AimSensitivity, settings, -1);
    // Rows outside the preset leave it alone.
    step(SettingRow::Fov, settings, 1);
    step(SettingRow::FrameRate, settings, 1);
    require(preset_of(settings) == Preset::Modern, "rows outside the preset changed it");
}

void bindings() {
    using namespace sarusaru::ui;
    using sarusaru::shell::Action;
    using sarusaru::shell::Binding;
    const Binding key_z{Binding::Kind::Key, SDL_SCANCODE_Z};
    {
        const Binding l3_r3{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_LEFT_STICK, 1, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
        const auto ps = glyph(l3_r3, SDL_GAMEPAD_TYPE_PS5), xbox = glyph(l3_r3, SDL_GAMEPAD_TYPE_XBOXONE);
        require(ps.kind == Glyph::Kind::Pill && ps.label == "L3+R3" && xbox.label == "LS+RS", "a button chord's glyph is wrong");
    }
    const Binding key_c{Binding::Kind::Key, SDL_SCANCODE_C};
    const Binding east{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_EAST};

    // Editing one cell leaves everything else alone.
    auto controls = sarusaru::shell::default_settings().controls;
    const auto defaults = controls;
    set_binding(controls, BindingDevice::Keyboard, Action::Cross, key_c);
    require(bindings_of(controls, BindingDevice::Keyboard, Action::Cross) == std::vector{key_c}, "set did not replace");
    add_binding(controls, BindingDevice::Keyboard, Action::Cross, key_z);
    add_binding(controls, BindingDevice::Keyboard, Action::Cross, key_z);
    require(bindings_of(controls, BindingDevice::Keyboard, Action::Cross) == std::vector{key_c, key_z},
            "add did not append once");
    require(sharing(controls, BindingDevice::Keyboard, Action::Cross, key_z) == std::vector{Action::Circle},
            "shared input not found (Z is circle's by default)");
    clear_bindings(controls, BindingDevice::Gamepad, Action::Circle);
    require(bindings_of(controls, BindingDevice::Gamepad, Action::Circle).empty(), "clear left inputs");
    controls.dead_zone = 0.4f;
    reset_bindings(controls, BindingDevice::Keyboard);
    require(controls.keyboard == defaults.keyboard && controls.gamepad != defaults.gamepad && controls.dead_zone == 0.4f,
            "reset touched the other device or the dead zone");

    // Glyphs: keys by short name, face buttons by position, the rest named
    // as each pad family names them.
    require(glyph({Binding::Kind::Key, SDL_SCANCODE_RETURN}, SDL_GAMEPAD_TYPE_STANDARD).label == "Enter",
            "key name not shortened");
    const auto face = glyph(east, SDL_GAMEPAD_TYPE_PS5);
    require(face.kind == Glyph::Kind::Face && face.button == SDL_GAMEPAD_BUTTON_EAST, "face button not drawn by position");
    const Binding shoulder{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    require(glyph(shoulder, SDL_GAMEPAD_TYPE_PS4).label == "L1" && glyph(shoulder, SDL_GAMEPAD_TYPE_XBOXONE).label == "LB",
            "shoulder not named per family");
    require(glyph({Binding::Kind::Axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1}, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO).label == "ZL",
            "trigger not named per family");
    require(glyph({Binding::Kind::Axis, SDL_GAMEPAD_AXIS_LEFTX, -1}, SDL_GAMEPAD_TYPE_PS5).label == "LS\u2190",
            "stick direction not named");

    // Keyboard capture: the next key, repeats ignored, Esc cancels.
    BindingCapture capture;
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.button(SDL_GAMEPAD_BUTTON_BACK,true,0.1);
    capture.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true,0.2);
    capture.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false,0.3);
    require(capture.take()==Binding{Binding::Kind::Button,SDL_GAMEPAD_BUTTON_BACK,1,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},"state chord was not captured");
    capture.start(BindingDevice::Keyboard, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.key(SDL_SCANCODE_RETURN, true);
    require(capture.active(), "key repeat captured");
    capture.key(SDL_SCANCODE_C, false);
    require(capture.outcome() == BindingCapture::Outcome::Bound && capture.take() == key_c && !capture.active(),
            "key not captured");
    capture.start(BindingDevice::Keyboard, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.key(SDL_SCANCODE_ESCAPE, false);
    require(capture.outcome() == BindingCapture::Outcome::Cancelled && !capture.take(), "Esc did not cancel");
    capture.start(BindingDevice::Keyboard, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.update(100.0);
    require(capture.active(), "keyboard capture timed out");
    capture.pointer();
    require(capture.outcome() == BindingCapture::Outcome::Cancelled, "a click did not cancel");
    (void)capture.take();

    // Gamepad: binds on release, so the back button binds with a short press.
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.button(SDL_GAMEPAD_BUTTON_SOUTH, false, 0.1);  // the press that opened it, let go
    require(capture.active(), "release of the opening press captured");
    capture.key(SDL_SCANCODE_Z, false);
    require(capture.active(), "a key captured for a gamepad");
    capture.button(SDL_GAMEPAD_BUTTON_EAST, true, 0.2);
    capture.update(0.5);
    capture.button(SDL_GAMEPAD_BUTTON_EAST, false, 0.6);
    require(capture.take() == east, "short press of the back button not bound");
    // ...and holding it cancels.
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.button(SDL_GAMEPAD_BUTTON_EAST, true, 1.0);
    require(capture.cancel_progress(1.5) > 0.49f && capture.cancel_progress(1.5) < 0.51f, "hold progress wrong");
    capture.update(2.05);
    require(capture.outcome() == BindingCapture::Outcome::Cancelled, "holding back did not cancel");
    (void)capture.take();
    // A long hold of another button still binds it.
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.button(SDL_GAMEPAD_BUTTON_NORTH, true, 0.5);
    capture.update(9.0);
    require(capture.active() && capture.pressing(), "timed out while a button was held");
    capture.button(SDL_GAMEPAD_BUTTON_NORTH, false, 9.5);
    require(capture.take() == Binding{Binding::Kind::Button, SDL_GAMEPAD_BUTTON_NORTH}, "long hold not bound");
    // Nothing pressed: it gives up.
    capture.start(BindingDevice::Gamepad, 10.0, SDL_GAMEPAD_BUTTON_EAST);
    require(capture.seconds_left(12.2) == 4, "countdown wrong");
    capture.update(16.1);
    require(capture.outcome() == BindingCapture::Outcome::Cancelled, "no timeout");
    (void)capture.take();
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.key(SDL_SCANCODE_ESCAPE, false);
    require(capture.outcome() == BindingCapture::Outcome::Cancelled, "Esc did not cancel a gamepad capture");
    (void)capture.take();

    // Axes bind by direction once let go; one pushed at the start must
    // return first.
    float axes[SDL_GAMEPAD_AXIS_COUNT]{};
    axes[SDL_GAMEPAD_AXIS_LEFTX] = 0.9f;
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST, axes);
    capture.axis(SDL_GAMEPAD_AXIS_LEFTX, 0.95f);
    capture.axis(SDL_GAMEPAD_AXIS_LEFTX, 0.1f);
    require(capture.active() && !capture.pressing(), "an axis pushed at the start was captured");
    capture.axis(SDL_GAMEPAD_AXIS_LEFTX, -0.8f);
    require(capture.active() && capture.pressing(), "axis bound before release");
    capture.axis(SDL_GAMEPAD_AXIS_LEFTX, -0.1f);
    require(capture.take() == Binding{Binding::Kind::Axis, SDL_GAMEPAD_AXIS_LEFTX, -1}, "stick direction not bound");
    capture.start(BindingDevice::Gamepad, 0.0, SDL_GAMEPAD_BUTTON_EAST);
    capture.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.4f);
    require(!capture.pressing(), "a light trigger touch counted");
    capture.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.7f);
    capture.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f);
    require(capture.take() == Binding{Binding::Kind::Axis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1}, "trigger not bound");
}

void discs(const std::filesystem::path &directory) {
    using namespace sarusaru;
    std::vector<std::uint8_t> boot(3000u);
    for (std::size_t i = 0; i < boot.size(); ++i) boot[i] = static_cast<std::uint8_t>(i * 13u + 5u);
    const std::string hash = psprecomp::sha256(boot);
    const DiscIdentity identity{"TEST00001", boot.size(), hash};
    const auto write = [&](const char *name, const std::vector<std::uint8_t> &bytes) {
        const auto path = directory / name;
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()),
                                                     static_cast<std::streamsize>(bytes.size()));
        return path;
    };

    const auto good = write("good.iso", disc_image("TEST-00001|0001|", boot));
    const auto check = check_disc(good, identity);
    require(check.supported() && check.disc_id == "TEST00001", "supported disc rejected");

    require(check_disc(directory / "absent.iso", identity).result == DiscCheck::Result::Missing, "missing file accepted");
    require(check_disc(write("text.iso", std::vector<std::uint8_t>(40000u, 'x')), identity).result ==
                DiscCheck::Result::NotAnIso,
            "non-ISO accepted");
    require(check_disc(write("nopsp.iso", disc_image("not a umd", boot)), identity).result == DiscCheck::Result::NotPspGame,
            "non-PSP disc accepted");
    const auto other = check_disc(write("other.iso", disc_image("ULUS-12345|0001|", boot)), identity);
    require(other.result == DiscCheck::Result::OtherGame && other.disc_id == "ULUS12345", "other game accepted or not named");
    auto changed = boot;
    changed[100] ^= 1u;
    require(check_disc(write("changed.iso", disc_image("TEST-00001|0001|", changed)), identity).result ==
                DiscCheck::Result::OtherExecutable,
            "changed executable accepted");

    umd::DiscImage disc(good);
    verify_disc(disc, identity);
    try {
        umd::DiscImage wrong(directory / "other.iso");
        verify_disc(wrong, identity);
        require(false, "verify_disc accepted another game");
    } catch (const psprecomp::Error &) {}
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("sarusaru-launcher-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    texts();
    rows();
    presets();
    bindings();
    discs(directory);
    std::filesystem::remove_all(directory);
    std::cout << "Launcher texts, rows, bindings, file listing and disc recognition tests passed\n";
}
