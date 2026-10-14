#include "shell/input.hpp"

#include <algorithm>
#include <cmath>

namespace sarusaru::shell {

Input::~Input() {
    for (auto *gamepad : gamepads_) SDL_CloseGamepad(gamepad);
}

void Input::handle(const SDL_Event &event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (SDL_Gamepad *gamepad = SDL_OpenGamepad(event.gdevice.which)) gamepads_.push_back(gamepad);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        const auto gone = std::find_if(gamepads_.begin(), gamepads_.end(), [&](SDL_Gamepad *gamepad) {
            return SDL_GetGamepadID(gamepad) == event.gdevice.which;
        });
        if (gone != gamepads_.end()) {
            SDL_CloseGamepad(*gone);
            gamepads_.erase(gone);
        }
    }
}

ctrl::Controller::Pad Input::pad() const {
    using namespace ctrl;
    ctrl::Controller::Pad pad;
    const bool *keys = SDL_GetKeyboardState(nullptr);
    const struct { SDL_Scancode key; std::uint32_t button; } keyboard[] = {
        {SDL_SCANCODE_UP, kButtonUp}, {SDL_SCANCODE_DOWN, kButtonDown}, {SDL_SCANCODE_LEFT, kButtonLeft},
        {SDL_SCANCODE_RIGHT, kButtonRight}, {SDL_SCANCODE_Z, kButtonCircle}, {SDL_SCANCODE_X, kButtonCross},
        {SDL_SCANCODE_A, kButtonSquare}, {SDL_SCANCODE_S, kButtonTriangle}, {SDL_SCANCODE_Q, kButtonL},
        {SDL_SCANCODE_W, kButtonR}, {SDL_SCANCODE_RETURN, kButtonStart}, {SDL_SCANCODE_BACKSPACE, kButtonSelect},
    };
    for (const auto &[key, button] : keyboard)
        if (keys[key]) pad.buttons |= button;
    float x = 0.0f, y = 0.0f;
    if (keys[SDL_SCANCODE_J]) x -= 1.0f;
    if (keys[SDL_SCANCODE_L]) x += 1.0f;
    if (keys[SDL_SCANCODE_I]) y -= 1.0f;
    if (keys[SDL_SCANCODE_K]) y += 1.0f;

    const struct { SDL_GamepadButton from; std::uint32_t button; } buttons[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, kButtonUp}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, kButtonDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, kButtonLeft}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, kButtonRight},
        {SDL_GAMEPAD_BUTTON_EAST, kButtonCircle}, {SDL_GAMEPAD_BUTTON_SOUTH, kButtonCross},
        {SDL_GAMEPAD_BUTTON_WEST, kButtonSquare}, {SDL_GAMEPAD_BUTTON_NORTH, kButtonTriangle},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, kButtonL}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, kButtonR},
        {SDL_GAMEPAD_BUTTON_START, kButtonStart}, {SDL_GAMEPAD_BUTTON_BACK, kButtonSelect},
    };
    for (auto *gamepad : gamepads_) {
        for (const auto &[from, button] : buttons)
            if (SDL_GetGamepadButton(gamepad, from)) pad.buttons |= button;
        // The triggers double as the shoulders.
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) pad.buttons |= kButtonL;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) pad.buttons |= kButtonR;
        const float stick_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        const float stick_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        if (std::hypot(stick_x, stick_y) > 0.2f) {
            x = stick_x;
            y = stick_y;
        }
    }
    const auto axis = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(128.0f + value * 127.5f), 0l, 255l));
    };
    pad.lx = axis(x);
    pad.ly = axis(y);
    return pad;
}

} // namespace sarusaru::shell
