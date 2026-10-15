#include "ui/subtitles.hpp"

#include "ui/theme.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace sarusaru::ui {

void draw_subtitle(std::string_view text, ImVec2 origin, ImVec2 size) {
    ImFont *font = fonts().strong != nullptr ? fonts().strong : ImGui::GetFont();
    // About 16 of the PSP's 272 lines tall.
    const float font_size = size.y * 0.06f;
    const float wrap_width = size.x * 0.86f;
    const float line_height = font_size * 1.18f;
    // The text's own lines, each wrapped to the picture's width.
    std::vector<std::string_view> lines;
    for (std::size_t at = 0; at <= text.size();) {
        const auto end = std::min(text.find('\n', at), text.size());
        const char *begin = text.data() + at, *stop = text.data() + end;
        while (begin < stop) {
            const char *wrap = font->CalcWordWrapPosition(font_size, begin, stop, wrap_width);
            if (wrap == begin) wrap = begin + 1;
            std::string_view line(begin, static_cast<std::size_t>(wrap - begin));
            while (!line.empty() && line.back() == ' ') line.remove_suffix(1);
            lines.push_back(line);
            begin = wrap;
            while (begin < stop && *begin == ' ') ++begin;
        }
        at = end + 1;
    }
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const float outline = std::max(1.0f, font_size * 0.07f);
    float y = origin.y + size.y * 0.95f - line_height * static_cast<float>(lines.size());
    for (const auto line : lines) {
        const char *begin = line.data(), *end = line.data() + line.size();
        const float width = font->CalcTextSizeA(font_size, wrap_width * 2.0f, 0.0f, begin, end).x;
        const ImVec2 at{origin.x + (size.x - width) / 2.0f, y};
        // A dark outline keeps it readable over any scene, then the text.
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx != 0 || dy != 0)
                    draw->AddText(font, font_size, {at.x + dx * outline, at.y + dy * outline}, IM_COL32(0, 0, 0, 220),
                                  begin, end);
        draw->AddText(font, font_size, at, IM_COL32(255, 255, 255, 255), begin, end);
        y += line_height;
    }
}

} // namespace sarusaru::ui
