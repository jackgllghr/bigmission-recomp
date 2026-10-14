#pragma once

// The bindings screen's logic, without a window: editing an action's keys or
// gamepad inputs in the settings, describing each input as a glyph, and
// capturing the next input the player presses.
#include "shell/settings.hpp"
#include "ui/text.hpp"

#include <SDL3/SDL.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sarusaru::ui {

enum class BindingDevice { Keyboard, Gamepad };

// The action's name, as the bindings screen lists it.
[[nodiscard]] Text action_text(shell::Action action);

[[nodiscard]] const std::vector<shell::Binding> &bindings_of(const shell::ControlSettings &controls,
                                                             BindingDevice device, shell::Action action);
// The captured input becomes the action's only one.
void set_binding(shell::ControlSettings &controls, BindingDevice device, shell::Action action, shell::Binding binding);
// The captured input joins the action's others (once).
void add_binding(shell::ControlSettings &controls, BindingDevice device, shell::Action action, shell::Binding binding);
void clear_bindings(shell::ControlSettings &controls, BindingDevice device, shell::Action action);
// That device's bindings back to the defaults; the other device's and the
// dead zone are kept.
void reset_bindings(shell::ControlSettings &controls, BindingDevice device);
// The other actions the input also triggers on that device (allowed: one
// input may trigger several actions on purpose).
[[nodiscard]] std::vector<shell::Action> sharing(const shell::ControlSettings &controls, BindingDevice device,
                                                 shell::Action action, const shell::Binding &binding);

// How an input is drawn: a keyboard key, a face button (drawn with the
// pad's own labels: shapes or letters), or a pill with a short name.
struct Glyph {
    enum class Kind { Key, Face, Pill };
    Kind kind{Kind::Key};
    std::string label;  // for Key and Pill
    SDL_GamepadButton button{SDL_GAMEPAD_BUTTON_INVALID};  // for Face
};
[[nodiscard]] Glyph glyph(const shell::Binding &binding, SDL_GamepadType type);

// Capturing the next input for a binding. Fed the window's input with times
// in seconds; the caller applies the result.
//
// Keyboard: the next key pressed (key repeats ignored); Esc cancels, so Esc
// itself is bound only in settings.ini.
// Gamepad: the next button binds when it is released, so every button can
// be bound, including the one that backs out of menus; holding that one
// for kHoldToCancel cancels instead. A stick or trigger binds in the
// direction pushed past kPress, once it returns below kRest; an axis already
// pushed when capture starts must first return. kTimeout without a press, or
// Esc, cancels.
// For either, a click or tap cancels.
class BindingCapture {
public:
    static constexpr double kTimeout = 6.0;
    static constexpr double kHoldToCancel = 1.0;
    static constexpr float kPress = 0.6f;
    static constexpr float kRest = 0.3f;
    enum class Outcome { Idle, Waiting, Bound, Cancelled };

    // `axes` are the gamepads' current axis values (-1..1), for the rule
    // that a pushed axis must return first.
    void start(BindingDevice device, double now, SDL_GamepadButton back, std::span<const float> axes = {});
    [[nodiscard]] bool active() const noexcept { return outcome_ == Outcome::Waiting; }
    [[nodiscard]] BindingDevice device() const noexcept { return device_; }

    void key(SDL_Scancode scancode, bool repeat);
    void button(SDL_GamepadButton button, bool down, double now);
    void axis(SDL_GamepadAxis axis, float value);
    void pointer();
    void update(double now);

    [[nodiscard]] Outcome outcome() const noexcept { return outcome_; }
    // The bound input, once; then the capture is idle again. Also returns
    // to idle after a cancel.
    std::optional<shell::Binding> take();

    // 0 to 1: how long the back button has been held towards cancelling.
    [[nodiscard]] float cancel_progress(double now) const;
    [[nodiscard]] int seconds_left(double now) const;
    // A gamepad button is held: the countdown stops.
    [[nodiscard]] bool pressing() const noexcept { return pending_button_ != SDL_GAMEPAD_BUTTON_INVALID || pending_axis_; }

private:
    Outcome outcome_{Outcome::Idle};
    BindingDevice device_{BindingDevice::Keyboard};
    SDL_GamepadButton back_{SDL_GAMEPAD_BUTTON_EAST};
    double started_{};
    SDL_GamepadButton pending_button_{SDL_GAMEPAD_BUTTON_INVALID};
    double pressed_at_{};
    std::optional<shell::Binding> pending_axis_;
    bool armed_[SDL_GAMEPAD_AXIS_COUNT]{};
    std::optional<shell::Binding> result_;

    void finish(std::optional<shell::Binding> result);
};

} // namespace sarusaru::ui
