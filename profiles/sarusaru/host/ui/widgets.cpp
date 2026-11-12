#include "ui/widgets.hpp"

#include "ui/text.hpp"
#include "ui/theme.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace sarusaru::ui {
namespace {

constexpr float kPi = 3.14159265f;

std::string g_focus_request;

ImU32 with_alpha(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu));
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t,
                                                 x.w + (y.w - x.w) * t));
}

// Eases a per-control value towards `target` (0..1), for the focus bounce.
float animate(ImGuiID id, float target) {
    ImGuiStorage *storage = ImGui::GetStateStorage();
    float *value = storage->GetFloatRef(ImHashStr("anim", 0, id), target);
    const float rate = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 14.0f);
    *value += (target - *value) * rate;
    return *value;
}

struct Interaction {
    ImGuiID id{};
    bool visible{}, pressed{}, hovered{}, held{}, focused{};
};

// Adds a control at `bb` and decides how it is being used. The mouse moves
// the focus to what it hovers, so the keyboard or gamepad carries on from
// there; a finger shows the focus only while it presses.
Interaction interact(const Layer &layer, std::string_view name, const ImRect &bb, ImGuiItemFlags flags = 0,
                     bool enabled = true) {
    ImGuiContext &g = *GImGui;
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    Interaction result;
    result.id = window->GetID(name.data(), name.data() + name.size());
    ImGui::ItemSize(bb.GetSize());
    if (!enabled) flags |= ImGuiItemFlags_Disabled;
    result.visible = ImGui::ItemAdd(bb, result.id, nullptr, flags);
    if (!g_focus_request.empty() && g_focus_request == name) {
        ImGui::SetFocusID(result.id, window);
        ImGui::SetNavCursorVisibleAfterMove();
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
        g_focus_request.clear();
    }
    if (!result.visible) return result;
    result.pressed = ImGui::ButtonBehavior(bb, result.id, &result.hovered, &result.held) && enabled;
    // The mouse moves the focus to what it hovers, so one control is
    // highlighted and the keyboard or gamepad carries on from it.
    if (layer.device() == Device::Mouse && result.hovered && g.NavId != result.id && (flags & ImGuiItemFlags_NoNav) == 0)
        ImGui::SetFocusID(result.id, window);
    result.focused = layer.device() == Device::Touch ? result.held : g.NavId == result.id;
    return result;
}

bool left_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
           ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft);
}
bool right_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
           ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight);
}

void triangle(ImDrawList *draw, ImVec2 center, float size, int direction, ImU32 color) {
    const float d = static_cast<float>(direction);
    draw->AddTriangleFilled(ImVec2(center.x + d * size * 0.55f, center.y), ImVec2(center.x - d * size * 0.45f, center.y - size * 0.6f),
                            ImVec2(center.x - d * size * 0.45f, center.y + size * 0.6f), color);
}

// Recolours the vertices from `start` on by height, from `top` at `y0` to
// `bottom` at `y1`, through `middle` halfway when it is not 0. Keeps each
// vertex's alpha, so antialiased edges stay soft.
void shade_vertical(ImDrawList *draw, int start, float y0, float y1, ImU32 top, ImU32 middle, ImU32 bottom) {
    const float span = std::max(y1 - y0, 1.0f);
    for (int i = start; i < draw->VtxBuffer.Size; ++i) {
        ImDrawVert &vertex = draw->VtxBuffer[i];
        const float t = std::clamp((vertex.pos.y - y0) / span, 0.0f, 1.0f);
        ImU32 color = middle == 0u ? mix(top, bottom, t) : t < 0.5f ? mix(top, middle, t * 2.0f) : mix(middle, bottom, t * 2.0f - 1.0f);
        vertex.col = (color & ~IM_COL32_A_MASK) | (vertex.col & IM_COL32_A_MASK);
    }
}

// Leans the vertices from `start` on to the right above `center_y` (and to
// the left below it), like the box art's slanted buttons.
void slant(ImDrawList *draw, int start, float center_y) {
    constexpr float kLean = 0.123f; // tan(7 degrees)
    for (int i = start; i < draw->VtxBuffer.Size; ++i) {
        ImDrawVert &vertex = draw->VtxBuffer[i];
        vertex.pos.x += (center_y - vertex.pos.y) * kLean;
    }
}

// A five-pointed star of `radius`, turned by `angle` radians.
void star(ImDrawList *draw, ImVec2 center, float radius, float angle, ImU32 color) {
    std::array<ImVec2, 10> points;
    for (int i = 0; i < 10; ++i) {
        const float r = i % 2 == 0 ? radius : radius * 0.42f;
        const float a = angle - kPi * 0.5f + kPi * static_cast<float>(i) / 5.0f;
        points[static_cast<std::size_t>(i)] = ImVec2(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
    }
    draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), color);
}

void outlined_star(ImDrawList *draw, ImVec2 center, float radius, float angle, ImU32 fill, ImU32 outline, float thickness) {
    star(draw, center, radius + thickness * 1.6f, angle, outline);
    star(draw, center, radius, angle, fill);
}

// The focus marker: the purple star from around the logo, rimmed in yellow,
// rocking gently. `amount` (0..1) grows it in.
void focus_star(ImDrawList *draw, ImVec2 center, float radius, float amount, float time) {
    if (amount <= 0.01f) return;
    const float angle = (-12.0f + 6.0f * std::sin(time * 3.0f)) * kPi / 180.0f;
    outlined_star(draw, center, radius * amount, angle, with_alpha(colors::kStar, amount), with_alpha(colors::kYellow, amount),
                  radius * 0.16f * amount);
}

