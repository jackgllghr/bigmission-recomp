#pragma once

// The look of the launcher and the port's screens, after the series' box art:
// light-blue rays and white star badges behind chunky yellow-to-orange
// lettering with thick navy-purple outlines, red and helmet-silver buttons
// that sit on a solid shadow, and a purple star marking the focus.
#include "imgui.h"

#include <filesystem>
#include <string_view>

namespace sarusaru::ui {

namespace colors {
inline constexpr ImU32 kRayDark = IM_COL32(42, 149, 230, 255);   // the sunburst's alternating rays
inline constexpr ImU32 kRayLight = IM_COL32(92, 188, 248, 255);
inline constexpr ImU32 kGlow = IM_COL32(255, 255, 255, 190);      // the bright centre of the rays
inline constexpr ImU32 kVignette = IM_COL32(24, 50, 150, 97);     // darkening towards the corners
inline constexpr ImU32 kStarShadow = IM_COL32(22, 96, 190, 115);  // under the white star badges
inline constexpr ImU32 kYellow = IM_COL32(255, 210, 31, 255);
inline constexpr ImU32 kYellowShade = IM_COL32(229, 138, 0, 255);
inline constexpr ImU32 kYellowPale = IM_COL32(255, 245, 168, 255);
inline constexpr ImU32 kLetterTop = IM_COL32(255, 242, 122, 255); // the title lettering's fill, top to bottom
inline constexpr ImU32 kLetterMid = IM_COL32(255, 198, 28, 255);
inline constexpr ImU32 kLetterBottom = IM_COL32(242, 90, 0, 255);
inline constexpr ImU32 kBlue = IM_COL32(42, 27, 110, 255);        // the navy-purple outline around everything
inline constexpr ImU32 kBlueDeep = IM_COL32(26, 15, 74, 255);     // shadows under outlines
inline constexpr ImU32 kBlueLight = IM_COL32(120, 110, 190, 255);
inline constexpr ImU32 kStar = IM_COL32(59, 42, 147, 255);        // the focus star
inline constexpr ImU32 kRed = IM_COL32(238, 40, 48, 255);
inline constexpr ImU32 kRedLight = IM_COL32(255, 97, 102, 255);
inline constexpr ImU32 kRedDark = IM_COL32(195, 18, 27, 255);
inline constexpr ImU32 kRedInk = IM_COL32(122, 10, 18, 255);      // the drop under white text on red
inline constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 kHelmet = IM_COL32(220, 229, 243, 255);    // the helmet's shaded white
inline constexpr ImU32 kRowStripe = IM_COL32(241, 245, 251, 255);
inline constexpr ImU32 kInk = IM_COL32(42, 27, 110, 255);         // text on white
inline constexpr ImU32 kInkDim = IM_COL32(74, 79, 122, 255);
inline constexpr ImU32 kDisabled = IM_COL32(138, 144, 181, 255);
inline constexpr ImU32 kGood = IM_COL32(31, 166, 74, 255);
inline constexpr ImU32 kShadow = IM_COL32(42, 27, 110, 255);      // the solid drop under buttons and panels
// Face buttons as PlayStation colours them.
inline constexpr ImU32 kPsCircle = IM_COL32(236, 66, 72, 255);
inline constexpr ImU32 kPsCross = IM_COL32(96, 148, 236, 255);
inline constexpr ImU32 kPsTriangle = IM_COL32(40, 190, 150, 255);
inline constexpr ImU32 kPsSquare = IM_COL32(230, 110, 190, 255);
inline constexpr ImU32 kXboxFace = IM_COL32(0, 0, 0, 255);
inline constexpr ImU32 kXboxA = IM_COL32(110, 200, 80, 255);
inline constexpr ImU32 kXboxB = IM_COL32(240, 80, 70, 255);
inline constexpr ImU32 kXboxX = IM_COL32(80, 150, 240, 255);
inline constexpr ImU32 kXboxY = IM_COL32(250, 200, 40, 255);
} // namespace colors

// The faces, loaded once. Sizes are chosen per draw (Dear ImGui 1.92 renders
// glyphs on demand at any size).
struct Fonts {
    ImFont *display{};  // M PLUS Rounded 1c Bold: titles and big buttons
    ImFont *body{};     // M PLUS Rounded 1c Bold: everything else, with Japanese
    ImFont *strong{};   // Same face for emphasised text
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
