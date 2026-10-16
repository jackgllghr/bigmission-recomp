#include "ui/settings_rows.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace sarusaru::ui {
namespace {

constexpr SettingRow kGraphics[] = {SettingRow::Preset, SettingRow::Fullscreen, SettingRow::WindowSize, SettingRow::InternalResolution,
    SettingRow::FrameRate, SettingRow::Fov, SettingRow::DrawDistance, SettingRow::CelShading, SettingRow::Textures};
constexpr SettingRow kControls[] = {SettingRow::DeadZone};
constexpr SettingRow kInterface[] = {SettingRow::Language, SettingRow::GameText, SettingRow::Subtitles, SettingRow::ConfirmButton, SettingRow::ControllerButtons, SettingRow::SkipIntroLogos};
constexpr SettingRow kUpdates[] = {SettingRow::AutomaticUpdates};
constexpr SettingsTab kTabs[] = {{Text::TabGraphics, kGraphics}, {Text::TabControls, kControls},
                                 {Text::TabInterface, kInterface}, {Text::TabUpdates, kUpdates}};

// Native, a cap of 30 and 60 FPS motion (shell::interpolates).
constexpr double kFrameRates[] = {0, 30, 60};
// Native and how many times as far actors stay drawn (game/draw_distance.hpp).
constexpr double kDrawDistances[] = {1, 1.5, 2, 3, 4, 6, 8};
// Window widths at the PSP's aspect (the height follows): 2, 3, 4 and 6 times 480.
constexpr double kWindowWidths[] = {960, 1440, 1920, 2880};

std::vector<double> fov_steps() {
    std::vector<double> steps{0.0};
    for (int degrees = 30; degrees <= 120; degrees += 5) steps.push_back(degrees);
    return steps;
}
std::vector<double> dead_zone_steps() {
    std::vector<double> steps;
    for (int percent = 0; percent <= 95; percent += 5) steps.push_back(percent / 100.0);
    return steps;
}

std::string number(const char *pattern, double value) {
    char text[64];
    std::snprintf(text, sizeof text, pattern, value);
    return text;
}
std::string on_off(bool value) { return std::string(tr(value ? Text::On : Text::Off)); }

} // namespace

std::span<const SettingsTab> settings_tabs() { return kTabs; }

std::optional<std::uint32_t> texture_scale(const shell::Settings &settings) {
    if (settings.texture_pack.empty()) return 0u;
    auto name = settings.texture_pack.filename().string();
    if (name.empty()) name = settings.texture_pack.parent_path().filename().string();
    if (name.starts_with("4x-")) return 4u;
    if (name.starts_with("8x-")) return 8u;
    return std::nullopt;
}

std::uint32_t next_texture_scale(std::optional<std::uint32_t> current, int direction) {
    constexpr std::uint32_t kOrder[] = {0u, 4u, 8u};
    const int index = !current ? 0 : *current == 8u ? 2 : *current == 4u ? 1 : 0;
    return kOrder[(index + (direction > 0 ? 1 : 2)) % 3];
}

namespace {
bool offers_english = true, offers_hd = true;
} // namespace

void set_offered_downloads(bool english, bool hd_textures) {
    offers_english = english;
    offers_hd = hd_textures;
}

Preset preset_of(const shell::Settings &settings) {
    const bool english = settings.translation_language == "en";
    if (settings.internal_resolution == 1u && settings.texture_pack.empty() && !english &&
        settings.draw_distance == 1.0f && !settings.skip_intro_logos)
        return Preset::Native;
    if (settings.internal_resolution == kModernResolution &&
        (offers_hd ? texture_scale(settings) == 4u : settings.texture_pack.empty()) && (english || !offers_english) &&
        settings.draw_distance == kModernDrawDistance && settings.skip_intro_logos)
        return Preset::Modern;
    return Preset::Custom;
}

Preset next_preset(Preset current, int direction) {
    if (current == Preset::Custom) return direction > 0 ? Preset::Native : Preset::Modern;
    return current == Preset::Native ? Preset::Modern : Preset::Native;
}

void apply_preset(shell::Settings &settings, Preset preset) {
    switch (preset) {
    case Preset::Native:
        settings.internal_resolution = 1u;
        settings.texture_pack.clear();
        settings.translation_language = "ja";
        settings.draw_distance = 1.0f;
        settings.skip_intro_logos = false;
        break;
    case Preset::Modern:
        settings.internal_resolution = kModernResolution;
        settings.draw_distance = kModernDrawDistance;
        settings.skip_intro_logos = true;
        break;
    case Preset::Custom:
        break;
    }
}

