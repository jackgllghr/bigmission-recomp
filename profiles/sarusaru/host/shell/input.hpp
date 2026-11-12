#pragma once

// Host keyboard and gamepads to the PSP pad, using the settings file's bindings.
#include "ctrl/controller.hpp"
#include "shell/settings.hpp"
#include "shell/touch_controls.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace sarusaru::shell {
struct GamepadState {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<std::int16_t, SDL_GAMEPAD_AXIS_COUNT> axes{};
};
struct InputState {
    ctrl::Controller::Pad pad;
    bool fast_forward{}, menu{}, save_state{}, load_state{};
    float camera_x{}, camera_y{};
    float aim_x{}, aim_y{}; // physical right stick, with the same dead zone
    bool camera_reset{};
};
// Pure mapping shared by live polling and synthetic tests (no window/device required).
// A pad's button chord (two buttons held together) takes those buttons for
// itself: while it is held, neither also triggers its own single-button actions.
[[nodiscard]] InputState map_input(const ControlSettings &settings, std::span<const bool> keys,
                                   std::span<const GamepadState> gamepads);
// Routes the camera controls to the PSP stick only while an ability is aiming.
// The stronger stick wins; sensitivity changes response up to the native limit.
void apply_aiming(InputState &state, bool aiming, float sensitivity);
// Adds the on-screen controls, which use the PSP's own layout whatever the
// bindings: their buttons, the left stick when pushed further than the
// pads', and the right stick as the default gamepad bindings' camera and aim.
void apply_touch(InputState &state, const TouchPad &touch);
// Whether this build shows on-screen controls: on Android, or elsewhere with
// SARUSARU_TOUCH_CONTROLS=1 (the mouse then touches, for trying them out).
[[nodiscard]] bool touch_controls_available();

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
    // The on-screen controls while they are shown (no gamepad connected), else null.
    [[nodiscard]] const TouchControls *touch_controls() const noexcept {
        return touch_ && gamepads_.empty() ? &*touch_ : nullptr;
    }
    // Lets go of every touch, for when the pause menu had the window's input.
    void release_touches() noexcept { if (touch_) touch_->clear(); }
private:
    ControlSettings settings_;
    std::vector<SDL_Gamepad *> gamepads_;
    std::optional<TouchControls> touch_;
};
} // namespace sarusaru::shell