// A radial fill of rings: `stops` pairs a radius (as a fraction of
// `radii`) with the colour there; colours blend between rings.
void radial(ImDrawList *draw, ImVec2 center, ImVec2 radii, std::initializer_list<std::pair<float, ImU32>> stops) {
    constexpr int kSegments = 64;
    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    const auto point = [&](int i, float f) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        return ImVec2(center.x + std::cos(a) * radii.x * f, center.y + std::sin(a) * radii.y * f);
    };
    const auto *previous = stops.begin();
    for (const auto *ring = previous + 1; ring != stops.end(); previous = ring++) {
        draw->PrimReserve(kSegments * 6, kSegments * 2);
        const auto base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
        for (int i = 0; i < kSegments; ++i) {
            draw->PrimWriteVtx(point(i, previous->first), uv, previous->second);
            draw->PrimWriteVtx(point(i, ring->first), uv, ring->second);
        }
        for (int i = 0; i < kSegments; ++i) {
            const auto a = static_cast<ImDrawIdx>(base + 2 * i), b = static_cast<ImDrawIdx>(base + 2 * ((i + 1) % kSegments));
            draw->PrimWriteIdx(a); draw->PrimWriteIdx(static_cast<ImDrawIdx>(a + 1)); draw->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1));
            draw->PrimWriteIdx(a); draw->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1)); draw->PrimWriteIdx(b);
        }
    }
}

// The focus highlight shared by rows: pale yellow to banana yellow inside a
// navy-purple rim, with the focus star on the left.
void row_highlight(const Layer &layer, ImDrawList *draw, const ImRect &bb, float amount) {
    const float u = layer.unit();
    if (amount <= 0.01f) return;
    const float rounding = 14.0f * u;
    draw->AddRectFilled(bb.Min, bb.Max, with_alpha(colors::kBlue, amount), rounding);
    gradient_rect(draw, ImVec2(bb.Min.x + 3.0f * u, bb.Min.y + 3.0f * u), ImVec2(bb.Max.x - 3.0f * u, bb.Max.y - 3.0f * u),
                  with_alpha(colors::kYellowPale, amount), with_alpha(IM_COL32(255, 213, 58, 255), amount), rounding - 3.0f * u);
    focus_star(draw, ImVec2(bb.Min.x + 20.0f * u, bb.GetCenter().y), 12.0f * u, amount, layer.time());
}

// --- Glyphs for the hints ---
// Each advances `cursor` past itself; with a null `draw` it only measures.

void keycap(ImDrawList *draw, ImVec2 &cursor, float u, std::string_view label) {
    ImFont *font = fonts().strong;
    const float size = 14.0f * u;
    const ImVec2 text = text_size(font, size, label);
    const float width = std::max(text.x + 14.0f * u, 26.0f * u), height = 24.0f * u;
    const ImVec2 min(cursor.x, cursor.y - height * 0.5f), max(cursor.x + width, cursor.y + height * 0.5f);
    cursor.x = max.x + 4.0f * u;
    if (draw == nullptr) return;
    draw->AddRectFilled(ImVec2(min.x, min.y + 2.0f * u), ImVec2(max.x, max.y + 2.0f * u), colors::kBlueDeep, 6.0f * u);
    draw->AddRectFilled(min, max, colors::kWhite, 6.0f * u);
    draw->AddRect(min, max, colors::kBlue, 6.0f * u, 2.0f * u);
    draw->AddText(font, size, ImVec2(min.x + (width - text.x) * 0.5f, min.y + (height - text.y) * 0.5f), colors::kInk,
                  label.data(), label.data() + label.size());
}

void face_button(ImDrawList *draw, ImVec2 &cursor, float u, SDL_GamepadType type, SDL_GamepadButton button) {
    const float r = 13.0f * u;
    const ImVec2 c(cursor.x + r, cursor.y);
    cursor.x += 2.0f * r + 4.0f * u;
    if (draw == nullptr) return;
    const bool xbox = type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE;
    draw->AddCircleFilled(ImVec2(c.x, c.y + 2.0f * u), r, colors::kBlueDeep);
    draw->AddCircleFilled(c, r, xbox ? colors::kXboxFace : IM_COL32(36, 44, 70, 255));
    draw->AddCircle(c, r, colors::kWhite, 0, 2.0f * u);
    const float s = r * 0.48f, t = 2.4f * u;
    const auto letter = [&](const char *text, ImU32 color) {
        ImFont *font = fonts().strong;
        const ImVec2 size = text_size(font, 16.0f * u, text);
        draw->AddText(font, 16.0f * u, ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.52f), color, text);
    };
    switch (SDL_GetGamepadButtonLabelForType(type, button)) {
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        draw->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), colors::kPsCross, t);
        draw->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), colors::kPsCross, t);
        break;
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: draw->AddCircle(c, s * 1.05f, colors::kPsCircle, 0, t); break;
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
        draw->AddRect(ImVec2(c.x - s * 0.9f, c.y - s * 0.9f), ImVec2(c.x + s * 0.9f, c.y + s * 0.9f), colors::kPsSquare, 0.0f, t);
        break;
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        draw->AddTriangle(ImVec2(c.x, c.y - s * 1.05f), ImVec2(c.x - s * 1.05f, c.y + s * 0.75f),
                          ImVec2(c.x + s * 1.05f, c.y + s * 0.75f), colors::kPsTriangle, t);
        break;
    case SDL_GAMEPAD_BUTTON_LABEL_A: letter("A", xbox ? colors::kXboxA : colors::kWhite); break;
    case SDL_GAMEPAD_BUTTON_LABEL_B: letter("B", xbox ? colors::kXboxB : colors::kWhite); break;
    case SDL_GAMEPAD_BUTTON_LABEL_X: letter("X", xbox ? colors::kXboxX : colors::kWhite); break;
    case SDL_GAMEPAD_BUTTON_LABEL_Y: letter("Y", xbox ? colors::kXboxY : colors::kWhite); break;
    default: letter("?", colors::kWhite); break;
    }
}