double step_value(std::span<const double> values, double current, int direction) {
    if (values.empty()) return current;
    // Within half a thousandth counts as the step itself (float settings).
    constexpr double kTolerance = 0.0005;
    const auto exact = std::find_if(values.begin(), values.end(),
        [&](double value) { return std::abs(value - current) < kTolerance; });
    if (exact != values.end()) {
        auto index = static_cast<std::ptrdiff_t>(exact - values.begin()) + (direction > 0 ? 1 : -1);
        const auto size = static_cast<std::ptrdiff_t>(values.size());
        return values[static_cast<std::size_t>((index % size + size) % size)];
    }
    if (direction > 0) {
        const auto above = std::upper_bound(values.begin(), values.end(), current);
        return above != values.end() ? *above : values.front();
    }
    const auto below = std::lower_bound(values.begin(), values.end(), current);
    return below != values.begin() ? *(below - 1) : values.back();
}

bool restart_only(SettingRow row) { return row == SettingRow::GameText || row == SettingRow::SkipIntroLogos; }

RowView describe(SettingRow row, const shell::Settings &settings, bool in_game) {
    RowView view;
    const auto text = [](Text id) { return std::string(tr(id)); };
    switch (row) {
    case SettingRow::Preset: {
        constexpr Text values[] = {Text::ValuePresetNative, Text::ValuePresetModern, Text::ValuePresetCustom};
        constexpr Text descriptions[] = {Text::DescPresetNative, Text::DescPresetModern, Text::DescPresetCustom};
        const auto preset = static_cast<std::size_t>(preset_of(settings));
        view.label = text(Text::RowPreset);
        view.value = text(values[preset]);
        view.description = text(descriptions[preset]);
        break;
    }
    case SettingRow::InternalResolution: {
        const auto scale = settings.internal_resolution;
        view.label = text(Text::RowResolution);
        view.value = std::to_string(scale) + "\u00D7 (" + std::to_string(480u * scale) + "\u00D7" +
                     std::to_string(272u * scale) + ")";
        view.description = text(Text::DescResolution);
        break;
    }
    case SettingRow::CelShading:
        view.label = text(Text::RowCelShading);
        view.value = text(settings.cel_shading == 0 ? Text::Off :
                          settings.cel_shading == 1 ? Text::ValueCelBanded : Text::ValueCelOutlined);
        view.description = text(settings.cel_shading == 2 ? Text::DescCelOutlined : Text::DescCelShading);
        break;
    case SettingRow::Textures: {
        view.label = text(Text::RowTextures);
        const auto scale = texture_scale(settings);
        view.value = text(!scale ? Text::ValueTexturesCustom : *scale == 0u ? Text::ValueTexturesOriginal
                                  : *scale == 4u ? Text::ValueTexturesHd : Text::ValueTextures4k);
        view.description = text(Text::DescTextures);
        break;
    }
    case SettingRow::FrameRate:
        view.label = text(Text::RowFrameRate);
        view.value = settings.frame_rate == 0 ? text(Text::ValueFrameRateNative)
                                              : ui::format(Text::ValueFps, {std::to_string(settings.frame_rate)});
        view.description = text(shell::interpolates(settings) ? Text::DescFrameRate60 : Text::DescFrameRate);
        break;
    case SettingRow::Fov:
        view.label = text(Text::RowFov);
        view.value = settings.fov_degrees == 0.0f ? text(Text::ValueFovNative) : number("%.0f\u00B0", settings.fov_degrees);
        view.description = text(Text::DescFov);
        break;
    case SettingRow::DrawDistance:
        view.label = text(Text::RowDrawDistance);
        view.value = settings.draw_distance == 1.0f ? text(Text::ValueDrawDistanceNative)
                                                    : number("%g\u00D7", settings.draw_distance);
        view.description = text(Text::DescDrawDistance);
        break;
    case SettingRow::Fullscreen:
        view.label = text(Text::RowFullscreen);
        view.value = on_off(settings.fullscreen);
        view.description = text(Text::DescFullscreen);
        break;
    case SettingRow::WindowSize:
        view.label = text(Text::RowWindowSize);
        view.value = std::to_string(settings.window_width) + "\u00D7" + std::to_string(settings.window_height);
        view.enabled = !settings.fullscreen;
        view.description = text(view.enabled ? Text::DescWindowSize : Text::DescWindowSizeFullscreen);
        break;
    case SettingRow::DeadZone:
        view.label = text(Text::RowDeadZone);
        view.value = number("%.0f%%", settings.controls.dead_zone * 100.0);
        view.description = text(Text::DescDeadZone);
        break;
    case SettingRow::AutomaticUpdates:
        view.label = text(Text::RowAutomaticUpdates);
        view.value = on_off(settings.check_for_updates);
        view.description = text(Text::DescAutomaticUpdates);
#if defined(__ANDROID__)
        view.enabled = false;
        view.value = "Obtainium";
        view.description = text(Text::UpdatesAndroid);
#endif
        break;
    case SettingRow::Language:
        view.label = text(Text::RowLanguage);
        // Each language is named in itself, so it can be found in either.
        view.value = settings.language == shell::Language::Auto
            ? ui::format(Text::ValueAutomatic, {language_name(resolve_language(shell::Language::Auto))})
            : std::string(language_name(settings.language));
        view.description = text(Text::DescLanguage);
        break;
    case SettingRow::GameText:
        view.label = text(Text::RowGameText);
        view.value = text(settings.translation_language == "en" ? Text::ValueGameEnglish : Text::ValueGameJapanese);
        view.description = text(Text::DescGameText);
        break;
    case SettingRow::Subtitles:
        view.label = text(Text::RowSubtitles);
        view.value = on_off(settings.subtitles);
        view.description = text(Text::DescSubtitles);
        break;
    case SettingRow::SkipIntroLogos:
        view.label = text(Text::RowSkipIntroLogos);
        view.value = on_off(settings.skip_intro_logos);
        view.description = text(Text::DescSkipIntroLogos);
        break;
    case SettingRow::ControllerButtons: {
        constexpr Text values[] = {Text::ValuePromptAuto, Text::ValuePromptXbox, Text::ValuePromptSwitch,
            Text::ValuePromptPlayStation, Text::ValuePromptKeyboard};
        view.label = text(Text::RowControllerButtons);
        view.value = text(values[static_cast<std::size_t>(settings.prompt_style)]);
        view.description = text(Text::DescControllerButtons);
        break;
    }
    case SettingRow::ConfirmButton:
        view.label = text(Text::RowConfirm);
        view.value = text(settings.confirm_south ? Text::ValueSouth : Text::ValueEast);
        view.face = settings.confirm_south ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
        view.description = text(Text::DescConfirm);
        break;
    }
    if (in_game && restart_only(row)) view.description += " " + text(Text::AppliesNextStart);
    return view;
}

