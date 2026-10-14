#pragma once

// Shift-JIS, the game's text encoding, to UTF-8 for the host's terminal and
// tools.
#include <string>
#include <string_view>

namespace sarusaru::text {

// Converts `text`; bytes that are not valid Shift-JIS (and every non-ASCII
// byte where the host has no converter) become \xNN escapes.
[[nodiscard]] std::string shift_jis_to_utf8(std::string_view text);

} // namespace sarusaru::text
