#include "ui/widgets.hpp"

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

// The focus highlight shared by rows: pale yellow, outlined, the lamp on the left.
void row_highlight(const Layer &layer, ImDrawList *draw, const ImRect &bb, float amount) {
    const float u = layer.unit();
    if (amount <= 0.01f) return;
    const float rounding = 12.0f * u;
    draw->AddRectFilled(bb.Min, bb.Max, with_alpha(colors::kBlue, amount), rounding);
    gradient_rect(draw, ImVec2(bb.Min.x + 3.0f * u, bb.Min.y + 3.0f * u), ImVec2(bb.Max.x - 3.0f * u, bb.Max.y - 3.0f * u),
                  with_alpha(colors::kYellowPale, amount), with_alpha(colors::kYellow, amount), rounding - 3.0f * u);
    siren_lamp(draw, ImVec2(bb.Min.x + 20.0f * u, bb.GetCenter().y), 8.0f * u * amount, layer.time());
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
    draw->AddCircleFilled(ImVec2(c.x, c.y + 2.0f * u), r, colors::kBlueDeep);
    draw->AddCircleFilled(c, r, IM_COL32(36, 44, 70, 255));
    draw->AddCircle(c, r, colors::kWhite, 0, 2.0f * u);
    const float s = r * 0.48f, t = 2.4f * u;
    const auto letter = [&](const char *text, ImU32 color) {
        ImFont *font = fonts().strong;
        const ImVec2 size = text_size(font, 16.0f * u, text);
        draw->AddText(font, 16.0f * u, ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.52f), color, text);
    };
    const bool xbox = type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE;
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
    case SDL_GAMEPAD_BUTTON_LABEL_A: letter("A", xbox ? IM_COL32(110, 200, 80, 255) : colors::kWhite); break;
    case SDL_GAMEPAD_BUTTON_LABEL_B: letter("B", xbox ? IM_COL32(240, 80, 70, 255) : colors::kWhite); break;
    case SDL_GAMEPAD_BUTTON_LABEL_X: letter("X", xbox ? IM_COL32(80, 150, 240, 255) : colors::kWhite); break;
    case SDL_GAMEPAD_BUTTON_LABEL_Y: letter("Y", xbox ? IM_COL32(250, 200, 40, 255) : colors::kWhite); break;
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
                   ImU32 bottom, ImU32 outline, float thickness) {
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
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, ImVec2(position.x, position.y + size * 0.15f),
                                                  ImVec2(position.x, position.y + extent.y * 0.85f), top, bottom);
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
    draw->AddRectFilledMultiColor(ImVec2(0, 0), size, colors::kSkyTop, colors::kSkyTop, colors::kSkyBottom,
                                  colors::kSkyBottom);

    // Sunburst rays turning slowly about a point above the middle.
    const ImVec2 sun(size.x * 0.5f, size.y * 0.32f);
    const float reach = std::hypot(size.x, size.y);
    constexpr int kRays = 18;
    for (int i = 0; i < kRays; ++i) {
        const float a = t * 0.04f + 2.0f * kPi * static_cast<float>(i) / kRays;
        const float b = a + kPi / kRays;
        draw->AddTriangleFilled(sun, ImVec2(sun.x + std::cos(a) * reach, sun.y + std::sin(a) * reach),
                                ImVec2(sun.x + std::cos(b) * reach, sun.y + std::sin(b) * reach), IM_COL32(255, 255, 255, 20));
    }

    // Clouds: puffs of circles drifting right and wrapping around.
    struct Cloud { float x, y, scale, speed; };
    constexpr Cloud kClouds[] = {{0.05f, 0.18f, 1.0f, 9.0f}, {0.42f, 0.10f, 0.7f, 6.0f}, {0.78f, 0.22f, 1.2f, 11.0f},
                                 {0.25f, 0.62f, 1.4f, 14.0f}, {0.66f, 0.70f, 1.1f, 12.0f}, {0.92f, 0.52f, 0.8f, 8.0f}};
    for (const Cloud &cloud : kClouds) {
        const float span = size.x + 240.0f * u;
        const float x = std::fmod(cloud.x * span + t * cloud.speed * u, span) - 120.0f * u;
        const float y = cloud.y * size.y, s = cloud.scale * u;
        constexpr std::array<std::array<float, 3>, 5> kPuffs{{{-48, 8, 26}, {-18, -8, 34}, {20, -14, 38}, {52, 4, 28}, {8, 12, 30}}};
        for (const auto &puff : kPuffs)
            draw->AddCircleFilled(ImVec2(x + puff[0] * s, y + puff[1] * s + 6.0f * s), puff[2] * s, IM_COL32(120, 180, 240, 120));
        for (const auto &puff : kPuffs)
            draw->AddCircleFilled(ImVec2(x + puff[0] * s, y + puff[1] * s), puff[2] * s, colors::kCloud);
    }

    // The swoosh from under the logo: a yellow band rising to the right,
    // gently rippling, outlined in blue.
    constexpr int kPoints = 48;
    std::vector<ImVec2> edge;
    edge.reserve(kPoints + 1);
    for (int i = 0; i <= kPoints; ++i) {
        const float f = static_cast<float>(i) / kPoints;
        edge.emplace_back(size.x * f, size.y - 46.0f * u - 26.0f * u * f + 6.0f * u * std::sin(f * 2.0f * kPi + t * 0.8f));
    }
    std::vector<ImVec2> shape = edge;
    shape.emplace_back(size.x, size.y + 4.0f);
    shape.emplace_back(0.0f, size.y + 4.0f);
    const int start = draw->VtxBuffer.Size;
    draw->AddConcavePolyFilled(shape.data(), static_cast<int>(shape.size()), colors::kWhite);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, ImVec2(0, size.y - 80.0f * u),
                                                  ImVec2(0, size.y), colors::kYellow, colors::kYellowShade);
    draw->AddPolyline(edge.data(), static_cast<int>(edge.size()), colors::kBlue, 6.0f * u);
}