void step(SettingRow row, shell::Settings &settings, int direction) {
    switch (row) {
    // The launcher also turns on Modern's HD textures and English (apply_preset).
    case SettingRow::Preset:
        apply_preset(settings, next_preset(preset_of(settings), direction));
        break;
    case SettingRow::InternalResolution:
        // 1 to 8, wrapping.
        settings.internal_resolution = direction > 0 ? settings.internal_resolution % 8u + 1u
                                                     : (settings.internal_resolution + 6u) % 8u + 1u;
        break;
    case SettingRow::CelShading: {
        constexpr double presets[] = {0, 1, 2};
        settings.cel_shading = static_cast<std::uint32_t>(step_value(presets, settings.cel_shading, direction));
        break;
    }
    // The launcher makes or finds the pack for an HD choice; back to the original here.
    case SettingRow::Textures:
        if (next_texture_scale(texture_scale(settings), direction) == 0u) settings.texture_pack.clear();
        break;
    case SettingRow::FrameRate:
        settings.frame_rate = static_cast<std::uint32_t>(step_value(kFrameRates, settings.frame_rate, direction));
        break;
    case SettingRow::Fov: {
        const auto steps = fov_steps();
        settings.fov_degrees = static_cast<float>(step_value(steps, settings.fov_degrees, direction));
        break;
    }
    case SettingRow::DrawDistance:
        settings.draw_distance = static_cast<float>(step_value(kDrawDistances, settings.draw_distance, direction));
        break;
    case SettingRow::Fullscreen:
        settings.fullscreen = !settings.fullscreen;
        break;
    case SettingRow::WindowSize: {
        if (settings.fullscreen) break;
        const auto width = static_cast<std::uint32_t>(step_value(kWindowWidths, settings.window_width, direction));
        settings.window_width = width;
        settings.window_height = width * 272u / 480u;
        break;
    }
    case SettingRow::DeadZone: {
        const auto steps = dead_zone_steps();
        settings.controls.dead_zone = static_cast<float>(step_value(steps, settings.controls.dead_zone, direction));
        break;
    }
    case SettingRow::AutomaticUpdates:
#if !defined(__ANDROID__)
        settings.check_for_updates = !settings.check_for_updates;
#endif
        break;
    case SettingRow::Language: {
        constexpr shell::Language kOrder[] = {shell::Language::Auto, shell::Language::English, shell::Language::Japanese};
        const auto index = static_cast<int>(settings.language) + (direction > 0 ? 1 : 2);
        settings.language = kOrder[index % 3];
        break;
    }
    // The launcher downloads the English pack before it lets this turn English on.
    case SettingRow::GameText:
        settings.translation_language = settings.translation_language == "en" ? "ja" : "en";
        break;
    case SettingRow::Subtitles:
        settings.subtitles = !settings.subtitles;
        break;
    case SettingRow::SkipIntroLogos:
        settings.skip_intro_logos = !settings.skip_intro_logos;
        break;
    case SettingRow::ControllerButtons:
        settings.prompt_style = static_cast<shell::PromptStyle>((static_cast<int>(settings.prompt_style) + (direction > 0 ? 1 : 4)) % 5);
        break;
    case SettingRow::ConfirmButton:
        settings.confirm_south = !settings.confirm_south;
        break;
    }
}

} // namespace sarusaru::ui
