#pragma once

// The settings the launcher shows, as rows of values to step through: what
// each row says about the current settings, and how left/right (or a tap on
// its arrows) changes them. Pure, so the tests check it without a window.
#include "shell/settings.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace sarusaru::ui {

enum class SettingRow {
    InternalResolution, FrameInterpolation, FrameRateCap, Fov, GameFrameRate,
    Fullscreen, WindowSize,
    DeadZone, ConfirmButton,
};

struct SettingsTab {
    std::string_view title;
    std::span<const SettingRow> rows;
};
[[nodiscard]] std::span<const SettingsTab> settings_tabs();

struct RowView {
    std::string label;
    std::string value;
    std::string description;
    // The value clashes with another setting (description says how).
    bool warning{};
};
[[nodiscard]] RowView describe(SettingRow row, const shell::Settings &settings);

// Moves the row's value one step in `direction` (+1 or -1), wrapping around
// at the ends. A value set by hand that is not one of the row's steps moves
// to the nearest step in that direction.
void step(SettingRow row, shell::Settings &settings, int direction);

// The general rule step() follows, for a sorted list of values.
[[nodiscard]] double step_value(std::span<const double> values, double current, int direction);

} // namespace sarusaru::ui
