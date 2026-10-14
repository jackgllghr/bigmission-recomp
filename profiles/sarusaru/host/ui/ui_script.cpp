#include "ui/ui_script.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace sarusaru::ui {
namespace {

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

float number(const std::string &text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    float value{};
    if (!(in >> value) || in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("SARUSARU_UI_SCRIPT: expected a number, not " + text);
    return value;
}

void push_key(SDL_Window *window, SDL_Scancode scancode, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.scancode = scancode;
    event.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    event.key.down = down;
    SDL_PushEvent(&event);
}

} // namespace

std::optional<std::string> UiScript::from_environment() {
    const char *text = std::getenv("SARUSARU_UI_SCRIPT");
    if (text == nullptr || *text == '\0') return std::nullopt;
    return std::string(text);
}

UiScript::UiScript(std::string_view text) {
    constexpr std::pair<std::string_view, std::size_t> kVerbs[] = {
        {"wait", 1}, {"key", 1}, {"pad", 1}, {"padhold", 2}, {"axis", 2}, {"padtype", 1}, {"move", 2}, {"click", 2}, {"tap", 2},
        {"drag", 4}, {"wheel", 1}, {"drop", 1}, {"capture", 1}, {"quit", 0}};
    while (!text.empty()) {
        const auto end = text.find(';');
        const auto part = trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (part.empty()) continue;
        Command command;
        const auto space = part.find(' ');
        command.verb = std::string(part.substr(0, space));
        // drop and capture take the rest as a path, which may have spaces.
        if (space != part.npos) {
            const auto rest = trim(part.substr(space + 1));
            if (command.verb == "drop" || command.verb == "capture") {
                command.arguments.emplace_back(rest);
            } else {
                std::istringstream words{std::string(rest)};
                for (std::string word; words >> word;) command.arguments.push_back(word);
            }
        }
        bool known = false;
        for (const auto &[verb, count] : kVerbs)
            if (verb == command.verb) {
                known = true;
                if (command.arguments.size() != count)
                    throw std::runtime_error("SARUSARU_UI_SCRIPT: " + command.verb + " takes " + std::to_string(count) +
                                             " argument(s)");
            }
        if (!known) throw std::runtime_error("SARUSARU_UI_SCRIPT: unknown command " + command.verb);
        for (const auto &argument : command.arguments)
            if (command.verb != "key" && command.verb != "pad" && command.verb != "padtype" && command.verb != "drop" &&
                command.verb != "capture" && command.verb != "padhold" && command.verb != "axis")
                (void)number(argument);
        if (command.verb == "padhold" || command.verb == "axis") (void)number(command.arguments[1]);
        commands_.push_back(std::move(command));
    }
}

UiScript::~UiScript() {
    if (pad_ != nullptr) SDL_CloseJoystick(pad_);
    if (pad_id_ != 0) SDL_DetachVirtualJoystick(pad_id_);
}

void UiScript::open_pad() {
    if (pad_ != nullptr) return;
    // A scripted run may not have the window's focus, and SDL otherwise
    // drops gamepad input then.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    // Vendor and product make SDL name its type: a DualSense, an Xbox
    // Series pad or a Switch Pro Controller.
    if (pad_type_ == "xbox") {
        desc.vendor_id = 0x045E;
        desc.product_id = 0x0B12;
    } else if (pad_type_ == "switch") {
        desc.vendor_id = 0x057E;
        desc.product_id = 0x2009;
    } else {
        desc.vendor_id = 0x054C;
        desc.product_id = 0x0CE6;
    }
    desc.name = "SARUSARU_UI_SCRIPT gamepad";
    pad_id_ = SDL_AttachVirtualJoystick(&desc);
    if (pad_id_ == 0) throw std::runtime_error(std::string("SARUSARU_UI_SCRIPT: no virtual gamepad: ") + SDL_GetError());
    pad_ = SDL_OpenJoystick(pad_id_);
}

void UiScript::start(const Command &command, Layer &layer) {
    const auto &a = command.arguments;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto at = [&](std::size_t index) { return ImVec2(number(a[index]) * display.x, number(a[index + 1]) * display.y); };
    if (command.verb == "wait") {
        wait_until_ = layer.time() + number(a[0]);
    } else if (command.verb == "key") {
        const SDL_Scancode scancode = SDL_GetScancodeFromName(a[0].c_str());
        if (scancode == SDL_SCANCODE_UNKNOWN) throw std::runtime_error("SARUSARU_UI_SCRIPT: unknown key " + a[0]);
        push_key(layer.window(), scancode, true);
        pending_.push_back([scancode](Layer &l) { push_key(l.window(), scancode, false); });
    } else if (command.verb == "padtype") {
        pad_type_ = a[0];
    } else if (command.verb == "axis") {
        const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(a[0].c_str());
        if (axis == SDL_GAMEPAD_AXIS_INVALID) throw std::runtime_error("SARUSARU_UI_SCRIPT: unknown axis " + a[0]);
        open_pad();
        const float value = std::clamp(number(a[1]), -1.0f, 1.0f);
        SDL_SetJoystickVirtualAxis(pad_, axis, static_cast<Sint16>(value * 32767.0f));
    } else if (command.verb == "pad" || command.verb == "padhold") {
        // Face buttons by position as well as by SDL's names (a, b, x, y).
        const std::string name = a[0] == "south" ? "a" : a[0] == "east" ? "b" : a[0] == "west" ? "x" : a[0] == "north" ? "y" : a[0];
        const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
        if (button == SDL_GAMEPAD_BUTTON_INVALID) throw std::runtime_error("SARUSARU_UI_SCRIPT: unknown button " + a[0]);
        open_pad();
        SDL_SetJoystickVirtualButton(pad_, button, true);
        if (command.verb == "padhold") {
            // Released by the next command, after the hold.
            wait_until_ = layer.time() + number(a[1]);
            release_ = button;
        } else {
            // Held for a few frames, as a real press is.
            pending_.push_back([](Layer &) {});
            pending_.push_back([this, button](Layer &) { SDL_SetJoystickVirtualButton(pad_, button, false); });
        }
    } else if (command.verb == "move" || command.verb == "click" || command.verb == "tap") {
        touch_ = command.verb == "tap";
        pointer_ = at(0);
        layer.set_device(touch_ ? Device::Touch : Device::Mouse);
        if (command.verb != "move") {
            pending_.push_back([](Layer &) { ImGui::GetIO().AddMouseButtonEvent(0, true); });
            pending_.push_back([](Layer &) {});
            pending_.push_back([](Layer &) { ImGui::GetIO().AddMouseButtonEvent(0, false); });
        }
    } else if (command.verb == "drag") {
        touch_ = true;
        layer.set_device(Device::Touch);
        const ImVec2 from = at(0), to = at(2);
        pointer_ = from;
        pending_.push_back([](Layer &) { ImGui::GetIO().AddMouseButtonEvent(0, true); });
        constexpr int kSteps = 20;
        for (int i = 1; i <= kSteps; ++i) {
            const float f = static_cast<float>(i) / kSteps;
            pending_.push_back([this, from, to, f](Layer &) {
                pointer_ = ImVec2(from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f);
            });
        }
        pending_.push_back([](Layer &) { ImGui::GetIO().AddMouseButtonEvent(0, false); });
    } else if (command.verb == "wheel") {
        layer.set_device(Device::Mouse);
        ImGui::GetIO().AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        ImGui::GetIO().AddMouseWheelEvent(0.0f, number(a[0]));
    } else if (command.verb == "drop") {
        SDL_Event event{};
        event.type = SDL_EVENT_DROP_FILE;
        event.drop.windowID = SDL_GetWindowID(layer.window());
        // Left allocated: SDL does not free the data of events pushed by hand.
        event.drop.data = SDL_strdup(a[0].c_str());
        SDL_PushEvent(&event);
    } else if (command.verb == "capture") {
        layer.capture_next(std::filesystem::path(std::u8string(a[0].begin(), a[0].end())));
    } else if (command.verb == "quit") {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }
}

void UiScript::tick(Layer &layer) {
    ImGuiIO &io = ImGui::GetIO();
    if (pointer_) {
        // Again every frame, after the backend's own idea of the pointer.
        io.AddMouseSourceEvent(touch_ ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(pointer_->x, pointer_->y);
    }
    if (!pending_.empty()) {
        auto step = std::move(pending_.front());
        pending_.erase(pending_.begin());
        step(layer);
        if (touch_ && pending_.empty() && pointer_) {
            // A lifted finger leaves no pointer behind.
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            pointer_.reset();
        }
        return;
    }
    if (layer.time() < wait_until_) return;
    if (release_ != SDL_GAMEPAD_BUTTON_INVALID) {
        SDL_SetJoystickVirtualButton(pad_, release_, false);
        release_ = SDL_GAMEPAD_BUTTON_INVALID;
        return;
    }
    if (next_ < commands_.size()) start(commands_[next_++], layer);
}

} // namespace sarusaru::ui
