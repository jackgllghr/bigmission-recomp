#include "ui/bindings.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace sarusaru::ui {
namespace {

using shell::Action;
using shell::Binding;

std::vector<Binding> &slot(shell::ControlSettings &controls, BindingDevice device, Action action) {
    auto &table = device == BindingDevice::Keyboard ? controls.keyboard : controls.gamepad;
    return table[static_cast<std::size_t>(action)];
}

enum class Family { PlayStation, Xbox, Nintendo, Other };
Family family(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return Family::PlayStation;
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return Family::Xbox;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return Family::Nintendo;
    default: return Family::Other;
    }
}

// Picks the PlayStation, Xbox, Nintendo or other name.
std::string by_family(SDL_GamepadType type, const char *ps, const char *xbox, const char *nintendo, const char *other) {
    switch (family(type)) {
    case Family::PlayStation: return ps;
    case Family::Xbox: return xbox;
    case Family::Nintendo: return nintendo;
    case Family::Other: break;
    }
    return other;
}

std::string key_label(int code) {
    const std::string_view name = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
    constexpr std::pair<std::string_view, std::string_view> kShort[] = {
        {"Return", "Enter"}, {"Escape", "Esc"}, {"Up", "↑"}, {"Down", "↓"}, {"Left", "←"},
        {"Right", "→"}};
    for (const auto &[from, to] : kShort)
        if (name == from) return std::string(to);
    return name.empty() ? "#" + std::to_string(code) : std::string(name);
}

std::string button_label(SDL_GamepadButton button, SDL_GamepadType type) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return by_family(type, "×", "A", "B", "A");
    case SDL_GAMEPAD_BUTTON_EAST: return by_family(type, "○", "B", "A", "B");
    case SDL_GAMEPAD_BUTTON_WEST: return by_family(type, "□", "X", "Y", "X");
    case SDL_GAMEPAD_BUTTON_NORTH: return by_family(type, "△", "Y", "X", "Y");
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D↑";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D↓";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D←";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D→";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return by_family(type, "L1", "LB", "L", "L1");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return by_family(type, "R1", "RB", "R", "R1");
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return by_family(type, "L3", "LS", "L●", "L3");
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return by_family(type, "R3", "RS", "R●", "R3");
    case SDL_GAMEPAD_BUTTON_START:
        return type == SDL_GAMEPAD_TYPE_PS3 ? "START" : by_family(type, "OPTIONS", "MENU", "+", "START");
    case SDL_GAMEPAD_BUTTON_BACK:
        return type == SDL_GAMEPAD_TYPE_PS3 ? "SELECT"
             : type == SDL_GAMEPAD_TYPE_PS4 ? "SHARE"
                                            : by_family(type, "CREATE", "VIEW", "−", "SELECT");
    case SDL_GAMEPAD_BUTTON_GUIDE: return "HOME";
    case SDL_GAMEPAD_BUTTON_MISC1: return "MISC";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "P1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "P2";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "P3";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "P4";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "TOUCHPAD";
    default: break;
    }
    const char *name = SDL_GetGamepadStringForButton(button);
    return name != nullptr ? name : "?";
}

std::string axis_label(SDL_GamepadAxis axis, int direction, SDL_GamepadType type) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: return direction < 0 ? "LS←" : "LS→";
    case SDL_GAMEPAD_AXIS_LEFTY: return direction < 0 ? "LS↑" : "LS↓";
    case SDL_GAMEPAD_AXIS_RIGHTX: return direction < 0 ? "RS←" : "RS→";
    case SDL_GAMEPAD_AXIS_RIGHTY: return direction < 0 ? "RS↑" : "RS↓";
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return by_family(type, "L2", "LT", "ZL", "L2");
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return by_family(type, "R2", "RT", "ZR", "R2");
    default: return "?";
    }
}

} // namespace

Text action_text(shell::Action action) {
    switch (action) {
    case Action::Up: return Text::ActionUp;
    case Action::Down: return Text::ActionDown;
    case Action::Left: return Text::ActionLeft;
    case Action::Right: return Text::ActionRight;
    case Action::Circle: return Text::ActionCircle;
    case Action::Cross: return Text::ActionCross;
    case Action::Square: return Text::ActionSquare;
    case Action::Triangle: return Text::ActionTriangle;
    case Action::L: return Text::ActionL;
    case Action::R: return Text::ActionR;
    case Action::Start: return Text::ActionStart;
    case Action::Select: return Text::ActionSelect;
    case Action::AnalogLeft: return Text::ActionAnalogLeft;
    case Action::AnalogRight: return Text::ActionAnalogRight;
    case Action::AnalogUp: return Text::ActionAnalogUp;
    case Action::AnalogDown: return Text::ActionAnalogDown;
    case Action::CameraLeft: return Text::ActionCameraLeft;
    case Action::CameraRight: return Text::ActionCameraRight;
    case Action::CameraUp: return Text::ActionCameraUp;
    case Action::CameraDown: return Text::ActionCameraDown;
    case Action::CameraReset: return Text::ActionCameraReset;
    case Action::FastForward: return Text::ActionFastForward;
    case Action::Menu:
    case Action::Count: break;
    }
    return Text::ActionMenu;
}

const std::vector<Binding> &bindings_of(const shell::ControlSettings &controls, BindingDevice device, Action action) {
    const auto &table = device == BindingDevice::Keyboard ? controls.keyboard : controls.gamepad;
    return table[static_cast<std::size_t>(action)];
}

void set_binding(shell::ControlSettings &controls, BindingDevice device, Action action, Binding binding) {
    slot(controls, device, action) = {binding};
}

void add_binding(shell::ControlSettings &controls, BindingDevice device, Action action, Binding binding) {
    auto &bindings = slot(controls, device, action);
    if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end()) bindings.push_back(binding);
}

