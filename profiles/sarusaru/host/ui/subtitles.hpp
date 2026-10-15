#pragma once

// A movie's subtitle line (movie/subtitles.hpp) as drawn over the game's
// picture: white outlined text centred near the bottom, scaled with the
// picture and wrapped to its width.
#include "imgui.h"

#include <string_view>

namespace sarusaru::ui {

// Into the foreground draw list of the current frame (Layer::present_over),
// for a picture at `origin` of `size` points.
void draw_subtitle(std::string_view text, ImVec2 origin, ImVec2 size);

} // namespace sarusaru::ui