float screen_title(const Layer &layer, std::string_view title) {
    const float u = layer.unit();
    ImFont *font = fonts().display;
    const float size = 46.0f * u;
    const ImVec2 extent = text_size(font, size, title);
    const ImVec2 position((ImGui::GetIO().DisplaySize.x - extent.x) * 0.5f, 18.0f * u);
    outlined_text(ImGui::GetForegroundDrawList(), font, size, position, title, colors::kYellow, colors::kYellowShade,
                  colors::kBlue, 5.0f * u);
    return position.y + extent.y + 10.0f * u;
}

void panel(const Layer &layer, ImVec2 min, ImVec2 max) {
    const float u = layer.unit(), rounding = 22.0f * u, edge = 5.0f * u;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge + 6.0f * u), ImVec2(max.x + edge, max.y + edge + 6.0f * u),
                        colors::kShadow, rounding + edge);
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge), ImVec2(max.x + edge, max.y + edge), colors::kBlue, rounding + edge);
    gradient_rect(draw, min, max, colors::kWhite, colors::kHelmet, rounding);
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

    // Grows a little when focused and sinks when pressed.
    const float grow = 6.0f * u * focus;
    const float sink = item.held ? 3.0f * u : 0.0f;
    const ImVec2 min(bb.Min.x - grow, bb.Min.y - grow * 0.5f + sink), max(bb.Max.x + grow, bb.Max.y + grow * 0.5f + sink);
    const float rounding = (max.y - min.y) * 0.5f, edge = 4.0f * u;
    draw->AddRectFilled(ImVec2(min.x, min.y + 6.0f * u - sink), ImVec2(max.x, max.y + 6.0f * u - sink), colors::kShadow, rounding);
    draw->AddRectFilled(ImVec2(min.x - edge, min.y - edge), ImVec2(max.x + edge, max.y + edge), colors::kBlue, rounding + edge);
    ImU32 top = colors::kWhite, bottom = colors::kHelmet;
    if (!enabled) {
        top = IM_COL32(232, 236, 244, 255);
        bottom = IM_COL32(200, 208, 222, 255);
    } else if (primary) {
        top = mix(IM_COL32(255, 70, 70, 255), colors::kYellow, focus);
        bottom = mix(colors::kRedDark, colors::kYellowShade, focus);
    } else {
        top = mix(colors::kWhite, colors::kYellow, focus);
        bottom = mix(colors::kHelmet, colors::kYellowShade, focus);
    }
    gradient_rect(draw, min, max, top, bottom, rounding);
    // The helmet's sheen across the top half.
    draw->AddRectFilled(ImVec2(min.x + rounding * 0.6f, min.y + 4.0f * u),
                        ImVec2(max.x - rounding * 0.6f, min.y + (max.y - min.y) * 0.42f), IM_COL32(255, 255, 255, 70),
                        rounding * 0.5f);

    ImFont *font = fonts().display;
    const float text_height = size.y * 0.5f;
    const ImVec2 extent = text_size(font, text_height, label);
    const ImVec2 at((min.x + max.x - extent.x) * 0.5f, (min.y + max.y - extent.y) * 0.5f + 2.0f * u);
    if (!enabled)
        draw->AddText(font, text_height, at, colors::kDisabled, label.data(), label.data() + label.size());
    else if (primary && focus < 0.5f)
        outlined_text(draw, font, text_height, at, label, colors::kYellow, colors::kYellowShade, colors::kBlue, 3.0f * u);
    else
        draw->AddText(font, text_height, at, colors::kBlue, label.data(), label.data() + label.size());
    siren_lamp(draw, ImVec2(min.x + rounding, (min.y + max.y) * 0.5f), 10.0f * u * focus, layer.time());
    return item.pressed;
}

