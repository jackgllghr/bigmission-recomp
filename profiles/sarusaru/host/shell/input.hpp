#pragma once

// Host keyboard and gamepads to the PSP pad, using the settings file's bindings.
#include "ctrl/controller.hpp"
#include "shell/settings.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <span>
#include <vector>

namespace sarusaru::shell {
struct GamepadState {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
};
struct InputState {
    ctrl::Controller::Pad pad;
    bool fast_forward{}, menu{};
    float camera_x{}, camera_y{};
    bool camera_reset{};
};
// Pure mapping shared by live polling and synthetic tests (no window/device required).
// A pad's button chord (two buttons held together) takes those buttons for
// itself: while it is held, neither also triggers its own single-button actions.
[[nodiscard]] InputState map_input(const ControlSettings &settings, std::span<const bool> keys,
                                   std::span<const GamepadState> gamepads);
class Input {
public:
    // Opens the gamepads already connected: their SDL_EVENT_GAMEPAD_ADDED may
    // have gone to the launcher before the game started.
    explicit Input(ControlSettings settings);
    ~Input();
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;
    void handle(const SDL_Event &event);
    [[nodiscard]] InputState sample() const;
private:
    ControlSettings settings_;
    std::vector<SDL_Gamepad *> gamepads_;
};
} // namespace sarusaru::shell
