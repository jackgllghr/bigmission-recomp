#include "ui/settings_rows.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace sarusaru::ui {
namespace {

constexpr SettingRow kGraphics[] = {SettingRow::InternalResolution, SettingRow::FrameInterpolation,
    SettingRow::FrameRateCap, SettingRow::Fov, SettingRow::GameFrameRate};
constexpr SettingRow kWindow[] = {SettingRow::Fullscreen, SettingRow::WindowSize};
constexpr SettingRow kControls[] = {SettingRow::DeadZone};
constexpr SettingRow kInterface[] = {SettingRow::Language, SettingRow::GameText, SettingRow::ConfirmButton};
constexpr SettingsTab kTabs[] = {{Text::TabGraphics, kGraphics}, {Text::TabWindow, kWindow},
                                 {Text::TabControls, kControls}, {Text::TabInterface, kInterface}};

constexpr double kGameRates[] = {0, 15, 20, 30, 60};
constexpr double kFrameCaps[] = {0, 15, 20, 30, 60};
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

RowView describe(SettingRow row, const shell::Settings &settings) {
    RowView view;
    const auto text = [](Text id) { return std::string(tr(id)); };
    switch (row) {
    case SettingRow::InternalResolution: {
        const auto scale = settings.internal_resolution;
        view.label = text(Text::RowResolution);
        view.value = std::to_string(scale) + "\u00D7 (" + std::to_string(480u * scale) + "\u00D7" +
                     std::to_string(272u * scale) + ")";
        view.description = text(Text::DescResolution);
        break;
    }
    case SettingRow::FrameInterpolation:
        view.label = text(Text::RowInterpolation);
        view.value = on_off(settings.frame_interpolation);
        view.description = text(Text::DescInterpolation);
        if (settings.frame_interpolation && settings.experimental_game_frame_rate != 0) {
            view.warning = true;
            view.description = text(Text::WarnInterpolationRate);
        } else if (settings.frame_interpolation && settings.frame_rate != 0 && settings.frame_rate < 60) {
            view.warning = true;
            view.description = text(Text::WarnInterpolationCap);
        }
        break;
    case SettingRow::FrameRateCap:
        view.label = text(Text::RowFrameCap);
        view.value = settings.frame_rate == 0 ? text(Text::Off) : ui::format(Text::ValueFps, {std::to_string(settings.frame_rate)});
        view.description = text(Text::DescFrameCap);
        break;
    case SettingRow::Fov:
        view.label = text(Text::RowFov);
        view.value = settings.fov_degrees == 0.0f ? text(Text::ValueFovNative) : number("%.0f\u00B0", settings.fov_degrees);
        view.description = text(Text::DescFov);
        break;
    case SettingRow::GameFrameRate:
        view.label = text(Text::RowGameRate);
        view.value = settings.experimental_game_frame_rate == 0
            ? text(Text::ValueGameRateNative)
            : ui::format(Text::ValueExperimental, {std::to_string(settings.experimental_game_frame_rate)});
        view.description = text(Text::DescGameRate);
        view.warning = settings.experimental_game_frame_rate != 0;
        break;
    case SettingRow::Fullscreen:
        view.label = text(Text::RowFullscreen);
        view.value = on_off(settings.fullscreen);
        view.description = text(Text::DescFullscreen);
        break;
    case SettingRow::WindowSize:
        view.label = text(Text::RowWindowSize);
        view.value = std::to_string(settings.window_width) + "\u00D7" + std::to_string(settings.window_height);
        view.description = text(Text::DescWindowSize);
        break;
    case SettingRow::DeadZone:
        view.label = text(Text::RowDeadZone);
        view.value = number("%.0f%%", settings.controls.dead_zone * 100.0);
        view.description = text(Text::DescDeadZone);
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
    case SettingRow::ConfirmButton:
        view.label = text(Text::RowConfirm);
        view.value = text(settings.confirm_south ? Text::ValueSouth : Text::ValueEast);
        view.face = settings.confirm_south ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
        view.description = text(Text::DescConfirm);
        break;
    }
    return view;
}

void step(SettingRow row, shell::Settings &settings, int direction) {
    switch (row) {
    case SettingRow::InternalResolution:
        // 1 to 8, wrapping.
        settings.internal_resolution = direction > 0 ? settings.internal_resolution % 8u + 1u
                                                     : (settings.internal_resolution + 6u) % 8u + 1u;
        break;
    case SettingRow::FrameInterpolation:
        settings.frame_interpolation = !settings.frame_interpolation;
        break;
    case SettingRow::FrameRateCap:
        settings.frame_rate = static_cast<std::uint32_t>(step_value(kFrameCaps, settings.frame_rate, direction));
        break;
    case SettingRow::Fov: {
        const auto steps = fov_steps();
        settings.fov_degrees = static_cast<float>(step_value(steps, settings.fov_degrees, direction));
        break;
    }
    case SettingRow::GameFrameRate:
        settings.experimental_game_frame_rate =
            static_cast<std::uint32_t>(step_value(kGameRates, settings.experimental_game_frame_rate, direction));
        break;
    case SettingRow::Fullscreen:
        settings.fullscreen = !settings.fullscreen;
        break;
    case SettingRow::WindowSize: {
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
    case SettingRow::ConfirmButton:
        settings.confirm_south = !settings.confirm_south;
        break;
    }
}

} // namespace sarusaru::ui
