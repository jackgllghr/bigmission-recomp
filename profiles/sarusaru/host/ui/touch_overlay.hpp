#pragma once

// The on-screen controls (shell/touch_controls.hpp) as drawn over the running
// game: translucent, with what is held lit and each stick's knob where its
// finger pushes it.
#include "shell/touch_controls.hpp"

#include "imgui.h"

namespace sarusaru::ui {

// Into the foreground draw list of the current frame (Layer::present_over),
// over the whole window of `display` points, with the face buttons and the
// rest labelled as `type`'s (Xbox letters, Nintendo letters or PlayStation
// symbols, which are the PSP's own).
void draw_touch_controls(const shell::TouchPad &held, SDL_GamepadType type, ImVec2 display);

} // namespace sarusaru::ui
