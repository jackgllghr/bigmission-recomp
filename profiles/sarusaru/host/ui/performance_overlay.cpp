#include "ui/performance_overlay.hpp"

#include "ui/theme.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace sarusaru::ui {
namespace {

constexpr ImU32 kLabel = IM_COL32(170, 200, 255, 255);
constexpr ImU32 kValue = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kUnit = IM_COL32(200, 200, 200, 255);
constexpr ImU32 kDim = IM_COL32(185, 185, 185, 255);
constexpr ImU32 kGood = IM_COL32(120, 230, 120, 255);
constexpr ImU32 kFair = IM_COL32(250, 210, 80, 255);
constexpr ImU32 kPoor = IM_COL32(250, 100, 90, 255);

std::string printed(const char *pattern, double value) {
    char text[32];
    std::snprintf(text, sizeof text, pattern, value);
    return text;
}

// Green near the frame rate asked for, yellow below it, red well below.
ImU32 rate_colour(float fps, float target) {
    if (target <= 0.0f) return kValue;
    if (fps >= target * 0.95f) return kGood;
    return fps >= target * 0.75f ? kFair : kPoor;
}

struct Piece {
    std::string text;
    ImU32 colour;
    bool small{};
};
// A row of the panel: a label column, then pieces.
struct Row {
    std::string label;
    std::vector<Piece> pieces;
    bool graph{};
};

} // namespace