void pad_pill(ImDrawList *draw, ImVec2 &cursor, float u, std::string_view label) {
    ImFont *font = fonts().strong;
    const float size = 13.0f * u;
    const ImVec2 text = text_size(font, size, label);
    const float width = text.x + 14.0f * u, height = 22.0f * u;
    const ImVec2 min(cursor.x, cursor.y - height * 0.5f), max(cursor.x + width, cursor.y + height * 0.5f);
    cursor.x = max.x + 4.0f * u;
    if (draw == nullptr) return;
    draw->AddRectFilled(ImVec2(min.x, min.y + 2.0f * u), ImVec2(max.x, max.y + 2.0f * u), colors::kBlueDeep, height * 0.5f);
    draw->AddRectFilled(min, max, IM_COL32(36, 44, 70, 255), height * 0.5f);
    draw->AddRect(min, max, colors::kWhite, height * 0.5f, 2.0f * u);
    draw->AddText(font, size, ImVec2(min.x + 7.0f * u, min.y + (height - text.y) * 0.5f), colors::kWhite,
                  label.data(), label.data() + label.size());
}

void dpad_left_right(ImDrawList *draw, ImVec2 &cursor, float u) {
    const float r = 13.0f * u;
    const ImVec2 c(cursor.x + r, cursor.y);
    cursor.x += 2.0f * r + 4.0f * u;
    if (draw == nullptr) return;
    draw->AddCircleFilled(ImVec2(c.x, c.y + 2.0f * u), r, colors::kBlueDeep);
    draw->AddCircleFilled(c, r, IM_COL32(36, 44, 70, 255));
    draw->AddCircle(c, r, colors::kWhite, 0, 2.0f * u);
    triangle(draw, ImVec2(c.x - r * 0.45f, c.y), 7.0f * u, -1, colors::kWhite);
    triangle(draw, ImVec2(c.x + r * 0.45f, c.y), 7.0f * u, 1, colors::kWhite);
}

std::string_view shoulder(SDL_GamepadType type, bool right) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return right ? "R1" : "L1";
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return right ? "RB" : "LB";
    default: return right ? "R" : "L";
    }
}

} // namespace

// --- Drawing -------------------------------------------------------------

ImVec2 text_size(ImFont *font, float size, std::string_view text, float wrap) {
    return font->CalcTextSizeA(size, FLT_MAX, wrap, text.data(), text.data() + text.size());
}

void gradient_rect(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom, float rounding) {
    const int start = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, colors::kWhite, rounding);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, ImVec2(min.x, min.y),
                                                  ImVec2(min.x, max.y), top, bottom);
}

void outlined_text(ImDrawList *draw, ImFont *font, float size, ImVec2 position, std::string_view text, ImU32 top,
                   ImU32 bottom, ImU32 outline, float thickness, ImU32 middle) {
    const char *begin = text.data(), *end = text.data() + text.size();
    // The shadow, then the outline as copies around a circle, then the fill.
    constexpr int kSteps = 16;
    for (int i = 0; i < kSteps; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / kSteps;
        draw->AddText(font, size, ImVec2(position.x + std::cos(angle) * thickness,
                                         position.y + thickness * 0.9f + std::sin(angle) * thickness),
                      colors::kBlueDeep, begin, end);
    }
    for (int i = 0; i < kSteps; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / kSteps;
        draw->AddText(font, size, ImVec2(position.x + std::cos(angle) * thickness, position.y + std::sin(angle) * thickness),
                      outline, begin, end);
    }
    const int start = draw->VtxBuffer.Size;
    draw->AddText(font, size, position, colors::kWhite, begin, end);
    const ImVec2 extent = text_size(font, size, text);
    shade_vertical(draw, start, position.y + size * 0.15f, position.y + extent.y * 0.85f, top, middle, bottom);
}

void siren_lamp(ImDrawList *draw, ImVec2 center, float radius, float time) {
    if (radius <= 0.5f) return;
    const float pulse = 0.5f + 0.5f * std::sin(time * 6.0f);
    draw->AddCircleFilled(center, radius * (1.5f + 0.35f * pulse), with_alpha(colors::kRed, 0.25f + 0.2f * pulse));
    draw->AddCircleFilled(center, radius * 1.18f, colors::kBlue);
    draw->AddCircleFilled(center, radius, mix(colors::kRedDark, colors::kRed, 0.6f + 0.4f * pulse));
    draw->AddCircleFilled(ImVec2(center.x - radius * 0.32f, center.y - radius * 0.35f), radius * 0.32f,
                          IM_COL32(255, 255, 255, 200));
}

void draw_backdrop(const Layer &layer) {
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const float u = layer.unit(), t = layer.time();
    draw->AddRectFilled(ImVec2(0, 0), size, colors::kRayDark);

    // Alternating rays turning slowly about a point above the middle.
    const ImVec2 sun(size.x * 0.5f, size.y * 0.4f);
    const float reach = std::hypot(size.x, size.y);
    constexpr int kRays = 22;
    for (int i = 0; i < kRays; ++i) {
        const float a = t * 0.03f + 2.0f * kPi * static_cast<float>(i) / kRays;
        const float b = a + kPi / kRays;
        draw->AddTriangleFilled(sun, ImVec2(sun.x + std::cos(a) * reach, sun.y + std::sin(a) * reach),
                                ImVec2(sun.x + std::cos(b) * reach, sun.y + std::sin(b) * reach), colors::kRayLight);
    }

    // A bright glow where the rays meet, and the corners darkened.
    const float corner = std::hypot(size.x * 0.5f, size.y * 0.6f);
    radial(draw, sun, ImVec2(corner, corner),
           {{0.0f, colors::kGlow}, {0.18f, IM_COL32(205, 238, 255, 115)}, {0.52f, IM_COL32(255, 255, 255, 0)}});
    const ImVec2 middle(size.x * 0.5f, size.y * 0.45f), vignette(size.x * 0.72f, size.y * 0.8f);
    radial(draw, middle, vignette,
           {{0.55f, with_alpha(colors::kVignette, 0.0f)}, {1.0f, colors::kVignette}, {1.6f, colors::kVignette}});

    // White star badges, as the box art scatters them, rocking gently. Each
    // is placed as a fraction of the screen, with its size and lean.
    struct Badge { float x, y, size, lean; };
    constexpr Badge kBadges[] = {{0.092f, 0.164f, 96, -14}, {0.173f, 0.372f, 64, 10}, {0.121f, 0.665f, 118, -6},
                                 {0.899f, 0.160f, 110, 16}, {0.816f, 0.465f, 70, -18}, {0.910f, 0.660f, 90, 8}};
    int index = 0;
    for (const Badge &badge : kBadges) {
        const float radius = badge.size * 0.36f * u;
        const float angle = (badge.lean + 3.0f * std::sin(t * 0.6f + static_cast<float>(index++))) * kPi / 180.0f;
        const ImVec2 center(badge.x * size.x, badge.y * size.y);
        star(draw, ImVec2(center.x, center.y + 4.0f * u), radius + 2.0f * u, angle, colors::kStarShadow);
        star(draw, center, radius + 2.0f * u, angle, colors::kWhite);
    }
}

