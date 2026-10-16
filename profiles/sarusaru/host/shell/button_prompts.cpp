#include "shell/button_prompts.hpp"
#include "psprecomp/sha256.hpp"
#include "ui/theme.hpp"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace sarusaru::shell {
ButtonPrompts::ButtonPrompts() {
    std::ifstream file(ui::resource_path("fonts/MPLUSRounded1c-Bold.ttf"), std::ios::binary);
    font_.assign(std::istreambuf_iterator<char>(file), {});
}
void ButtonPrompts::configure(const ControlSettings &controls, ui::BindingDevice device, SDL_GamepadType type) {
    if (controls_ == controls && device_ == device && type_ == type) return;
    controls_ = controls;
    device_ = device;
    type_ = type;
    ++revision_;
}
unsigned ButtonPrompts::identify(const gpu::TextureState &t, std::span<const std::uint32_t> pixels) {
    // common00/Keypallete.tm2 and common/e_but_{0,1}.tm2.
    // Hash only logical pixels: power-of-two padding contains unrelated data.
    unsigned atlas = 0, width = 0, height = 0;
    std::string_view expected;
    if (t.width == 128 && t.buffer_width == 96 && t.height == 32) {
        atlas = 1; width = 96; height = 24;
        expected = "f5cf92ddc8b96abbee09854cfc712793ad9d4ea48583ac54d2196b6d2b734361";
    } else if (t.width == 128 && t.buffer_width == 128 && t.height == 128) {
        atlas = 2; width = 128; height = 120;
        expected = "09466c6d2b869d6fff2a00acb79b11a16829f77938ec22cab1786c5401c9705b";
    } else if (t.width == 128 && t.buffer_width == 128 && t.height == 256) {
        atlas = 3; width = 128; height = 240;
        expected = "314efab32a5541b6f55f27af74d5f059dab420144aa18e9ab11cac3edd1d6a53";
    } else return 0;
    if (pixels.size() < static_cast<std::size_t>(t.width) * height) return 0;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(width * height * 4);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::uint8_t>(pixels[y * t.width + x] >> shift));
    return psprecomp::sha256(bytes) == expected ? atlas : 0;
}
void ButtonPrompts::draw(std::vector<std::uint32_t> &pixels, unsigned width, unsigned scale,
                         int x, int y, int w, int h, Action action) const {
    const auto &bindings = (device_ == ui::BindingDevice::Keyboard ? controls_.keyboard : controls_.gamepad)
        [static_cast<std::size_t>(action)];
    // Never leave a misleading PSP button when an action is unbound.
    const auto glyph = bindings.empty() ? ui::Glyph{ui::Glyph::Kind::Key, "-"} : ui::glyph(bindings.front(), type_);
    std::string label = glyph.label;
    const bool face = glyph.kind == ui::Glyph::Kind::Face;
    const bool xbox = face && (type_ == SDL_GAMEPAD_TYPE_XBOX360 || type_ == SDL_GAMEPAD_TYPE_XBOXONE);
    const bool ps = type_ == SDL_GAMEPAD_TYPE_PS3 || type_ == SDL_GAMEPAD_TYPE_PS4 || type_ == SDL_GAMEPAD_TYPE_PS5;
    const bool nintendo = type_ >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && type_ <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    if (face && !ps) {
        switch (glyph.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: label = nintendo ? "B" : "A"; break;
        case SDL_GAMEPAD_BUTTON_EAST: label = nintendo ? "A" : "B"; break;
        case SDL_GAMEPAD_BUTTON_WEST: label = nintendo ? "Y" : "X"; break;
        case SDL_GAMEPAD_BUTTON_NORTH: label = nintendo ? "X" : "Y"; break;
        default: break;
        }
    }
    std::uint32_t ink = ui::colors::kWhite;
    if (xbox) {
        if (label == "A") ink = ui::colors::kXboxA;
        else if (label == "B") ink = ui::colors::kXboxB;
        else if (label == "X") ink = ui::colors::kXboxX;
        else if (label == "Y") ink = ui::colors::kXboxY;
    }
    x *= scale; y *= scale; w *= scale; h *= scale;
    const auto put = [&](int px, int py, std::uint32_t color) {
        if (px >= 0 && px < static_cast<int>(width) && py >= 0 &&
            static_cast<std::size_t>(py) * width + px < pixels.size()) {
            // Atlas pixels use straight alpha. Composite coverage over the
            // restored bar (or the other button in a diagonal pair).
            auto &background = pixels[py * width + px];
            const unsigned alpha = color >> 24, behind = background >> 24;
            const unsigned weight = alpha * 255 + behind * (255 - alpha);
            if (!weight) return;
            std::uint32_t blended = ((weight + 127) / 255) << 24;
            for (unsigned shift = 0; shift < 24; shift += 8)
                blended |= ((((color >> shift) & 255u) * alpha * 255 +
                             ((background >> shift) & 255u) * behind * (255 - alpha) + weight / 2) /
                            weight) << shift;
            background = blended;
        }
    };
    const float cx = x + w * .5f, cy = y + h * .5f, r = std::min(w, h) * .45f;
    for (int py = y; py < y + h; ++py) for (int px = x; px < x + w; ++px) {
        const float dx = px + .5f - cx, dy = py + .5f - cy;
        // Signed distance gives circles and rounded keycaps a one-texel
        // antialiased edge at any atlas resolution.
        const float corner = std::min(w, h) * .18f;
        const float qx = std::abs(dx) - (w * .48f - corner);
        const float qy = std::abs(dy) - (h * .43f - corner);
        const float distance = face ? std::sqrt(dx * dx + dy * dy) - r :
            std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - corner;
        const float coverage = std::clamp(.5f - distance, 0.f, 1.f);
        if (coverage == 0.f) continue;
        const float border = std::clamp(distance + scale + .5f, 0.f, 1.f);
        const std::uint32_t fill = xbox ? ui::colors::kXboxFace : 0xff30251cu;
        constexpr std::uint32_t outline = 0xffc7d6e6u;
        std::uint32_t color = static_cast<std::uint32_t>(std::lround(coverage * 255.f)) << 24;
        for (unsigned shift = 0; shift < 24; shift += 8)
            color |= static_cast<std::uint32_t>(std::lround(
                ((fill >> shift) & 255u) * (1.f - border) + ((outline >> shift) & 255u) * border)) << shift;
        put(px, py, color);
    }
    if (face && ps) {
        const float a = r * .52f, stroke = std::max(1.f, scale * .75f);
        for (int py = y; py < y + h; ++py) for (int px = x; px < x + w; ++px) {
            const float dx = px + .5f - cx, dy = py + .5f - cy;
            bool ink = false; std::uint32_t color = 0xffffffffu;
            switch (glyph.button) {
            case SDL_GAMEPAD_BUTTON_EAST: ink = std::abs(std::sqrt(dx*dx+dy*dy)-a) < stroke; color = 0xff6868efu; break;
            case SDL_GAMEPAD_BUTTON_WEST: ink = std::abs(std::max(std::abs(dx),std::abs(dy))-a) < stroke; color = 0xffc47be6u; break;
            case SDL_GAMEPAD_BUTTON_SOUTH: ink = std::abs(std::abs(dx)-std::abs(dy)) < stroke && std::max(std::abs(dx),std::abs(dy)) < a; color = 0xffeeaa78u; break;
            case SDL_GAMEPAD_BUTTON_NORTH: ink = (std::abs(dy-a) < stroke && std::abs(dx) < a) ||
                (dy >= -a && dy <= a && std::abs(std::abs(dx)-(dy+a)*.5f) < stroke); color = 0xffa4d65cu; break;
            default: break;
            }
            if (ink) put(px, py, color);
        }
        return;
    }
    if (font_.empty()) return;
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, font_.data(), 0)) return;
    std::vector<int> codes;
    const char *at = label.c_str();
    while (*at) { size_t remaining = label.c_str() + label.size() - at; codes.push_back(SDL_StepUTF8(&at, &remaining)); }
    float em = stbtt_ScaleForPixelHeight(&info, h * .78f), advance = 0;
    for (int code : codes) { int a; stbtt_GetCodepointHMetrics(&info, code, &a, nullptr); advance += a * em; }
    if (advance > w * .83f) em *= w * .83f / advance;
    int ascent, descent; stbtt_GetFontVMetrics(&info, &ascent, &descent, nullptr);
    float pen = cx - advance * (em / stbtt_ScaleForPixelHeight(&info, h * .78f)) * .5f;
    const float baseline = cy + (ascent + descent) * em * .5f;
    for (int code : codes) {
        int bw, bh, bx, by, a;
        auto *bitmap = stbtt_GetCodepointBitmap(&info, em, em, code, &bw, &bh, &bx, &by);
        for (int j = 0; j < bh; ++j) for (int i = 0; i < bw; ++i) {
            const int px = static_cast<int>(pen) + bx + i, py = static_cast<int>(baseline) + by + j;
            if (px < x || px >= x+w || py < y || py >= y+h) continue;
            const unsigned alpha = bitmap[j*bw+i];
            if (alpha) put(px, py, (ink & 0x00ffffffu) | (alpha << 24));
        }
        stbtt_FreeBitmap(bitmap, nullptr);
        stbtt_GetCodepointHMetrics(&info, code, &a, nullptr); pen += a * em;
    }
}
void ButtonPrompts::apply(unsigned atlas, std::vector<std::uint32_t> &pixels, unsigned width, unsigned scale) const {
    if (font_.empty()) return; // Retain the original if packaged resources are missing.
    const auto clear = [&](unsigned x, unsigned y, unsigned w, unsigned h) {
        for (unsigned py = y * scale; py < (y + h) * scale; ++py)
            for (unsigned px = x * scale; px < (x + w) * scale && px < width; ++px) {
                const auto offset = static_cast<std::size_t>(py) * width + px;
                if (offset < pixels.size()) pixels[offset] = 0;
            }
    };
    const auto restore_bar = [&](unsigned row, unsigned left, unsigned count, bool top) {
        // The opposite end is free of controller artwork. Mirror its cap and
        // extend its interior, retaining native/translated colors and HD alpha.
        const unsigned first_y = top ? 0 : 14;
        const unsigned last_y = top ? 10 : 24;
        for (unsigned y = first_y * scale; y < last_y * scale; ++y)
            for (unsigned x = left * scale; x < (left + count) * scale && x < width; ++x) {
                if (x >= 111 * scale) continue;
                const unsigned source_x = top ? (left == 0 ? std::min(x, 8 * scale) :
                    std::min(111 * scale - 1 - x, 8 * scale)) :
                    std::max(111 * scale - 1 - x, 104 * scale);
                // Row 6 has a button over the left cap; row 0 has the same
                // top bar and an unobscured cap.
                const unsigned source_y = top ? y : row * 24 * scale + y;
                const auto source = static_cast<std::size_t>(source_y) * width + source_x;
                const auto destination = static_cast<std::size_t>(row * 24 * scale + y) * width + x;
                if (source < pixels.size() && destination < pixels.size())
                    pixels[destination] = pixels[source];
            }
    };
    const auto button = [&](int x, int y, int w, int h, Action a) { draw(pixels, width, scale, x,y,w,h,a); };
    if (atlas == 1) {
        clear(0, 0, 96, 24);
        constexpr Action actions[] = {Action::Square, Action::Triangle, Action::Circle, Action::Cross};
        for (int i = 0; i < 4; ++i) button(i*24, 0, 24, 24, actions[i]);
    } else if (atlas == 2) {
        for (int i = 0; i < 5; ++i) {
            clear(0, i * 24, 32, 24);
            restore_bar(i, 0, 32, false);
            button(0, i*24+6, 26, 14, i < 3 ? Action::Start : Action::Select);
        }
    } else if (atlas == 3) {
        // The PSP artwork extends around and between the replacement glyphs.
        // Remove its entire region, after translation/HD art, not just the
        // smaller rectangles occupied by our buttons.
        for (unsigned row = 0; row < 9; ++row) {
            const unsigned left = row < 2 ? 88 : 0, count = row < 2 ? 40 : 32;
            clear(left, row * 24, count, 24);
            restore_bar(row, left, count, row < 2 || row == 6);
        }
        // Paired icons are alternatives, not a chord. Each retains its own binding.
        for (int row : {0,1,4}) {
            const int x = row < 2 ? 88 : 0;
            button(x, row*24+8, 14, 16, Action::Square);
            button(x+8, row*24, 14, 16, Action::Triangle);
        }
        for (int row : {2,3,7,8}) {
            button(0, row*24+8, 14, 16, Action::Cross);
            button(8, row*24, 14, 16, Action::Circle);
        }
        button(10, 5*24+5, 14, 16, Action::Circle);
        button(10, 6*24+5, 14, 16, Action::Cross);
    }
}
} // namespace sarusaru::shell
