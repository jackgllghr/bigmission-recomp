#pragma once

// SARUSARU_UI_SCRIPT drives the port's screens without a person, to check
// every device's path through them and capture what they show. Commands are
// separated by ';' and run one after another, a frame apart:
//
//   wait SECONDS          let the screens run
//   key NAME              press and release a key (SDL key name: Return, Down, Escape, Q)
//   pad BUTTON            press and release a button of a virtual gamepad
//                         (SDL names: south, east, dpdown, leftshoulder...)
//   padtype ps|xbox|switch  what the virtual gamepad says it is (before its first press)
//   move X Y              move the mouse; X and Y are fractions of the window
//   click X Y             move the mouse there and click
//   tap X Y               touch there briefly with a finger
//   drag X1 Y1 X2 Y2      drag a finger from one point to the other
//   wheel STEPS           turn the mouse wheel (positive scrolls up)
//   drop PATH             drop a file on the window
//   capture FILE          save the next picture as a BMP
//   quit                  close the window
//
// Pointer input goes straight to Dear ImGui (the SDL backend would otherwise
// replace it with the real pointer's position); keys go through SDL's event
// queue and buttons through an SDL virtual gamepad, as a person's would.
#include "ui/layer.hpp"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sarusaru::ui {

class UiScript {
public:
    // Throws std::runtime_error for a malformed script.
    explicit UiScript(std::string_view text);
    ~UiScript();
    UiScript(const UiScript &) = delete;
    UiScript &operator=(const UiScript &) = delete;

    // The script in SARUSARU_UI_SCRIPT, if set.
    static std::optional<std::string> from_environment();

    // Called by the layer at the start of every frame.
    void tick(Layer &layer);
    [[nodiscard]] bool finished() const noexcept { return next_ >= commands_.size() && pending_.empty(); }

private:
    struct Command {
        std::string verb;
        std::vector<std::string> arguments;
    };
    std::vector<Command> commands_;
    std::size_t next_{};
    float wait_until_{};
    // Steps of the current command still to send, one per frame.
    std::vector<std::function<void(Layer &)>> pending_;
    std::optional<ImVec2> pointer_;
    bool touch_{};
    std::string pad_type_{"ps"};
    SDL_JoystickID pad_id_{};
    SDL_Joystick *pad_{};

    void start(const Command &command, Layer &layer);
    void open_pad();
};

} // namespace sarusaru::ui
