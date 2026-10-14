#pragma once

// The launcher's text in English and Japanese (ui/texts.inc). Screens ask for
// text by ID; the language is chosen in Settings, or follows the system's.
#include "shell/settings.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace sarusaru::ui {

enum class Text {
#define X(id, english, japanese) id,
#include "ui/texts.inc"
#undef X
    Count
};

// The language to show for a setting: Auto becomes Japanese when the
// system's first preferred language is Japanese, English otherwise.
[[nodiscard]] shell::Language resolve_language(shell::Language setting);
// English or Japanese (from resolve_language).
void set_language(shell::Language language);
[[nodiscard]] shell::Language language();

[[nodiscard]] std::string_view tr(Text text);
[[nodiscard]] std::string_view tr(Text text, shell::Language language);
// The text with {0}, {1}... replaced by `arguments`.
[[nodiscard]] std::string format(Text text, std::initializer_list<std::string_view> arguments);
// A language's name in that language: "English", "日本語".
[[nodiscard]] std::string_view language_name(shell::Language language);

} // namespace sarusaru::ui