float screen_title(const Layer &layer, std::string_view title) {
    const float u = layer.unit();
    ImFont *font = fonts().display;
    const float size = 50.0f * u;
    const ImVec2 extent = text_size(font, size, title);
    const ImVec2 position((ImGui::GetIO().DisplaySize.x - extent.x) * 0.5f, 16.0f * u);
    outlined_text(ImGui::GetForegroundDrawList(), font, size, position, title, colors::kLetterTop, colors::kLetterBottom,
                  colors::kBlue, 6.0f * u, colors::kLetterMid);
    return position.y + extent.y + 10.0f * u;
}

void panel(const Layer &layer, ImVec2 min, ImVec2 max, ImDrawList *draw) {
    const float u = layer.unit(), rounding = 22.0f * u, edge = 5.0f * u;
    if (draw == nullptr) draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge + 7.0f * u), ImVec2(max.x + edge, max.y + edge + 7.0f * u),
                        colors::kShadow, rounding + edge);
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge), ImVec2(max.x + edge, max.y + edge), colors::kBlue, rounding + edge);
    draw->AddRectFilled(min, max, colors::kWhite, rounding);
}

// --- Focus ---------------------------------------------------------------

void focus_control(std::string_view id) { g_focus_request = id; }

// --- Controls --------------------------------------------------------------

bool menu_button(const Layer &layer, std::string_view id, std::string_view label, ImVec2 size, bool primary, bool enabled) {
    const float u = layer.unit();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImRect bb(position, ImVec2(position.x + size.x, position.y + size.y));
    const Interaction item = interact(layer, id, bb, 0, enabled);
    if (!item.visible) return false;
    const float focus = animate(item.id, item.focused ? 1.0f : 0.0f);
    ImDrawList *draw = ImGui::GetWindowDrawList();

    // A slanted pill on a solid drop, which it sinks onto when pressed. The
    // focus rings it in yellow and puts the star beside it.
    const float sink = item.held ? 4.0f * u : 0.0f, drop = 6.0f * u - sink;
    const ImVec2 min(bb.Min.x, bb.Min.y + sink), max(bb.Max.x, bb.Max.y + sink);
    const float rounding = (max.y - min.y) * 0.5f, edge = 4.0f * u, center_y = (min.y + max.y) * 0.5f;
    const ImU32 rim = enabled ? colors::kBlue : colors::kDisabled;
    const int start = draw->VtxBuffer.Size;
    if (focus > 0.01f) {
        const float ring = edge + 6.0f * u * focus;
        draw->AddRectFilled(ImVec2(min.x - ring, min.y - ring), ImVec2(max.x + ring, max.y + ring + drop),
                            with_alpha(colors::kYellowShade, focus), rounding + ring);
        draw->AddRectFilled(ImVec2(min.x - ring, min.y - ring), ImVec2(max.x + ring, max.y + ring),
                            with_alpha(colors::kYellow, focus), rounding + ring);
    }
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge + drop), ImVec2(max.x + edge, max.y + edge + drop), rim, rounding + edge);
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge), ImVec2(max.x + edge, max.y + edge), rim, rounding + edge);
    const int fill = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, colors::kWhite, rounding);
    if (!enabled)
        shade_vertical(draw, fill, min.y, max.y, IM_COL32(236, 240, 247, 255), 0, IM_COL32(220, 226, 238, 255));
    else if (primary)
        shade_vertical(draw, fill, min.y, max.y, colors::kRedLight, colors::kRed, colors::kRedDark);
    else
        shade_vertical(draw, fill, min.y, max.y, colors::kWhite, colors::kWhite, colors::kHelmet);
    // A button without a label (a save slot) has its content drawn over it
    // upright, so it stays upright too.
    if (!label.empty()) slant(draw, start, center_y);

    ImFont *font = fonts().display;
    const float text_height = size.y * 0.54f;
    const ImVec2 extent = text_size(font, text_height, label);
    const ImVec2 at((min.x + max.x - extent.x) * 0.5f, center_y - extent.y * 0.5f);
    if (!enabled) {
        draw->AddText(font, text_height, at, colors::kDisabled, label.data(), label.data() + label.size());
    } else if (primary) {
        draw->AddText(font, text_height, ImVec2(at.x, at.y + 3.0f * u), colors::kRedInk, label.data(), label.data() + label.size());
        draw->AddText(font, text_height, at, colors::kWhite, label.data(), label.data() + label.size());
    } else {
        draw->AddText(font, text_height, at, colors::kInk, label.data(), label.data() + label.size());
    }
    focus_star(draw, ImVec2(min.x - 30.0f * u, center_y), 17.0f * u, focus, layer.time());
    return item.pressed;
}

