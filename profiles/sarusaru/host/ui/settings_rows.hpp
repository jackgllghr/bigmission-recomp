#pragma once

// The settings the launcher shows, as rows of values to step through: what
// each row says about the current settings, and how left/right (or a tap on
// its arrows) changes them. Pure, so the tests check it without a window.
#include "shell/settings.hpp"
#include "ui/text.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sarusaru::ui {

enum class SettingRow {
    InternalResolution, Textures, FrameRate, Fov, DrawDistance, CelShading, Fullscreen,
    WindowSize,
    DeadZone,
    Language, GameText, Subtitles, ConfirmButton, ControllerButtons, SkipIntroLogos, AutomaticUpdates,
};

struct SettingsTab {
    Text title;
    std::span<const SettingRow> rows;
};
[[nodiscard]] std::span<const SettingsTab> settings_tabs();

// In the current language (ui/text.hpp).
struct RowView {
    std::string label;
    std::string value;
    std::string description;
    // The value clashes with another setting (description says how).
    bool warning{};
    // Another setting makes this one moot (description says which); step() leaves it.
    bool enabled{true};
    // A gamepad face button drawn before the value, as the player's pad labels it.
    SDL_GamepadButton face{SDL_GAMEPAD_BUTTON_INVALID};
};
// `in_game`: shown in the pause menu, where a row that only takes effect at
// startup says so (restart_only()).
[[nodiscard]] RowView describe(SettingRow row, const shell::Settings &settings, bool in_game = false);

// Rows the running game cannot apply: the game text is patched in and the
// intro logos are skipped as it boots. Every other row applies at once.
[[nodiscard]] bool restart_only(SettingRow row);

// Moves the row's value one step in `direction` (+1 or -1), wrapping around
// at the ends. A value set by hand that is not one of the row's steps moves
// to the nearest step in that direction.
void step(SettingRow row, shell::Settings &settings, int direction);

// The Textures row's choice, from the pack directory's name
// (install/texture_build.hpp): 0 for the original textures, 4 or 8 for an
// HD pack made at that scale, nothing for a pack made some other way.
[[nodiscard]] std::optional<std::uint32_t> texture_scale(const shell::Settings &settings);
// The next of original (0), 4x and 8x in `direction`.
[[nodiscard]] std::uint32_t next_texture_scale(std::optional<std::uint32_t> current, int direction);

// The general rule step() follows, for a sorted list of values.
[[nodiscard]] double step_value(std::span<const double> values, double current, int direction);

} // namespace sarusaru::ui
