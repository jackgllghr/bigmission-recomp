#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sarusaru::shell {

enum class Action : std::size_t {
    Up, Down, Left, Right, Circle, Cross, Square, Triangle, L, R, Start, Select,
    AnalogLeft, AnalogRight, AnalogUp, AnalogDown, FastForward, Quit, Count
};
inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);
inline constexpr std::array<std::string_view, kActionCount> kActionNames = {
    "up", "down", "left", "right", "circle", "cross", "square", "triangle", "l", "r", "start", "select",
    "analog_left", "analog_right", "analog_up", "analog_down", "fast_forward", "quit"
};
struct Binding {
    enum class Kind { Key, Button, Axis };
    Kind kind;
    int code;
    int direction{1};
    bool operator==(const Binding &) const = default;
};
struct ControlSettings {
    std::array<std::vector<Binding>, kActionCount> keyboard, gamepad;
    float dead_zone{0.2f};
    bool operator==(const ControlSettings &) const = default;
};
struct Settings {
    std::uint32_t internal_resolution{2};
    // Presentation cap, 0 = every PSP vblank. Guest timing stays 60000/1001 Hz.
    std::uint32_t frame_rate{0};
    // Native game cadence override: 0, 15, 20, 30 or 60. Changes fixed-step game speed.
    std::uint32_t experimental_game_frame_rate{0};
    // 0 = native; reserved until gameplay camera/cutscene discrimination is verified.
    float fov_degrees{0.0f};
    ControlSettings controls;
    bool operator==(const Settings &) const = default;
};

[[nodiscard]] Settings default_settings();
// Partial files inherit defaults. Unknown/duplicate keys and invalid bindings throw.
[[nodiscard]] Settings parse_settings(std::string_view text);
[[nodiscard]] std::string serialize_settings(const Settings &settings);
[[nodiscard]] std::filesystem::path default_settings_path();
// Missing files are created with defaults; malformed existing files are never overwritten.
[[nodiscard]] Settings load_settings(const std::filesystem::path &path);

} // namespace sarusaru::shell
