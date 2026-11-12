#pragma once

// The on-screen controls for a touch screen without a gamepad (Android): two
// sticks, a d-pad, the four face buttons, L and R, Start, Select and a menu
// button that opens the pause menu. Each finger takes the control it first
// touches and keeps it until lifted: a stick follows the finger anywhere, and
// the d-pad and face buttons follow it across their own directions/buttons.
//
// Pure logic in window points (no window or renderer needed), so tests can
// press it; ui/touch_overlay.hpp draws it from the same layout.
#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace sarusaru::shell {

struct TouchLayout {
    struct Circle { float x{}, y{}, r{}; };
    struct Rect { float x0{}, y0{}, x1{}, y1{}; };
    // Sticks: `r` is the base's radius, which is also the knob's full travel.
    Circle left_stick, right_stick;
    // The d-pad's arms reach `r` from its centre.
    Circle dpad;
    // The face buttons sit `face_offset` from `face`'s centre, each of radius `face.r`.
    Circle face;
    float face_offset{};
    Rect l, r, start, select;
    Circle menu;
};
// Laid out for a landscape window of `width` x `height` points, sized from
// the shorter side so a phone's and a tablet's controls are the same size
// relative to the thumb.
[[nodiscard]] TouchLayout touch_layout(float width, float height);

// What the controls hold: PSP buttons (ctrl::kButton*), each stick from -1
// to 1 with +y downward, and the menu button.
struct TouchPad {
    std::uint32_t buttons{};
    float lx{}, ly{}, rx{}, ry{};
    bool menu{};
    bool operator==(const TouchPad &) const = default;
};

enum class TouchControl { None, LeftStick, RightStick, DPad, Face, L, R, Start, Select, Menu };

class TouchControls {
public:
    // Finger events (others are ignored), with the window's size in points.
    void handle(const SDL_Event &event, float width, float height);
    void press(SDL_FingerID finger, float x, float y, float width, float height);
    void move(SDL_FingerID finger, float x, float y);
    void release(SDL_FingerID finger);
    // Forgets every finger: the pause menu took the touches meanwhile, so a
    // lift during it never reaches here.
    void clear() noexcept { fingers_.clear(); }
    [[nodiscard]] TouchPad state() const;
    [[nodiscard]] bool touched() const noexcept { return !fingers_.empty(); }

private:
    struct Finger {
        SDL_FingerID id;
        TouchControl control;
        float x, y;
    };
    std::vector<Finger> fingers_;
    TouchLayout layout_{};
};

} // namespace sarusaru::shell
