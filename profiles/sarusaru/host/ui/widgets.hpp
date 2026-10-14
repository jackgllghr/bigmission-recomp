#pragma once

// The controls the port's screens are built from, drawn in the series' style
// (ui/theme.hpp). Every one works the same way with each device (ui/layer.hpp):
// the focused control wears the yellow highlight and Pipo's red siren lamp,
// whether a gamepad or the keyboard moved there or the mouse hovers it; a
// finger shows the highlight only while it presses.
//
// Sizes are in layout units (Layer::unit()), so everything scales with the window.
#include "ui/layer.hpp"
#include "ui/settings_rows.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace sarusaru::ui {

// --- Drawing -------------------------------------------------------------

// The sky behind every screen: a slowly turning sunburst, drifting clouds and
// the logo's yellow swoosh along the bottom.
void draw_backdrop(const Layer &layer);

// Lettering in the logo's style: a vertical gradient fill inside a thick
// outline, over a drop shadow. `position` is the top left of the text.
void outlined_text(ImDrawList *draw, ImFont *font, float size, ImVec2 position, std::string_view text, ImU32 top,
                   ImU32 bottom, ImU32 outline, float thickness);
[[nodiscard]] ImVec2 text_size(ImFont *font, float size, std::string_view text, float wrap = 0.0f);

// Pipo's helmet lamp, pulsing.
void siren_lamp(ImDrawList *draw, ImVec2 center, float radius, float time);

// A rounded rectangle filled with a vertical gradient.
void gradient_rect(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom, float rounding);

// A screen's title, centred at the top. Returns the y below it.
float screen_title(const Layer &layer, std::string_view title);

// A white panel with the blue outline, behind content at `min`..`max`.
void panel(const Layer &layer, ImVec2 min, ImVec2 max);

// --- Focus ---------------------------------------------------------------

// The control with this ID (as given to the control) takes the focus when
// it is next drawn, e.g. the first control of a new screen, or the button
// that opened a screen once the player comes back.
void focus_control(std::string_view id);

// --- Controls --------------------------------------------------------------

// The big pill-shaped buttons of the main menu. `primary` is the red one.
bool menu_button(const Layer &layer, std::string_view id, std::string_view label, ImVec2 size, bool primary = false,
                 bool enabled = true);
// A small "Back" pill for the mouse and touch; drawn only for them, as
// the gamepad and keyboard have a button for it. True when pressed.
bool back_button(const Layer &layer, ImVec2 position);
// A setting with a value stepped by left/right, its arrows, or activating it
// (+1). Returns -1, 0 or +1.
int setting_row(const Layer &layer, std::string_view id, const RowView &row, float width);
// An action within a panel: label on the left, optional detail on the right.
bool action_row(const Layer &layer, std::string_view id, std::string_view label, std::string_view detail,
                float width, bool enabled = true);
enum class ListIcon { Folder, Up, Disc, File, Place };
// An entry in the file browser.
bool list_row(const Layer &layer, std::string_view id, ListIcon icon, std::string_view name, std::string_view detail,
              float width, bool highlight = false);
// Tabs at `y`, centred, switched with L/R (Q/W on the keyboard) or a tap.
// Returns the selected tab.
int tab_bar(const Layer &layer, std::span<const std::string_view> tabs, int selected, float y);

// A scrolling list inside a panel: dragged with a finger, scrolled with a
// wheel, and following the focus. Pair with end_list().
void begin_list(std::string_view id, ImVec2 size);
void end_list(const Layer &layer);

// Wrapped body text at the cursor.
void paragraph(const Layer &layer, std::string_view text, float width, ImU32 color = colors::kInk);

// --- Button hints ----------------------------------------------------------

enum class Control { Confirm, Back, Change, Tabs, Browse };
struct Hint {
    Control control;
    std::string_view text;
};
// The bar along the bottom right, drawn with the glyphs of the device in use
// (PlayStation shapes, Xbox or Nintendo letters, or keyboard keys). Nothing
// for the mouse or touch: everything on screen can be pressed instead.
void hints(const Layer &layer, std::initializer_list<Hint> list);

} // namespace sarusaru::ui
