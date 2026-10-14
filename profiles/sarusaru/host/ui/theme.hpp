#pragma once

// The look of the launcher and the port's screens, after the series' own:
// chunky banana-yellow lettering with thick royal-blue outlines, red accents,
// Pipo's white helmet and its siren lamp, under a bright sky.
#include "imgui.h"

#include <filesystem>
#include <string_view>

namespace sarusaru::ui {

namespace colors {
inline constexpr ImU32 kSkyTop = IM_COL32(28, 124, 228, 255);
inline constexpr ImU32 kSkyBottom = IM_COL32(150, 216, 255, 255);
inline constexpr ImU32 kCloud = IM_COL32(255, 255, 255, 235);
inline constexpr ImU32 kYellow = IM_COL32(255, 222, 23, 255);
inline constexpr ImU32 kYellowShade = IM_COL32(255, 168, 0, 255);
inline constexpr ImU32 kYellowPale = IM_COL32(255, 244, 176, 255);
inline constexpr ImU32 kBlue = IM_COL32(16, 48, 168, 255);       // the outline around everything
inline constexpr ImU32 kBlueDeep = IM_COL32(8, 26, 102, 255);    // shadows under outlines
inline constexpr ImU32 kBlueLight = IM_COL32(64, 132, 240, 255);
inline constexpr ImU32 kRed = IM_COL32(232, 22, 30, 255);
inline constexpr ImU32 kRedDark = IM_COL32(160, 10, 16, 255);
inline constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 kHelmet = IM_COL32(226, 234, 246, 255);   // the helmet's shaded white
inline constexpr ImU32 kInk = IM_COL32(18, 34, 96, 255);         // text on white
inline constexpr ImU32 kInkDim = IM_COL32(86, 104, 150, 255);
inline constexpr ImU32 kDisabled = IM_COL32(170, 180, 200, 255);
inline constexpr ImU32 kGood = IM_COL32(30, 170, 70, 255);
inline constexpr ImU32 kShadow = IM_COL32(8, 26, 102, 90);
// Face buttons as PlayStation colours them.
inline constexpr ImU32 kPsCircle = IM_COL32(236, 66, 72, 255);
inline constexpr ImU32 kPsCross = IM_COL32(96, 148, 236, 255);
inline constexpr ImU32 kPsTriangle = IM_COL32(40, 190, 150, 255);
inline constexpr ImU32 kPsSquare = IM_COL32(230, 110, 190, 255);
} // namespace colors

// The faces, loaded once. Sizes are chosen per draw (Dear ImGui 1.92 renders
// glyphs on demand at any size).
struct Fonts {
    ImFont *display{};  // Luckiest Guy: titles and big buttons, Latin only
    ImFont *body{};     // M PLUS Rounded 1c Bold: everything else, with Japanese
    ImFont *strong{};   // M PLUS Rounded 1c ExtraBold
};
[[nodiscard]] const Fonts &fonts();
// A file in the resources folder beside the executable (fonts, the title art).
[[nodiscard]] std::filesystem::path resource_path(std::string_view name);
// Loads the faces from the resources folder beside the executable, falling
// back to Dear ImGui's own when one is missing. False if any was missing.
bool load_fonts();

// Dear ImGui's own style, for anything drawn without the custom widgets.
void apply_style(float unit);

} // namespace sarusaru::ui