bool back_button(const Layer &layer, ImVec2 position) {
    if (layer.device() != Device::Mouse && layer.device() != Device::Touch) return false;
    const float u = layer.unit();
    ImFont *font = fonts().display;
    const float size = 19.0f * u;
    const std::string_view label = tr(Text::Back);
    const ImVec2 extent = text_size(font, size, label);
    const ImVec2 box(extent.x + 52.0f * u, 40.0f * u);
    ImGui::SetCursorScreenPos(position);
    const ImRect bb(position, ImVec2(position.x + box.x, position.y + box.y));
    const Interaction item = interact(layer, "##back", bb, ImGuiItemFlags_NoNav);
    if (!item.visible) return false;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float rounding = box.y * 0.5f, edge = 3.0f * u, sink = item.held ? 3.0f * u : 0.0f;
    const ImVec2 min(bb.Min.x, bb.Min.y + sink), max(bb.Max.x, bb.Max.y + sink);
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge + 4.0f * u - sink), ImVec2(max.x + edge, max.y + edge + 4.0f * u - sink),
                        colors::kShadow, rounding + edge);
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge), ImVec2(max.x + edge, max.y + edge), colors::kBlue, rounding + edge);
    gradient_rect(draw, min, max, item.hovered || item.held ? colors::kYellowPale : colors::kWhite,
                  item.hovered || item.held ? colors::kYellow : colors::kWhite, rounding);
    const ImVec2 chevron(min.x + 20.0f * u, (min.y + max.y) * 0.5f);
    const ImVec2 points[] = {ImVec2(chevron.x + 4.0f * u, chevron.y - 7.0f * u), ImVec2(chevron.x - 3.0f * u, chevron.y),
                             ImVec2(chevron.x + 4.0f * u, chevron.y + 7.0f * u)};
    draw->AddPolyline(points, 3, colors::kBlue, 3.5f * u);
    draw->AddText(font, size, ImVec2(min.x + 34.0f * u, (min.y + max.y) * 0.5f - extent.y * 0.5f), colors::kBlue,
                  label.data(), label.data() + label.size());
    return item.pressed;
}

int setting_row(const Layer &layer, std::string_view id, const RowView &row, float width) {
    const float u = layer.unit();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImRect bb(position, ImVec2(position.x + width, position.y + 44.0f * u));
    const Interaction item = interact(layer, id, bb);
    if (!item.visible) return 0;
    const float focus = animate(item.id, item.focused ? 1.0f : 0.0f);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    row_highlight(layer, draw, bb, focus);

    ImFont *body = fonts().body, *strong = fonts().strong;
    const float size = 19.0f * u;
    const ImVec2 label_size = text_size(body, size, row.label);
    // A disabled row still takes focus, so its description can say why.
    const ImU32 ink = row.enabled ? colors::kInk : colors::kDisabled;
    draw->AddText(body, size, ImVec2(bb.Min.x + 40.0f * u, bb.GetCenter().y - label_size.y * 0.5f), ink,
                  row.label.c_str());

    // The value between its arrows, in the right part of the row.
    const float value_width = std::min(300.0f * u, width * 0.48f);

    // An experimental value: a yellow tag after the label, where there is room for it.
    if (row.experimental) {
        const std::string_view tag = tr(Text::Experimental);
        const float tag_size = 13.0f * u, pad_x = 7.0f * u, pad_y = 3.0f * u;
        const ImVec2 tag_extent = text_size(strong, tag_size, tag);
        const ImVec2 min(bb.Min.x + 40.0f * u + label_size.x + 10.0f * u, bb.GetCenter().y - tag_extent.y * 0.5f - pad_y);
        const ImVec2 max(min.x + tag_extent.x + 2.0f * pad_x, min.y + tag_extent.y + 2.0f * pad_y);
        if (max.x < bb.Max.x - value_width - 16.0f * u) {
            draw->AddRectFilled(min, max, colors::kYellow, (max.y - min.y) * 0.5f);
            draw->AddRect(min, max, colors::kYellowShade, (max.y - min.y) * 0.5f, 1.5f * u);
            draw->AddText(strong, tag_size, ImVec2(min.x + pad_x, min.y + pad_y), colors::kInk, tag.data(),
                          tag.data() + tag.size());
        }
    }
    const ImRect value(ImVec2(bb.Max.x - value_width - 12.0f * u, bb.Min.y), ImVec2(bb.Max.x - 12.0f * u, bb.Max.y));
    // The arrows: navy chevrons, which the focus sets in navy discs in yellow.
    const float arrow = 26.0f * u;
    for (const int direction : {-1, 1}) {
        const ImVec2 c(direction < 0 ? value.Min.x + arrow * 0.5f : value.Max.x - arrow * 0.5f, value.GetCenter().y);
        if (row.enabled && focus > 0.01f) draw->AddCircleFilled(c, arrow * 0.5f, with_alpha(colors::kBlue, focus));
        const ImU32 color = row.enabled ? mix(colors::kBlue, colors::kYellow, focus) : colors::kDisabled;
        const float d = static_cast<float>(direction), s = 5.0f * u;
        const ImVec2 points[] = {ImVec2(c.x - d * s * 0.6f, c.y - s), ImVec2(c.x + d * s * 0.6f, c.y), ImVec2(c.x - d * s * 0.6f, c.y + s)};
        draw->AddPolyline(points, 3, color, 3.0f * u);
    }
    // A face button, when the row has one, leads the value and is centred with it.
    const bool face = row.face != SDL_GAMEPAD_BUTTON_INVALID;
    ImVec2 glyph(0.0f, value.GetCenter().y);
    if (face) face_button(nullptr, glyph, u, layer.pad_type(), row.face);
    const ImVec2 value_size = text_size(strong, size, row.value);
    const float left = value.GetCenter().x - (glyph.x + value_size.x) * 0.5f;
    if (face) {
        ImVec2 at(left, value.GetCenter().y);
        face_button(draw, at, u, layer.pad_type(), row.face);
    }
    draw->AddText(strong, size, ImVec2(left + glyph.x, value.GetCenter().y - value_size.y * 0.5f),
                  row.warning ? colors::kRed : ink, row.value.c_str());

    int change = 0;
    if (!row.enabled) return change;
    if (item.pressed) {
        // A click or tap on the left arrow's side steps back; elsewhere, or
        // Enter or the confirm button, steps forward.
        const bool pointer = ImGui::GetIO().MouseReleased[0] || ImGui::GetIO().MouseClicked[0];
        change = pointer && ImGui::GetIO().MousePos.x < value.Min.x + value_width * 0.3f &&
                         ImGui::GetIO().MousePos.x >= value.Min.x - 8.0f * u ? -1 : 1;
    }
    if (GImGui->NavId == item.id && layer.device() != Device::Touch) {
        if (left_pressed()) change = -1;
        if (right_pressed()) change = 1;
    }
    return change;
}

