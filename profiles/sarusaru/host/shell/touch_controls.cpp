#include "shell/touch_controls.hpp"

#include "ctrl/controller.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sarusaru::shell {
namespace {
// How far past a control a touch still takes it, as a multiple of its size:
// thumbs land loosely, and the nearest control wins where two reach.
constexpr float kStickReach = 1.7f, kDPadReach = 1.5f, kFaceReach = 1.3f, kMenuReach = 1.4f;
// Rectangles' margin, as a fraction of the shorter side.
constexpr float kRectMargin = 0.02f;
// The centre of the d-pad and face buttons presses nothing, and a stick's
// knob moves this fraction of its travel before it counts.
constexpr float kCentre = 0.25f, kStickDeadZone = 0.08f;

bool inside(const TouchLayout::Rect &rect, float x, float y, float margin) {
    return x >= rect.x0 - margin && x <= rect.x1 + margin && y >= rect.y0 - margin && y <= rect.y1 + margin;
}

// Which of up/right/down/left (bits 0-3) a direction from a centre holds:
// one within `half` radians of its axis, else the two it is between.
unsigned directions(float dx, float dy, float half) {
    constexpr float quarter = std::numbers::pi_v<float> / 2.0f;
    // Clockwise from up, as the screen's y points down.
    float angle = std::atan2(dx, -dy);
    if (angle < 0.0f) angle += 4.0f * quarter;
    const long axis = std::lround(angle / quarter);
    const int nearest = static_cast<int>(axis % 4);
    // From -45 to 45 degrees off the nearest axis.
    const float off = angle - static_cast<float>(axis) * quarter;
    if (std::abs(off) <= half) return 1u << nearest;
    const int other = (nearest + (off > 0.0f ? 1 : 3)) % 4;
    return (1u << nearest) | (1u << other);
}
} // namespace

TouchLayout touch_layout(float width, float height) {
    const float s = std::min(width, height);
    TouchLayout layout;
    layout.l = {0.04f * s, 0.04f * s, 0.30f * s, 0.14f * s};
    layout.r = {width - 0.30f * s, 0.04f * s, width - 0.04f * s, 0.14f * s};
    layout.menu = {width / 2.0f, 0.09f * s, 0.05f * s};
    layout.dpad = {0.20f * s, 0.42f * s, 0.13f * s};
    layout.left_stick = {0.38f * s, 0.76f * s, 0.13f * s};
    layout.face = {width - 0.20f * s, 0.42f * s, 0.05f * s};
    layout.face_offset = 0.085f * s;
    layout.right_stick = {width - 0.38f * s, 0.76f * s, 0.13f * s};
    layout.select = {width / 2.0f - 0.175f * s, 0.885f * s, width / 2.0f - 0.025f * s, 0.955f * s};
    layout.start = {width / 2.0f + 0.025f * s, 0.885f * s, width / 2.0f + 0.175f * s, 0.955f * s};
    return layout;
}

void TouchControls::handle(const SDL_Event &event, float width, float height) {
    const auto &finger = event.tfinger;
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN: press(finger.fingerID, finger.x * width, finger.y * height, width, height); break;
    case SDL_EVENT_FINGER_MOTION: move(finger.fingerID, finger.x * width, finger.y * height); break;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: release(finger.fingerID); break;
    default: break;
    }
}

