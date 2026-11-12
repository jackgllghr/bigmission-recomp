#include "ui/touch_overlay.hpp"

#include "ctrl/controller.hpp"
#include "ui/theme.hpp"

#include <algorithm>
#include <cmath>

namespace sarusaru::ui {
namespace {
// Light enough to see the game through, dark enough to see over snow.
constexpr ImU32 kFill = IM_COL32(0, 0, 0, 70), kFillHeld = IM_COL32(255, 255, 255, 110);
constexpr ImU32 kLine = IM_COL32(255, 255, 255, 120), kMark = IM_COL32(255, 255, 255, 170);
// The symbols' own colours, softened.
constexpr ImU32 kTriangle = IM_COL32(90, 220, 170, 210), kCircle = IM_COL32(255, 110, 120, 210),
                kCross = IM_COL32(130, 175, 255, 210), kSquare = IM_COL32(240, 150, 220, 210);
constexpr ImU32 kXboxA = IM_COL32(130, 210, 100, 220), kXboxB = IM_COL32(245, 105, 95, 220),
                kXboxX = IM_COL32(100, 165, 245, 220), kXboxY = IM_COL32(250, 210, 70, 220);

bool is_xbox(SDL_GamepadType type) { return type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE; }
bool is_nintendo(SDL_GamepadType type) {
    return type >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && type <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 scaled(ImVec2 a, float s) { return {a.x * s, a.y * s}; }

// Centred on `centre`, shrunk to fit `width` if need be.
void label(ImDrawList *draw, const char *text, ImVec2 centre, float size, float width, ImU32 colour) {
    ImFont *font = fonts().strong != nullptr ? fonts().strong : ImGui::GetFont();
    ImVec2 extent = font->CalcTextSizeA(size, 1e6f, 0.0f, text);
    if (extent.x > width) {
        size *= width / extent.x;
        extent = font->CalcTextSizeA(size, 1e6f, 0.0f, text);
    }
    draw->AddText(font, size, {centre.x - extent.x / 2.0f, centre.y - extent.y / 2.0f}, colour, text);
}

void button_box(ImDrawList *draw, const shell::TouchLayout::Rect &rect, bool held, const char *text, float line) {
    const float rounding = (rect.y1 - rect.y0) / 2.0f;
    draw->AddRectFilled({rect.x0, rect.y0}, {rect.x1, rect.y1}, held ? kFillHeld : kFill, rounding);
    draw->AddRect({rect.x0, rect.y0}, {rect.x1, rect.y1}, kLine, rounding, line);
    label(draw, text, {(rect.x0 + rect.x1) / 2.0f, (rect.y0 + rect.y1) / 2.0f}, (rect.y1 - rect.y0) * 0.55f,
          (rect.x1 - rect.x0) * 0.75f, kMark);
}

void stick(ImDrawList *draw, const shell::TouchLayout::Circle &base, float x, float y, float line) {
    const ImVec2 centre{base.x, base.y};
    const bool pushed = x != 0.0f || y != 0.0f;
    draw->AddCircleFilled(centre, base.r, kFill, 48);
    draw->AddCircle(centre, base.r, kLine, 48, line);
    const float knob = base.r * 0.45f;
    const ImVec2 at = centre + scaled({x, y}, base.r - knob);
    draw->AddCircleFilled(at, knob, pushed ? kFillHeld : IM_COL32(255, 255, 255, 60), 32);
    draw->AddCircle(at, knob, kMark, 32, line);
}

void dpad(ImDrawList *draw, const shell::TouchLayout::Circle &pad, std::uint32_t buttons, float line) {
    using namespace ctrl;
    const float r = pad.r, w = r * 0.34f;
    const ImVec2 c{pad.x, pad.y};
    // Up, right, down, left: each arm, then its arrow.
    const ImVec2 axes[] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const std::uint32_t arms[] = {kButtonUp, kButtonRight, kButtonDown, kButtonLeft};
    ImVec2 outline[12];
    for (int i = 0; i < 4; ++i) {
        const ImVec2 out = axes[i], side{-out.y, out.x};
        // The arm's outer corners and the inner corner between it and the next.
        outline[i * 3 + 0] = c + scaled(out, r) + scaled(side, -w);
        outline[i * 3 + 1] = c + scaled(out, r) + scaled(side, w);
        outline[i * 3 + 2] = c + scaled(out, w) + scaled(side, w);
        // From the centre square out, so no two translucent fills overlap.
        const ImVec2 near = c + scaled(out, w), far = c + scaled(out, r);
        const ImVec2 lo{std::min(near.x, far.x) - std::abs(side.x) * w, std::min(near.y, far.y) - std::abs(side.y) * w};
        const ImVec2 hi{std::max(near.x, far.x) + std::abs(side.x) * w, std::max(near.y, far.y) + std::abs(side.y) * w};
        draw->AddRectFilled(lo, hi, (buttons & arms[i]) != 0u ? kFillHeld : kFill);
    }
    draw->AddRectFilled(c + ImVec2{-w, -w}, c + ImVec2{w, w}, kFill);
    draw->AddPolyline(outline, 12, kLine, line, ImDrawFlags_Closed);
    for (int i = 0; i < 4; ++i) {
        const ImVec2 out = axes[i], side{-out.y, out.x};
        const ImVec2 tip = c + scaled(out, r * 0.80f), back = c + scaled(out, r * 0.55f);
        draw->AddTriangleFilled(tip, back + scaled(side, w * 0.5f), back + scaled(side, -w * 0.5f), kMark);
    }
}

void face_buttons(ImDrawList *draw, const shell::TouchLayout &layout, std::uint32_t buttons, SDL_GamepadType type,
                  float line) {
    using namespace ctrl;
    const float r = layout.face.r, off = layout.face_offset, mark = r * 0.48f;
    const ImVec2 c{layout.face.x, layout.face.y};
    // Each place's PSP button, labelled as that place's button of `type`
    // (the default bindings put Cross on the bottom button, and so on).
    const struct { ImVec2 at; std::uint32_t button; SDL_GamepadButton place; } faces[] = {
        {{0, -off}, kButtonTriangle, SDL_GAMEPAD_BUTTON_NORTH}, {{off, 0}, kButtonCircle, SDL_GAMEPAD_BUTTON_EAST},
        {{0, off}, kButtonCross, SDL_GAMEPAD_BUTTON_SOUTH}, {{-off, 0}, kButtonSquare, SDL_GAMEPAD_BUTTON_WEST}};
    const float thick = std::max(line, r * 0.12f);
    const bool xbox = is_xbox(type);
    for (const auto &face : faces) {
        const ImVec2 at = c + face.at;
        draw->AddCircleFilled(at, r, (buttons & face.button) != 0u ? kFillHeld : kFill, 40);
        draw->AddCircle(at, r, kLine, 40, line);
        const auto letter = [&](const char *text, ImU32 colour) { label(draw, text, at, r * 1.1f, r * 1.6f, colour); };
        switch (SDL_GetGamepadButtonLabelForType(type, face.place)) {
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
            draw->AddTriangle(at + ImVec2{0, -mark}, at + ImVec2{mark * 0.95f, mark * 0.68f},
                              at + ImVec2{-mark * 0.95f, mark * 0.68f}, kTriangle, thick);
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: draw->AddCircle(at, mark * 0.85f, kCircle, 32, thick); break;
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            draw->AddLine(at + ImVec2{-mark * 0.75f, -mark * 0.75f}, at + ImVec2{mark * 0.75f, mark * 0.75f}, kCross, thick);
            draw->AddLine(at + ImVec2{mark * 0.75f, -mark * 0.75f}, at + ImVec2{-mark * 0.75f, mark * 0.75f}, kCross, thick);
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
            draw->AddRect(at + ImVec2{-mark * 0.7f, -mark * 0.7f}, at + ImVec2{mark * 0.7f, mark * 0.7f}, kSquare, 0.0f,
                          thick);
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_A: letter("A", xbox ? kXboxA : kMark); break;
        case SDL_GAMEPAD_BUTTON_LABEL_B: letter("B", xbox ? kXboxB : kMark); break;
        case SDL_GAMEPAD_BUTTON_LABEL_X: letter("X", xbox ? kXboxX : kMark); break;
        case SDL_GAMEPAD_BUTTON_LABEL_Y: letter("Y", xbox ? kXboxY : kMark); break;
        default: break;
        }
    }
}

void menu_button(ImDrawList *draw, const shell::TouchLayout::Circle &menu, bool held, float line) {
    const ImVec2 c{menu.x, menu.y};
    draw->AddCircleFilled(c, menu.r, held ? kFillHeld : kFill, 32);
    draw->AddCircle(c, menu.r, kLine, 32, line);
    // Three bars: the usual sign for a menu.
    const float half = menu.r * 0.45f, gap = menu.r * 0.3f, thick = std::max(line, menu.r * 0.12f);
    for (int i = -1; i <= 1; ++i)
        draw->AddLine({c.x - half, c.y + gap * i}, {c.x + half, c.y + gap * i}, kMark, thick);
}
} // namespace

void draw_touch_controls(const shell::TouchPad &held, SDL_GamepadType type, ImVec2 display) {
    using namespace ctrl;
    const auto layout = shell::touch_layout(display.x, display.y);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const float line = std::max(1.0f, std::min(display.x, display.y) * 0.004f);
    // The other buttons by the same controller's names (the PSP's for PlayStation).
    const bool xbox = is_xbox(type), nintendo = is_nintendo(type);
    button_box(draw, layout.l, (held.buttons & kButtonL) != 0u, xbox ? "LB" : "L", line);
    button_box(draw, layout.r, (held.buttons & kButtonR) != 0u, xbox ? "RB" : "R", line);
    button_box(draw, layout.select, (held.buttons & kButtonSelect) != 0u,
               xbox ? "VIEW" : nintendo ? "\xe2\x88\x92" : "SELECT", line);
    button_box(draw, layout.start, (held.buttons & kButtonStart) != 0u, xbox ? "MENU" : nintendo ? "+" : "START", line);
    menu_button(draw, layout.menu, held.menu, line);
    dpad(draw, layout.dpad, held.buttons, line);
    face_buttons(draw, layout, held.buttons, type, line);
    stick(draw, layout.left_stick, held.lx, held.ly, line);
    stick(draw, layout.right_stick, held.rx, held.ry, line);
}

} // namespace sarusaru::ui
