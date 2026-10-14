#pragma once

// Host keyboard and gamepads to the PSP pad.
//
// Keyboard: arrows the d-pad, Z circle, X cross, A square, S triangle, Q/W
// the shoulders, Enter start, Backspace select; I/J/K/L the analog stick.
// Gamepads map by position: east circle, south cross, west square, north
// triangle, the left stick the analog stick. (Rebinding comes with settings.)
#include "ctrl/controller.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace sarusaru::shell {

class Input {
public:
    ~Input();
    // Gamepads coming and going.
    void handle(const SDL_Event &event);
    [[nodiscard]] ctrl::Controller::Pad pad() const;

private:
    std::vector<SDL_Gamepad *> gamepads_;
};

} // namespace sarusaru::shell