bool back_button(const Layer &layer, ImVec2 position) {
    if (layer.device() != Device::Mouse && layer.device() != Device::Touch) return false;
    const float u = layer.unit();
    ImFont *font = fonts().strong;
    const float size = 18.0f * u;
    constexpr std::string_view label = "Back";
    const ImVec2 extent = text_size(font, size, label);
    const ImVec2 box(extent.x + 52.0f * u, 40.0f * u);
    ImGui::SetCursorScreenPos(position);
    const ImRect bb(position, ImVec2(position.x + box.x, position.y + box.y));
    const Interaction item = interact(layer, "##back", bb, ImGuiItemFlags_NoNav);
    if (!item.visible) return false;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float rounding = box.y * 0.5f, edge = 3.0f * u;
    draw->AddRectFilled(ImVec2(bb.Min.x - edge, bb.Min.y - edge), ImVec2(bb.Max.x + edge, bb.Max.y + edge), colors::kBlue, rounding + edge);
    gradient_rect(draw, bb.Min, bb.Max, item.hovered || item.held ? colors::kYellow : colors::kWhite,
                  item.hovered || item.held ? colors::kYellowShade : colors::kHelmet, rounding);
    triangle(draw, ImVec2(bb.Min.x + 20.0f * u, bb.GetCenter().y), 9.0f * u, -1, colors::kBlue);
    draw->AddText(font, size, ImVec2(bb.Min.x + 34.0f * u, bb.GetCenter().y - extent.y * 0.5f), colors::kBlue,
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
    draw->AddText(body, size, ImVec2(bb.Min.x + 40.0f * u, bb.GetCenter().y - label_size.y * 0.5f), colors::kInk,
                  row.label.c_str());

    // The value between its arrows, in the right part of the row.
    const float value_width = std::min(300.0f * u, width * 0.48f);
    const ImRect value(ImVec2(bb.Max.x - value_width - 12.0f * u, bb.Min.y), ImVec2(bb.Max.x - 12.0f * u, bb.Max.y));
    const float arrow = 22.0f * u;
    const ImU32 arrow_color = mix(colors::kBlueLight, colors::kBlue, focus);
    triangle(draw, ImVec2(value.Min.x + arrow * 0.5f, value.GetCenter().y), 9.0f * u, -1, arrow_color);
    triangle(draw, ImVec2(value.Max.x - arrow * 0.5f, value.GetCenter().y), 9.0f * u, 1, arrow_color);
    const ImVec2 value_size = text_size(strong, size, row.value);
    draw->AddText(strong, size, ImVec2(value.GetCenter().x - value_size.x * 0.5f, value.GetCenter().y - value_size.y * 0.5f),
                  row.warning ? colors::kRed : colors::kInk, row.value.c_str());

    int change = 0;
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
        draw->AddRectFilled(ImVec2(bb.Min.x - edge, bb.Min.y - edge), ImVec2(bb.Max.x + edge, bb.Max.y + edge),
                            colors::kBlue, rounding + edge);
        if (active)
            gradient_rect(draw, bb.Min, bb.Max, colors::kYellow, colors::kYellowShade, rounding);
        else
            gradient_rect(draw, bb.Min, bb.Max, item.hovered ? colors::kBlueLight : IM_COL32(48, 96, 210, 255),
                          colors::kBlue, rounding);
        const auto label = tabs[static_cast<std::size_t>(i)];
        const ImVec2 extent = text_size(font, size, label);
        draw->AddText(font, size, ImVec2(bb.Min.x + pad, bb.GetCenter().y - extent.y * 0.45f),
                      active ? colors::kBlue : colors::kWhite, label.data(), label.data() + label.size());
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
    if (layer.device() != Device::Gamepad && layer.device() != Device::Keyboard) return;
    const float u = layer.unit();
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    ImFont *font = fonts().strong;
    const float size = 17.0f * u;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const SDL_GamepadButton confirm = layer.confirm_south() ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
    const SDL_GamepadButton cancel = layer.confirm_south() ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;

    // Right-aligned at the bottom: measured first, then drawn.
    const auto draw_glyph = [&](ImDrawList *target, ImVec2 &cursor, Control control) {
        const bool pad = layer.device() == Device::Gamepad;
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
        case Control::Browse:
            if (pad) face_button(target, cursor, u, layer.pad_type(), SDL_GAMEPAD_BUTTON_NORTH); else keycap(target, cursor, u, "Tab");
            break;
        }
    };
    float total = 0.0f;
    for (const Hint &hint : list) {
        ImVec2 cursor(0.0f, 0.0f);
        draw_glyph(nullptr, cursor, hint.control);
        total += cursor.x + 2.0f * u + text_size(font, size, hint.text).x + 18.0f * u;
    }
    ImVec2 cursor(display.x - total - 6.0f * u, display.y - 26.0f * u);
    for (const Hint &hint : list) {
        draw_glyph(draw, cursor, hint.control);
        cursor.x += 2.0f * u;
        const ImVec2 extent = text_size(font, size, hint.text);
        outlined_text(draw, font, size, ImVec2(cursor.x, cursor.y - extent.y * 0.5f), hint.text, colors::kWhite,
                      colors::kWhite, colors::kBlue, 2.5f * u);
        cursor.x += extent.x + 18.0f * u;
    }
}

} // namespace sarusaru::ui