void TouchControls::press(SDL_FingerID id, float x, float y, float width, float height) {
    release(id);
    layout_ = touch_layout(width, height);
    const float margin = kRectMargin * std::min(width, height);
    TouchControl control = TouchControl::None;
    if (inside(layout_.l, x, y, margin)) control = TouchControl::L;
    else if (inside(layout_.r, x, y, margin)) control = TouchControl::R;
    else if (inside(layout_.start, x, y, margin)) control = TouchControl::Start;
    else if (inside(layout_.select, x, y, margin)) control = TouchControl::Select;
    else if (std::hypot(x - layout_.menu.x, y - layout_.menu.y) <= layout_.menu.r * kMenuReach) control = TouchControl::Menu;
    else {
        // The nearest of the round controls, by distance over its reach.
        struct Zone { TouchControl control; TouchLayout::Circle centre; float reach; };
        const Zone zones[] = {
            {TouchControl::LeftStick, layout_.left_stick, layout_.left_stick.r * kStickReach},
            {TouchControl::RightStick, layout_.right_stick, layout_.right_stick.r * kStickReach},
            {TouchControl::DPad, layout_.dpad, layout_.dpad.r * kDPadReach},
            {TouchControl::Face, layout_.face, (layout_.face_offset + layout_.face.r) * kFaceReach},
        };
        float best = 1.0f;
        for (const auto &zone : zones) {
            const float distance = std::hypot(x - zone.centre.x, y - zone.centre.y) / zone.reach;
            // A control another finger has is not taken twice.
            const bool taken = std::any_of(fingers_.begin(), fingers_.end(),
                [&](const Finger &held) { return held.control == zone.control; });
            if (distance <= best && !taken) {
                best = distance;
                control = zone.control;
            }
        }
    }
    if (control != TouchControl::None) fingers_.push_back({id, control, x, y});
}

void TouchControls::move(SDL_FingerID id, float x, float y) {
    for (auto &finger : fingers_)
        if (finger.id == id) {
            finger.x = x;
            finger.y = y;
        }
}

void TouchControls::release(SDL_FingerID id) {
    std::erase_if(fingers_, [id](const Finger &finger) { return finger.id == id; });
}

TouchPad TouchControls::state() const {
    using namespace ctrl;
    TouchPad pad;
    const auto stick = [](const TouchLayout::Circle &base, const Finger &finger, float &x, float &y) {
        float dx = (finger.x - base.x) / base.r, dy = (finger.y - base.y) / base.r;
        const float length = std::hypot(dx, dy);
        if (length <= kStickDeadZone) return;
        // Past the dead zone the knob's travel covers the whole range.
        const float scale = std::min(1.0f, (length - kStickDeadZone) / (1.0f - kStickDeadZone)) / length;
        x = dx * scale;
        y = dy * scale;
    };
    for (const auto &finger : fingers_) {
        switch (finger.control) {
        case TouchControl::LeftStick: stick(layout_.left_stick, finger, pad.lx, pad.ly); break;
        case TouchControl::RightStick: stick(layout_.right_stick, finger, pad.rx, pad.ry); break;
        case TouchControl::DPad: {
            const float dx = finger.x - layout_.dpad.x, dy = finger.y - layout_.dpad.y;
            if (std::hypot(dx, dy) < layout_.dpad.r * kCentre) break;
            // Eight equal directions: diagonals as easy to hold as the arms.
            const unsigned held = directions(dx, dy, std::numbers::pi_v<float> / 8.0f);
            constexpr std::uint32_t arms[] = {kButtonUp, kButtonRight, kButtonDown, kButtonLeft};
            for (unsigned i = 0; i < 4; ++i) if (held & (1u << i)) pad.buttons |= arms[i];
            break;
        }
        case TouchControl::Face: {
            const float dx = finger.x - layout_.face.x, dy = finger.y - layout_.face.y;
            if (std::hypot(dx, dy) < layout_.face_offset * kCentre * 2.0f) break;
            // Mostly one button; two only from between them.
            const unsigned held = directions(dx, dy, std::numbers::pi_v<float> / 6.0f);
            constexpr std::uint32_t faces[] = {kButtonTriangle, kButtonCircle, kButtonCross, kButtonSquare};
            for (unsigned i = 0; i < 4; ++i) if (held & (1u << i)) pad.buttons |= faces[i];
            break;
        }
        case TouchControl::L: pad.buttons |= kButtonL; break;
        case TouchControl::R: pad.buttons |= kButtonR; break;
        case TouchControl::Start: pad.buttons |= kButtonStart; break;
        case TouchControl::Select: pad.buttons |= kButtonSelect; break;
        case TouchControl::Menu: pad.menu = true; break;
        case TouchControl::None: break;
        }
    }
    return pad;
}

} // namespace sarusaru::shell