void draw_performance_overlay(shell::PerformanceOverlay level, const shell::PerformanceStats &stats,
                              const PerformanceInfo &info, ImVec2 display) {
    using shell::PerformanceOverlay;
    if (level == PerformanceOverlay::Off) return;
    // Without its Vulkan layer, MangoHud's choice shows the detailed overlay
    // (the shell passes Off when the layer draws its own).
    const bool detailed = level != PerformanceOverlay::Fps;
    const auto &s = stats.summary();
    ImFont *font = fonts().body != nullptr ? fonts().body : ImGui::GetFont();
    const float size = std::clamp(display.y * 0.024f, 13.0f, 22.0f);
    const float small = size * 0.78f;
    const float pad = size * 0.45f, gap = size * 0.35f;

    std::vector<Row> rows;
    const auto rate = [&](float fps) { return s.valid ? printed("%.0f", fps) : std::string("--"); };
    rows.push_back({"FPS", {{rate(s.fps), rate_colour(s.fps, info.target_fps)},
                            {s.valid ? printed("%.1f ms", s.frame_ms) : std::string(), kUnit, true}}});
    if (detailed) {
        rows.push_back({"1% low", {{rate(s.one_percent_low), rate_colour(s.one_percent_low, info.target_fps)},
                                   {s.valid ? printed("max %.1f ms", s.worst_ms) : std::string(), kUnit, true}}});
        // The game's own frames (30 a second in most scenes) and its speed:
        // under 100% the game itself is slowing down, not just the display.
        const float speed = s.speed * 100.0f;
        rows.push_back({"Game", {{rate(s.game_fps), kValue},
                                 {s.valid ? printed("%.0f%% speed", speed) : std::string(),
                                  !s.valid || (speed >= 97.0f && speed <= 103.0f) ? kUnit : speed < 97.0f ? kPoor : kFair, true}}});
        rows.push_back({"CPU", {{s.valid ? printed("%.1f", s.cpu_ms) : std::string("--"), kValue}, {"ms", kUnit, true}}});
        if (s.gpu_known)
            rows.push_back({"GPU", {{s.valid ? printed("%.1f", s.gpu_ms) : std::string("--"), kValue}, {"ms", kUnit, true}}});
        rows.push_back({"", {}, true});
    }

    // Measured first, for the panel behind.
    const auto width_of = [&](const std::string &text, float at) {
        return font->CalcTextSizeA(at, 10000.0f, 0.0f, text.c_str()).x;
    };
    float label_width = 0.0f, content_width = 0.0f;
    for (const auto &row : rows) {
        label_width = std::max(label_width, width_of(row.label, size));
        float width = 0.0f;
        for (const auto &piece : row.pieces)
            if (!piece.text.empty()) width += width_of(piece.text, piece.small ? small : size) + gap;
        content_width = std::max(content_width, width);
    }
    std::vector<std::string> details;
    if (detailed) {
        details.push_back(info.gpu);
        details.push_back(info.api);
        details.push_back(std::to_string(info.render_width) + "×" + std::to_string(info.render_height) + " (" +
                          std::to_string(info.scale) + "×) · " + info.frame_rate);
        details.push_back("SaruSaru Recomp " + info.version + " · " + info.platform);
    }
    const float graph_height = size * 2.4f;
    float panel_width = label_width + gap * 2.0f + content_width;
    if (detailed) panel_width = std::max(panel_width, size * 11.0f);
    for (const auto &line : details) panel_width = std::max(panel_width, width_of(line, small));
    float panel_height = 0.0f;
    for (const auto &row : rows) panel_height += (row.graph ? graph_height + gap : size * 1.15f);
    panel_height += static_cast<float>(details.size()) * small * 1.2f;

    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImVec2 origin{pad, pad};
    draw->AddRectFilled(origin, {origin.x + panel_width + pad * 2.0f, origin.y + panel_height + pad * 2.0f},
                        IM_COL32(0, 0, 0, 165), size * 0.3f);
    float y = origin.y + pad;
    const float left = origin.x + pad, values = left + label_width + gap * 2.0f;
    for (const auto &row : rows) {
        if (row.graph) {
            // The recent frame times, scaled to the slowest (at least two
            // frames of the target), with the target's own time marked.
            const float target_ms = info.target_fps > 0.0f ? 1000.0f / info.target_fps : 16.7f;
            const float top_ms = std::max(s.worst_ms, target_ms * 2.0f);
            const ImVec2 a{left, y}, b{left + panel_width, y + graph_height};
            draw->AddRectFilled(a, b, IM_COL32(255, 255, 255, 18));
            const float target_y = b.y - (target_ms / top_ms) * graph_height;
            draw->AddLine({a.x, target_y}, {b.x, target_y}, IM_COL32(120, 230, 120, 90));
            const std::size_t count = stats.count();
            if (count >= 2u) {
                const auto &times = stats.frame_times();
                std::vector<ImVec2> points;
                points.reserve(count);
                const float step = panel_width / static_cast<float>(shell::PerformanceStats::kHistory - 1u);
                const float start = b.x - step * static_cast<float>(count - 1u);
                for (std::size_t i = 0; i < count; ++i) {
                    const float ms = std::min(times[(stats.offset() + i) % shell::PerformanceStats::kHistory], top_ms);
                    points.push_back({start + step * static_cast<float>(i), b.y - (ms / top_ms) * graph_height});
                }
                draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(255, 210, 90, 255),
                                  std::max(1.0f, size * 0.08f));
            }
            const std::string top = printed("%.0f ms", top_ms);
            draw->AddText(font, small * 0.85f, {b.x - width_of(top, small * 0.85f) - 2.0f, a.y}, kDim, top.c_str());
            y += graph_height + gap;
            continue;
        }
        draw->AddText(font, size, {left, y}, kLabel, row.label.c_str());
        float x = values;
        for (const auto &piece : row.pieces) {
            if (piece.text.empty()) continue;
            const float at = piece.small ? small : size;
            // Smaller pieces share the line's baseline.
            draw->AddText(font, at, {x, y + (size - at) * 0.8f}, piece.colour, piece.text.c_str());
            x += width_of(piece.text, at) + gap;
        }
        y += size * 1.15f;
    }
    for (const auto &line : details) {
        draw->AddText(font, small, {left, y}, kDim, line.c_str());
        y += small * 1.2f;
    }
}

} // namespace sarusaru::ui