bool action_row(const Layer &layer, std::string_view id, std::string_view label, std::string_view detail, float width,
                bool enabled) {
    const float u = layer.unit();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImRect bb(position, ImVec2(position.x + width, position.y + 44.0f * u));
    const Interaction item = interact(layer, id, bb, 0, enabled);
    if (!item.visible) return false;
    const float focus = animate(item.id, item.focused ? 1.0f : 0.0f);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    row_highlight(layer, draw, bb, focus);
    ImFont *body = fonts().strong;
    const float size = 19.0f * u;
    const ImVec2 label_size = text_size(body, size, label);
    draw->AddText(body, size, ImVec2(bb.Min.x + 40.0f * u, bb.GetCenter().y - label_size.y * 0.5f),
                  enabled ? colors::kInk : colors::kDisabled, label.data(), label.data() + label.size());
    if (!detail.empty()) {
        ImFont *small = fonts().body;
        const float detail_size = 16.0f * u;
        const ImVec2 extent = text_size(small, detail_size, detail);
        draw->AddText(small, detail_size, ImVec2(bb.Max.x - extent.x - 40.0f * u, bb.GetCenter().y - extent.y * 0.5f),
                      colors::kInkDim, detail.data(), detail.data() + detail.size());
    }
    triangle(draw, ImVec2(bb.Max.x - 20.0f * u, bb.GetCenter().y), 8.0f * u, 1,
             enabled ? mix(colors::kBlueLight, colors::kBlue, focus) : colors::kDisabled);
    return item.pressed;
}

bool list_row(const Layer &layer, std::string_view id, ListIcon icon, std::string_view name, std::string_view detail,
              float width, bool highlight) {
    const float u = layer.unit();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImRect bb(position, ImVec2(position.x + width, position.y + 42.0f * u));
    const Interaction item = interact(layer, id, bb);
    if (!item.visible) return false;
    const float focus = animate(item.id, item.focused ? 1.0f : 0.0f);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (highlight && focus < 0.99f) draw->AddRectFilled(bb.Min, bb.Max, IM_COL32(255, 230, 120, 90), 12.0f * u);
    row_highlight(layer, draw, bb, focus);

    // The icon, drawn: folder, up arrow, disc or file.
    const ImVec2 c(bb.Min.x + 52.0f * u, bb.GetCenter().y);
    const float s = 11.0f * u;
    switch (icon) {
    case ListIcon::Folder:
    case ListIcon::Place:
        draw->AddRectFilled(ImVec2(c.x - s, c.y - s * 0.9f), ImVec2(c.x - s * 0.1f, c.y - s * 0.5f), colors::kYellowShade, 2.0f * u);
        gradient_rect(draw, ImVec2(c.x - s, c.y - s * 0.6f), ImVec2(c.x + s, c.y + s * 0.8f), colors::kYellow, colors::kYellowShade, 3.0f * u);
        draw->AddRect(ImVec2(c.x - s, c.y - s * 0.6f), ImVec2(c.x + s, c.y + s * 0.8f), colors::kBlue, 3.0f * u, 1.5f * u);
        break;
    case ListIcon::Up:
        draw->AddTriangleFilled(ImVec2(c.x, c.y - s), ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), colors::kBlue);
        draw->AddRectFilled(ImVec2(c.x - s * 0.35f, c.y), ImVec2(c.x + s * 0.35f, c.y + s), colors::kBlue);
        break;
    case ListIcon::Disc:
        draw->AddCircleFilled(c, s * 1.05f, colors::kBlue);
        draw->AddCircleFilled(c, s * 0.9f, IM_COL32(210, 220, 240, 255));
        draw->AddCircleFilled(ImVec2(c.x - s * 0.25f, c.y - s * 0.3f), s * 0.35f, IM_COL32(255, 255, 255, 220));
        draw->AddCircleFilled(c, s * 0.28f, colors::kBlue);
        break;
    case ListIcon::File:
        draw->AddRectFilled(ImVec2(c.x - s * 0.75f, c.y - s), ImVec2(c.x + s * 0.75f, c.y + s), colors::kWhite, 2.0f * u);
        draw->AddRect(ImVec2(c.x - s * 0.75f, c.y - s), ImVec2(c.x + s * 0.75f, c.y + s), colors::kInkDim, 2.0f * u, 1.5f * u);
        break;
    }
    ImFont *body = fonts().body;
    const float size = 18.0f * u;
    const float name_x = bb.Min.x + 76.0f * u;
    float detail_width = 0.0f;
    if (!detail.empty()) {
        const ImVec2 extent = text_size(body, 15.0f * u, detail);
        detail_width = extent.x + 20.0f * u;
        draw->AddText(body, 15.0f * u, ImVec2(bb.Max.x - extent.x - 16.0f * u, bb.GetCenter().y - extent.y * 0.5f),
                      colors::kInkDim, detail.data(), detail.data() + detail.size());
    }
    const ImVec4 clip(name_x, bb.Min.y, bb.Max.x - detail_width - 16.0f * u, bb.Max.y);
    const ImVec2 name_size = text_size(body, size, name);
    draw->AddText(body, size, ImVec2(name_x, bb.GetCenter().y - name_size.y * 0.5f), colors::kInk, name.data(),
                  name.data() + name.size(), 0.0f, &clip);
    return item.pressed;
}

