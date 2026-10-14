#include "text/shift_jis.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    using sarusaru::text::shift_jis_to_utf8;
    require(shift_jis_to_utf8("plain ASCII\n") == "plain ASCII\n", "ASCII changed");
#if SARUSARU_HAS_ICONV
    // サル (saru) in Shift-JIS: 83 54 83 8B.
    require(shift_jis_to_utf8("\x83\x54\x83\x8B!") == "\xE3\x82\xB5\xE3\x83\xAB!", "katakana not converted");
    // A lone lead byte at the end cannot be decoded.
    require(shift_jis_to_utf8("a\x83") == "a\\x83", "truncated character not escaped");
#else
    require(shift_jis_to_utf8("\x83\x54") == "\\x83\\x54", "bytes not escaped without a converter");
#endif
    std::cout << "Shift-JIS tests passed\n";
    return 0;
}
