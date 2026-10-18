#include "shell/input.hpp"

#include <algorithm>
#include <cmath>

namespace sarusaru::shell {
Input::Input(ControlSettings settings) : settings_(std::move(settings)) {
    int count = 0;
    if (SDL_JoystickID *ids = SDL_GetGamepads(&count); ids != nullptr) {
        for (int i = 0; i < count; ++i)
            if (auto *pad = SDL_OpenGamepad(ids[i])) gamepads_.push_back(pad);
        SDL_free(ids);
    }
}
Input::~Input() {
    for (auto *gamepad : gamepads_) SDL_CloseGamepad(gamepad);
}
void Input::handle(const SDL_Event &event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        const auto found = std::find_if(gamepads_.begin(), gamepads_.end(), [&](SDL_Gamepad *pad) {
            return SDL_GetGamepadID(pad) == event.gdevice.which;
        });
        if (found == gamepads_.end())
            if (auto *pad = SDL_OpenGamepad(event.gdevice.which)) gamepads_.push_back(pad);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        const auto gone = std::find_if(gamepads_.begin(), gamepads_.end(), [&](SDL_Gamepad *pad) {
            return SDL_GetGamepadID(pad) == event.gdevice.which;
        });
        if (gone != gamepads_.end()) {
            SDL_CloseGamepad(*gone);
            gamepads_.erase(gone);
        }
    }
}
InputState map_input(const ControlSettings &settings, std::span<const bool> keys,
                     std::span<const GamepadState> gamepads) {
    std::array<float, kActionCount> values{};
    for (std::size_t i = 0; i < kActionCount; ++i) {
        for (const auto &binding : settings.keyboard[i])
            if (binding.code >= 0 && static_cast<std::size_t>(binding.code) < keys.size() && keys[binding.code])
                values[i] = 1.0f;
    }
    for (const auto &pad : gamepads) {
        // Chords first: the buttons of one held are not also single presses.
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> chorded{};
        for (std::size_t i = 0; i < kActionCount; ++i)
            for (const auto &binding : settings.gamepad[i])
                if (binding.kind == Binding::Kind::Button && binding.chord >= 0 && pad.buttons[binding.code] &&
                    pad.buttons[binding.chord]) {
                    values[i] = 1.0f;
                    chorded[binding.code] = chorded[binding.chord] = true;
                }
        for (std::size_t i = 0; i < kActionCount; ++i) {
            for (const auto &binding : settings.gamepad[i]) {
                float value = 0.0f;
                if (binding.kind == Binding::Kind::Button) {
                    if (binding.chord >= 0) continue;
                    value = pad.buttons[binding.code] && !chorded[binding.code] ? 1.0f : 0.0f;
                } else {
                    const auto raw = pad.axes[binding.code];
                    value = static_cast<float>(raw) / (raw < 0 ? 32768.0f : 32767.0f);
                    value *= static_cast<float>(binding.direction);
                    // Triggers already have a unipolar range; stick axes use a rescaled dead zone.
                    if (binding.code < SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                        value = (value - settings.dead_zone) / (1.0f - settings.dead_zone);
                }
                values[i] = std::max(values[i], std::clamp(value, 0.0f, 1.0f));
            }
        }
    }
    InputState result;
    using namespace ctrl;
    constexpr std::uint32_t buttons[] = {kButtonUp, kButtonDown, kButtonLeft, kButtonRight,
        kButtonCircle, kButtonCross, kButtonSquare, kButtonTriangle, kButtonL, kButtonR, kButtonStart, kButtonSelect};
    for (std::size_t i = 0; i < std::size(buttons); ++i) if (values[i] > 0.5f) result.pad.buttons |= buttons[i];
    const auto value = [&](Action action) { return values[static_cast<std::size_t>(action)]; };
    const auto axis = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(128.0f + value * 127.5f), 0l, 255l));
    };
    result.pad.lx = axis(value(Action::AnalogRight) - value(Action::AnalogLeft));
    result.pad.ly = axis(value(Action::AnalogDown) - value(Action::AnalogUp));
    result.camera_x = value(Action::CameraRight) - value(Action::CameraLeft);
    result.camera_y = value(Action::CameraDown) - value(Action::CameraUp);
    // Aim uses the physical right stick's screen directions independently of
    // camera orbit inversion/bindings. Take both axes from the same gamepad.
    for (const auto &pad : gamepads) {
        const auto stick = [&](int axis) {
            const auto raw = pad.axes[axis];
            const float value = static_cast<float>(raw) / (raw < 0 ? 32768.0f : 32767.0f);
            const float magnitude = std::clamp((std::abs(value) - settings.dead_zone) / (1.0f - settings.dead_zone), 0.0f, 1.0f);
            return std::copysign(magnitude, value);
        };
        const float x = stick(SDL_GAMEPAD_AXIS_RIGHTX), y = stick(SDL_GAMEPAD_AXIS_RIGHTY);
        if (x * x + y * y > result.aim_x * result.aim_x + result.aim_y * result.aim_y) {
            result.aim_x = x;
            result.aim_y = y;
        }
    }
    result.camera_reset = value(Action::CameraReset) > 0.5f;
    result.fast_forward = value(Action::FastForward) > 0.5f;
    result.menu = value(Action::Menu) > 0.5f;
    result.save_state = value(Action::SaveState) > 0.5f;
    result.load_state = value(Action::LoadState) > 0.5f;
    return result;
}
void apply_aiming(InputState &state, bool aiming, float sensitivity) {
    if (!aiming) return;
    float x = (static_cast<float>(state.pad.lx) - 128.0f) / 127.5f;
    float y = (static_cast<float>(state.pad.ly) - 128.0f) / 127.5f;
    if (state.aim_x * state.aim_x + state.aim_y * state.aim_y > x * x + y * y) {
        x = state.aim_x;
        y = state.aim_y;
    }
    const float gain = std::isfinite(sensitivity) ? std::clamp(sensitivity, 0.25f, 2.0f) : 1.0f;
    const auto axis = [gain](float value) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(128.0f + value * gain * 127.5f), 0l, 255l));
    };
    state.pad.lx = axis(x);
    state.pad.ly = axis(y);
    state.camera_x = state.camera_y = 0.0f;
    state.camera_reset = false;
}

InputState Input::sample() const {
    int key_count = 0;
    const bool *keys = SDL_GetKeyboardState(&key_count);
    std::vector<GamepadState> pads(gamepads_.size());
    for (std::size_t i = 0; i < gamepads_.size(); ++i) {
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            pads[i].buttons[button] = SDL_GetGamepadButton(gamepads_[i], static_cast<SDL_GamepadButton>(button));
        for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
            pads[i].axes[axis] = SDL_GetGamepadAxis(gamepads_[i], static_cast<SDL_GamepadAxis>(axis));
    }
    return map_input(settings_, {keys, static_cast<std::size_t>(key_count)}, pads);
}
} // namespace sarusaru::shell