void clear_bindings(shell::ControlSettings &controls, BindingDevice device, Action action) {
    slot(controls, device, action).clear();
}

void reset_bindings(shell::ControlSettings &controls, BindingDevice device) {
    const auto defaults = shell::default_settings().controls;
    if (device == BindingDevice::Keyboard)
        controls.keyboard = defaults.keyboard;
    else
        controls.gamepad = defaults.gamepad;
}

std::vector<Action> sharing(const shell::ControlSettings &controls, BindingDevice device, Action action,
                            const Binding &binding) {
    std::vector<Action> others;
    for (std::size_t i = 0; i < shell::kActionCount; ++i) {
        const auto other = static_cast<Action>(i);
        if (other == action) continue;
        const auto &bindings = bindings_of(controls, device, other);
        if (std::find(bindings.begin(), bindings.end(), binding) != bindings.end()) others.push_back(other);
    }
    return others;
}

Glyph glyph(const Binding &binding, SDL_GamepadType type) {
    Glyph out;
    switch (binding.kind) {
    case Binding::Kind::Key:
        out.kind = Glyph::Kind::Key;
        out.label = key_label(binding.code);
        break;
    case Binding::Kind::Button: {
        const auto button = static_cast<SDL_GamepadButton>(binding.code);
        if (binding.chord >= 0) {
            out.kind = Glyph::Kind::Pill;
            out.label = button_label(button, type) + "+" + button_label(static_cast<SDL_GamepadButton>(binding.chord), type);
        } else if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_WEST ||
            button == SDL_GAMEPAD_BUTTON_NORTH) {
            out.kind = Glyph::Kind::Face;
            out.button = button;
        } else {
            out.kind = Glyph::Kind::Pill;
            out.label = button_label(button, type);
        }
        break;
    }
    case Binding::Kind::Axis:
        out.kind = Glyph::Kind::Pill;
        out.label = axis_label(static_cast<SDL_GamepadAxis>(binding.code), binding.direction, type);
        break;
    }
    return out;
}

// --- Capture -------------------------------------------------------------------

void BindingCapture::start(BindingDevice device, double now, SDL_GamepadButton back, std::span<const float> axes) {
    outcome_ = Outcome::Waiting;
    device_ = device;
    back_ = back;
    started_ = now;
    pending_button_ = SDL_GAMEPAD_BUTTON_INVALID;
    pending_axis_.reset();
    result_.reset();
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
        armed_[i] = static_cast<std::size_t>(i) >= axes.size() || std::abs(axes[static_cast<std::size_t>(i)]) < kRest;
}

void BindingCapture::finish(std::optional<Binding> result) {
    result_ = result;
    outcome_ = result ? Outcome::Bound : Outcome::Cancelled;
    pending_button_ = SDL_GAMEPAD_BUTTON_INVALID;
    pending_axis_.reset();
}

void BindingCapture::key(SDL_Scancode scancode, bool repeat) {
    if (!active() || repeat) return;
    if (scancode == SDL_SCANCODE_ESCAPE) {
        finish(std::nullopt);
        return;
    }
    if (device_ == BindingDevice::Keyboard) finish(Binding{Binding::Kind::Key, static_cast<int>(scancode)});
}

void BindingCapture::button(SDL_GamepadButton button, bool down, double now) {
    if (!active() || device_ != BindingDevice::Gamepad) return;
    if (down) {
        // The first press counts; a release of a button held before the
        // capture started (the one that started it) is ignored below.
        if (pending_button_ == SDL_GAMEPAD_BUTTON_INVALID && !pending_axis_) {
            pending_button_ = button;
            pressed_at_ = now;
        }
        return;
    }
    if (button != pending_button_) return;
    if (button == back_ && now - pressed_at_ >= kHoldToCancel) {
        finish(std::nullopt);
        return;
    }
    finish(Binding{Binding::Kind::Button, static_cast<int>(button)});
}

void BindingCapture::axis(SDL_GamepadAxis axis, float value) {
    if (!active() || device_ != BindingDevice::Gamepad || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return;
    const float magnitude = std::abs(value);
    if (pending_axis_ && pending_axis_->code == axis) {
        if (magnitude < kRest) finish(*pending_axis_);
        return;
    }
    if (!armed_[axis]) {
        armed_[axis] = magnitude < kRest;
        return;
    }
    if (magnitude >= kPress && pending_button_ == SDL_GAMEPAD_BUTTON_INVALID && !pending_axis_)
        pending_axis_ = Binding{Binding::Kind::Axis, static_cast<int>(axis), value < 0.0f ? -1 : 1};
}

void BindingCapture::pointer() {
    if (active()) finish(std::nullopt);
}

void BindingCapture::update(double now) {
    if (!active() || device_ != BindingDevice::Gamepad) return;
    if (pending_button_ == back_ && now - pressed_at_ >= kHoldToCancel) {
        finish(std::nullopt);
        return;
    }
    if (!pressing() && now - started_ >= kTimeout) finish(std::nullopt);
}

std::optional<Binding> BindingCapture::take() {
    if (outcome_ != Outcome::Bound && outcome_ != Outcome::Cancelled) return std::nullopt;
    outcome_ = Outcome::Idle;
    auto result = result_;
    result_.reset();
    return result;
}

float BindingCapture::cancel_progress(double now) const {
    if (!active() || pending_button_ != back_) return 0.0f;
    return static_cast<float>(std::clamp((now - pressed_at_) / kHoldToCancel, 0.0, 1.0));
}

int BindingCapture::seconds_left(double now) const {
    return std::max(0, static_cast<int>(std::ceil(kTimeout - (now - started_))));
}

} // namespace sarusaru::ui
