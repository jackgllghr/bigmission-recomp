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
    AnalogLeft, AnalogRight, AnalogUp, AnalogDown, FastForward, Menu, CameraLeft, CameraRight, CameraUp, CameraDown, CameraReset, SaveState, LoadState, Count
};
inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);
inline constexpr std::array<std::string_view, kActionCount> kActionNames = {
    "up", "down", "left", "right", "circle", "cross", "square", "triangle", "l", "r", "start", "select",
    "analog_left", "analog_right", "analog_up", "analog_down", "fast_forward", "menu",
    "camera_left", "camera_right", "camera_up", "camera_down", "camera_reset", "save_state", "load_state"
};
struct Binding {
    enum class Kind { Key, Button, Axis };
    Kind kind;
    int code;
    int direction{1};
    // A Button binding of two buttons held together (written a+b): the other
    // button, or -1. Stored with the lower button in `code`.
    int chord{-1};
    bool operator==(const Binding &) const = default;
};
struct ControlSettings {
    std::array<std::vector<Binding>, kActionCount> keyboard, gamepad;
    float dead_zone{0.2f};
    bool operator==(const ControlSettings &) const = default;
};
// The launcher's language; Auto follows the system's preferred language.
enum class Language { Auto, English, Japanese };
enum class PromptStyle { Auto, Xbox, Switch, PlayStation, Keyboard };
inline constexpr std::array<std::string_view, 5> kPromptStyleNames = {"auto", "xbox", "switch", "playstation", "keyboard"};
struct PromptAppearance { bool gamepad; SDL_GamepadType type; };
[[nodiscard]] constexpr PromptAppearance resolve_prompts(PromptStyle style, bool gamepad, SDL_GamepadType detected) {
    switch (style) {
    case PromptStyle::Xbox: return {true, SDL_GAMEPAD_TYPE_XBOXONE};
    case PromptStyle::Switch: return {true, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO};
    case PromptStyle::PlayStation: return {true, SDL_GAMEPAD_TYPE_PS5};
    case PromptStyle::Keyboard: return {false, detected};
    case PromptStyle::Auto: return {gamepad, detected};
    }
    return {gamepad, detected};
}

struct Settings {
    std::string translation_language{"ja"};
    // Relative paths resolve beside the selected settings file.
    std::filesystem::path translation_pack;
    // Subtitles over the movies, where there are subtitle files for them
    // (movie/subtitles.hpp).
    bool subtitles{true};
    std::uint32_t internal_resolution{2};
    // 0 = original, 1 = banded, 2 = banded with black world and character silhouettes.
    std::uint32_t cel_shading{0};
    // The window's size at startup, in points (it stays resizable), and
    // whether it opens fullscreen at the desktop's resolution instead.
    std::uint32_t window_width{960};
    std::uint32_t window_height{544};
    bool fullscreen{false};
    // 0 = every PSP vblank (the game's own frames), 1-59 = a presentation
    // cap, 60 = 60 FPS motion: an in-between picture for each game frame,
    // at the original game speed, one vblank later. The game always updates
    // at its native rate; guest timing stays 60000/1001 Hz.
    std::uint32_t frame_rate{0};
    // 0 = native; otherwise the gameplay camera's vertical FOV (game/camera_fov.hpp), 30-120 degrees.
    float fov_degrees{0.0f};
    // 1 = native; otherwise how much farther actors stay drawn (game/draw_distance.hpp), 1-8.
    float draw_distance{1.0f};
    // An HD texture pack directory (textures/hd_pack.hpp), or empty for the
    // original textures. Relative paths resolve beside the settings file.
    std::filesystem::path texture_pack;
    ControlSettings controls;
    // The player's disc image, chosen in the launcher (empty until then).
    std::filesystem::path game_disc;
    // Which face button confirms in the launcher and the port's screens: the
    // south one (cross) by default, or the east one (circle) as the game does.
    bool confirm_south{true};
    bool skip_intro_logos{false};
    std::uint32_t state_slot{1};
    bool check_for_updates{true};
    std::int64_t last_update_check{};
    Language language{Language::Auto};
    PromptStyle prompt_style{PromptStyle::Auto};
    bool operator==(const Settings &) const = default;
};

// Whether `frame_rate` asks for 60 FPS motion (interpolated in-between pictures).
[[nodiscard]] constexpr bool interpolates(const Settings &settings) { return settings.frame_rate == 60u; }

[[nodiscard]] Settings default_settings();
// Partial files inherit defaults. Unknown/duplicate keys and invalid bindings throw.
// An earlier version's quit binding becomes the menu's, unless the file binds menu too
// or unbinds quit (the overlay's Quit game replaced it).
// Files from earlier versions still load: experimental_game_frame_rate is
// ignored, and frame_interpolation = 1 becomes frame_rate = 60.
[[nodiscard]] Settings parse_settings(std::string_view text);
[[nodiscard]] std::string serialize_settings(const Settings &settings);
[[nodiscard]] std::filesystem::path default_settings_path();
// Missing files are created with defaults; malformed existing files are never overwritten.
[[nodiscard]] Settings load_settings(const std::filesystem::path &path);
// Replaces the file with `settings` through a temporary file, so a failed
// write leaves the old file intact.
void save_settings(const std::filesystem::path &path, const Settings &settings);

} // namespace sarusaru::shell