int tab_bar(const Layer &layer, std::span<const std::string_view> tabs, int selected, float y) {
    const float u = layer.unit();
    const int count = static_cast<int>(tabs.size());
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false))
        selected = (selected + count - 1) % count;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_W, false))
        selected = (selected + 1) % count;

    ImFont *font = fonts().display;
    const float size = 20.0f * u, height = 40.0f * u, gap = 10.0f * u, pad = 22.0f * u;
    float total = gap * static_cast<float>(count - 1);
    for (const auto tab : tabs) total += text_size(font, size, tab).x + 2.0f * pad;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float x = (ImGui::GetIO().DisplaySize.x - total) * 0.5f;

    // The shoulder buttons that switch tabs, either side.
    const bool pad_hints = layer.device() == Device::Gamepad, key_hints = layer.device() == Device::Keyboard;
    if (pad_hints || key_hints) {
        ImVec2 left(x - 52.0f * u, y + height * 0.5f), right(x + total + 14.0f * u, y + height * 0.5f);
        if (pad_hints) {
            pad_pill(draw, left, u, shoulder(layer.pad_type(), false));
            pad_pill(draw, right, u, shoulder(layer.pad_type(), true));
        } else {
            keycap(draw, left, u, "Q");
            keycap(draw, right, u, "W");
        }
    }
    for (int i = 0; i < count; ++i) {
        const float width = text_size(font, size, tabs[static_cast<std::size_t>(i)]).x + 2.0f * pad;
        const ImRect bb(ImVec2(x, y), ImVec2(x + width, y + height));
        ImGui::SetCursorScreenPos(bb.Min);
        const std::string id = "##tab" + std::to_string(i);
        const Interaction item = interact(layer, id, bb, ImGuiItemFlags_NoNav);
        if (item.pressed) selected = i;
        const bool active = i == selected;
        const float edge = 3.0f * u, rounding = height * 0.5f;
        draw->AddRectFilled(ImVec2(bb.Min.x - edge, bb.Min.y - edge + 4.0f * u), ImVec2(bb.Max.x + edge, bb.Max.y + edge + 4.0f * u),
                            colors::kShadow, rounding + edge);
        draw->AddRectFilled(ImVec2(bb.Min.x - edge, bb.Min.y - edge), ImVec2(bb.Max.x + edge, bb.Max.y + edge),
                            colors::kBlue, rounding + edge);
        const int fill = draw->VtxBuffer.Size;
        draw->AddRectFilled(bb.Min, bb.Max, colors::kWhite, rounding);
        if (active)
            shade_vertical(draw, fill, bb.Min.y, bb.Max.y, colors::kLetterTop, colors::kLetterMid, IM_COL32(255, 154, 18, 255));
        else if (item.hovered)
            shade_vertical(draw, fill, bb.Min.y, bb.Max.y, colors::kWhite, 0, colors::kYellowPale);
        const auto label = tabs[static_cast<std::size_t>(i)];
        const ImVec2 extent = text_size(font, size, label);
        draw->AddText(font, size, ImVec2(bb.Min.x + pad, bb.GetCenter().y - extent.y * 0.5f), colors::kInk, label.data(),
                      label.data() + label.size());
        x += width + gap;
    }
    return selected;
}

void begin_list(std::string_view id, ImVec2 size) {
    ImGui::BeginChild(std::string(id).c_str(), size, ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
}

void end_list(const Layer &layer) {
    // A finger dragging the list scrolls it, and the drag does not also press
    // the row it started on.
    const ImGuiIO &io = ImGui::GetIO();
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    if (io.MouseSource == ImGuiMouseSource_TouchScreen && ImGui::IsMouseDragging(0, 8.0f * layer.unit()) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows)) {
        ImGui::SetScrollY(window, window->Scroll.y - io.MouseDelta.y);
        ImGui::ClearActiveID();
    }
    ImGui::EndChild();
}

void draw_glyph(ImDrawList *draw, ImVec2 &cursor, float unit, SDL_GamepadType type, const Glyph &glyph) {
    switch (glyph.kind) {
    case Glyph::Kind::Key: keycap(draw, cursor, unit, glyph.label); break;
    case Glyph::Kind::Face: face_button(draw, cursor, unit, type, glyph.button); break;
    case Glyph::Kind::Pill: pad_pill(draw, cursor, unit, glyph.label); break;
    }
}

