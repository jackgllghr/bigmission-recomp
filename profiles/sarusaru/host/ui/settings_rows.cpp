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
constexpr SettingRow kControls[] = {SettingRow::ConfirmButton, SettingRow::DeadZone};
constexpr SettingsTab kTabs[] = {{"Graphics", kGraphics}, {"Window", kWindow}, {"Controls", kControls}};

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

std::string format(const char *pattern, double value) {
    char text[64];
    std::snprintf(text, sizeof text, pattern, value);
    return text;
}
const char *on_off(bool value) { return value ? "On" : "Off"; }

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
    switch (row) {
    case SettingRow::InternalResolution: {
        const auto scale = settings.internal_resolution;
        view.label = "Resolution";
        view.value = std::to_string(scale) + "× (" + std::to_string(480u * scale) + "×" +
                     std::to_string(272u * scale) + ")";
        view.description = "Draws the game at this many times the PSP's 480×272. Higher is sharper and "
                           "needs a faster GPU.";
        break;
    }
    case SettingRow::FrameInterpolation:
        view.label = "60 FPS motion";
        view.value = on_off(settings.frame_interpolation);
        view.description = "Shows a blended picture between the game's own 30 FPS frames, at the original game "
                           "speed. Adds about 17 ms of delay.";
        if (settings.frame_interpolation && settings.experimental_game_frame_rate != 0) {
            view.warning = true;
            view.description = "Needs the game update rate at Native; it stays off until then.";
        } else if (settings.frame_interpolation && settings.frame_rate != 0 && settings.frame_rate < 60) {
            view.warning = true;
            view.description = "Has no effect while the frame rate cap is below 60.";
        }
        break;
    case SettingRow::FrameRateCap:
        view.label = "Frame rate cap";
        view.value = settings.frame_rate == 0 ? "Off" : std::to_string(settings.frame_rate) + " FPS";
        view.description = "Limits how often pictures are shown. The game itself keeps its speed.";
        break;
    case SettingRow::Fov:
        view.label = "Field of view";
        view.value = settings.fov_degrees == 0.0f ? "Native (36.5°)" : format("%.0f°", settings.fov_degrees);
        view.description = "Widens the camera in stages and the hub. Menus and cutscenes keep the original view.";
        break;
    case SettingRow::GameFrameRate:
        view.label = "Game update rate";
        view.value = settings.experimental_game_frame_rate == 0
            ? "Native (30)" : std::to_string(settings.experimental_game_frame_rate) + " (experimental)";
        view.description = "Experimental: changes how often the game updates. Movement and animation speed "
                           "change with it. Use 60 FPS motion for smooth play at the right speed.";
        view.warning = settings.experimental_game_frame_rate != 0;
        break;
    case SettingRow::Fullscreen:
        view.label = "Fullscreen";
        view.value = on_off(settings.fullscreen);
        view.description = "Fills the screen at the desktop's resolution. Alt+Enter or F11 switches while playing.";
        break;
    case SettingRow::WindowSize:
        view.label = "Window size";
        view.value = std::to_string(settings.window_width) + "×" + std::to_string(settings.window_height);
        view.description = "The window's size when the game starts. It can still be resized by dragging.";
        break;
    case SettingRow::DeadZone:
        view.label = "Stick dead zone";
        view.value = format("%.0f%%", settings.controls.dead_zone * 100.0);
        view.description = "How far a stick must move before the game sees it. Raise it if a worn stick drifts.";
        break;
    case SettingRow::ConfirmButton:
        view.label = "Confirm button";
        view.value = settings.confirm_south ? "× South" : "○ East";
        view.description = "The face button that confirms in these menus. The game itself confirms with "
                           "○ (east), as in Japan.";
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
    case SettingRow::ConfirmButton:
        settings.confirm_south = !settings.confirm_south;
        break;
    }
}

} // namespace sarusaru::ui
