#include "ui/theme.hpp"

#include "platform/paths.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <string>

namespace sarusaru::ui {
namespace {

Fonts g_fonts;

ImFont *load(const char *file) {
    const auto path = resource_path(std::string("fonts/") + file);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        std::cerr << "sarusaru: missing interface font " << path.string() << '\n';
        return nullptr;
    }
    // Dear ImGui opens font files by UTF-8 name on every platform.
    const auto utf8 = path.u8string();
    ImFontConfig config;
    config.OversampleH = 2;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(std::string(utf8.begin(), utf8.end()).c_str(), 0.0f, &config);
}

} // namespace

const Fonts &fonts() { return g_fonts; }

std::filesystem::path resource_path(std::string_view name) {
    const auto directory = platform::shipped_files_directory();
    return (directory ? *directory : std::filesystem::path()) / "resources" /
           std::filesystem::path(std::u8string(name.begin(), name.end()));
}

bool load_fonts() {
    g_fonts.body = load("MPLUSRounded1c-Bold.ttf");
    // The game uses an ASCII subset of this face. Keep the full font here
    // so the launcher uses the same lettering in both English and Japanese.
    const bool complete = g_fonts.body != nullptr;
    ImFont *fallback = g_fonts.body != nullptr ? g_fonts.body : ImGui::GetIO().Fonts->AddFontDefaultVector();
    if (g_fonts.body == nullptr) g_fonts.body = fallback;
    g_fonts.strong = g_fonts.body;
    g_fonts.display = g_fonts.body;
    ImGui::GetIO().FontDefault = g_fonts.body;
    return complete;
}

void apply_style(float unit) {
    ImGuiStyle style;
    ImGui::StyleColorsLight(&style);
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FramePadding = ImVec2(12.0f * unit, 8.0f * unit);
    style.ItemSpacing = ImVec2(8.0f * unit, 6.0f * unit);
    style.ScrollbarSize = 10.0f * unit;
    style.ScrollbarRounding = 5.0f * unit;
    style.FrameRounding = 10.0f * unit;
    style.FontSizeBase = 22.0f * unit;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(colors::kInk);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(colors::kBlueLight);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(colors::kBlue);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(colors::kBlueDeep);
    // The custom widgets draw their own focus; Dear ImGui's frame would sit
    // on top of it.
    style.Colors[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
    ImGui::GetStyle() = style;
}

} // namespace sarusaru::ui