CellAction binding_cell(const Layer &layer, std::string_view id, std::span<const Glyph> glyphs, ImVec2 size) {
    const float u = layer.unit();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImRect bb(position, ImVec2(position.x + size.x, position.y + size.y));
    const Interaction item = interact(layer, id, bb);
    if (!item.visible) return CellAction::None;
    const float focus = animate(item.id, item.focused ? 1.0f : 0.0f);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float rounding = 10.0f * u;
    draw->AddRectFilled(bb.Min, bb.Max, IM_COL32(222, 232, 248, 255), rounding);
    if (focus > 0.01f) {
        draw->AddRectFilled(bb.Min, bb.Max, with_alpha(colors::kBlue, focus), rounding);
        gradient_rect(draw, ImVec2(bb.Min.x + 3.0f * u, bb.Min.y + 3.0f * u), ImVec2(bb.Max.x - 3.0f * u, bb.Max.y - 3.0f * u),
                      with_alpha(colors::kYellowPale, focus), with_alpha(colors::kYellow, focus), rounding - 3.0f * u);
    }

    // With the mouse or touch, the focused cell has its own add and clear buttons.
    const bool pointer = layer.device() == Device::Mouse || layer.device() == Device::Touch;
    const bool tools = pointer && (GImGui->NavId == item.id || item.hovered);
    const float tool = 26.0f * u;
    const ImVec2 clear_center(bb.Max.x - 8.0f * u - tool * 0.5f, bb.GetCenter().y);
    const ImVec2 add_center(clear_center.x - tool - 6.0f * u, clear_center.y);
    const float glyph_limit = tools ? add_center.x - tool * 0.5f - 4.0f * u : bb.Max.x - 8.0f * u;

    draw->PushClipRect(bb.Min, ImVec2(glyph_limit, bb.Max.y), true);
    ImVec2 cursor(bb.Min.x + 10.0f * u, bb.GetCenter().y);
    if (glyphs.empty()) {
        ImFont *font = fonts().body;
        const std::string_view none = tr(Text::Unbound);
        const ImVec2 extent = text_size(font, 16.0f * u, none);
        draw->AddText(font, 16.0f * u, ImVec2(cursor.x, cursor.y - extent.y * 0.5f), colors::kInkDim, none.data(),
                      none.data() + none.size());
    }
    for (const Glyph &g : glyphs) draw_glyph(draw, cursor, u, layer.pad_type(), g);
    draw->PopClipRect();

    if (tools) {
        for (const auto &[center, plus] : {std::pair{add_center, true}, std::pair{clear_center, false}}) {
            draw->AddCircleFilled(center, tool * 0.5f, colors::kBlue);
            draw->AddCircleFilled(center, tool * 0.5f - 2.0f * u, colors::kWhite);
            const float a = tool * 0.22f;
            if (plus) {
                draw->AddLine(ImVec2(center.x - a, center.y), ImVec2(center.x + a, center.y), colors::kBlue, 3.0f * u);
                draw->AddLine(ImVec2(center.x, center.y - a), ImVec2(center.x, center.y + a), colors::kBlue, 3.0f * u);
            } else {
                draw->AddLine(ImVec2(center.x - a, center.y - a), ImVec2(center.x + a, center.y + a), colors::kRed, 3.0f * u);
                draw->AddLine(ImVec2(center.x - a, center.y + a), ImVec2(center.x + a, center.y - a), colors::kRed, 3.0f * u);
            }
        }
    }

    if (item.pressed) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto within = [&](ImVec2 center) {
            return std::abs(mouse.x - center.x) <= tool * 0.6f && std::abs(mouse.y - center.y) <= tool * 0.6f;
        };
        if (tools && within(add_center)) return CellAction::Add;
        if (tools && within(clear_center)) return CellAction::Clear;
        return CellAction::Set;
    }
    if (GImGui->NavId == item.id && !pointer) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))
            return CellAction::Add;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            return CellAction::Clear;
    }
    return CellAction::None;
}

void paragraph(const Layer &layer, std::string_view text, float width, ImU32 color) {
    ImFont *font = fonts().body;
    const float size = 18.0f * layer.unit();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 extent = text_size(font, size, text, width);
    ImGui::GetWindowDrawList()->AddText(font, size, position, color, text.data(), text.data() + text.size(), width);
    ImGui::Dummy(extent);
}

// --- Button hints ----------------------------------------------------------

void hints(const Layer &layer, std::initializer_list<Hint> list) {
    if (layer.prompt_device() != Device::Gamepad && layer.prompt_device() != Device::Keyboard) return;
    const float u = layer.unit();
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    ImFont *font = fonts().strong;
    const float size = 17.0f * u;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const SDL_GamepadButton confirm = layer.confirm_south() ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
    const SDL_GamepadButton cancel = layer.confirm_south() ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;

    // Right-aligned at the bottom: measured first, then drawn.
    const auto draw_glyph = [&](ImDrawList *target, ImVec2 &cursor, Control control) {
        const bool pad = layer.prompt_device() == Device::Gamepad;
        switch (control) {
        case Control::Confirm:
            if (pad) face_button(target, cursor, u, layer.pad_type(), confirm); else keycap(target, cursor, u, "Enter");
            break;
        case Control::Back:
            if (pad) face_button(target, cursor, u, layer.pad_type(), cancel); else keycap(target, cursor, u, "Esc");
            break;
        case Control::Change:
            if (pad) dpad_left_right(target, cursor, u); else keycap(target, cursor, u, "← →");
            break;
        case Control::Tabs:
            if (pad) {
                pad_pill(target, cursor, u, shoulder(layer.pad_type(), false));
                pad_pill(target, cursor, u, shoulder(layer.pad_type(), true));
            } else {
                keycap(target, cursor, u, "Q");
                keycap(target, cursor, u, "W");
            }
            break;
        case Control::Add:
            if (pad) face_button(target, cursor, u, layer.pad_type(), SDL_GAMEPAD_BUTTON_NORTH); else keycap(target, cursor, u, "+");
            break;
        case Control::Clear:
            if (pad) face_button(target, cursor, u, layer.pad_type(), SDL_GAMEPAD_BUTTON_WEST);
            else keycap(target, cursor, u, "Backspace");
            break;
        }
    };
    float total = 0.0f;
    for (const Hint &hint : list) {
        ImVec2 cursor(0.0f, 0.0f);
        draw_glyph(nullptr, cursor, hint.control);
        total += cursor.x + 2.0f * u + text_size(font, size, hint.text).x + 16.0f * u;
    }
    // On a navy-purple capsule along the bottom right.
    const float height = 36.0f * u, margin = 14.0f * u, pad = 6.0f * u;
    const ImVec2 max(display.x - margin, display.y - margin), min(max.x - total - pad, max.y - height);
    draw->AddRectFilled(min, max, colors::kBlue, height * 0.5f);
    ImVec2 cursor(min.x + pad, (min.y + max.y) * 0.5f);
    for (const Hint &hint : list) {
        draw_glyph(draw, cursor, hint.control);
        cursor.x += 2.0f * u;
        const ImVec2 extent = text_size(font, size, hint.text);
        draw->AddText(font, size, ImVec2(cursor.x, cursor.y - extent.y * 0.5f), colors::kWhite, hint.text.data(),
                      hint.text.data() + hint.text.size());
        cursor.x += extent.x + 16.0f * u;
    }
}

} // namespace sarusaru::ui
