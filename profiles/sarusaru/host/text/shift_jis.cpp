#include "text/shift_jis.hpp"

#include <cstdio>

#if SARUSARU_HAS_ICONV
#include <iconv.h>
#endif

namespace sarusaru::text {
namespace {

void escape(std::string &out, unsigned char byte) {
    char hex[5];
    std::snprintf(hex, sizeof hex, "\\x%02X", byte);
    out += hex;
}

} // namespace

std::string shift_jis_to_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 3u / 2u);
#if SARUSARU_HAS_ICONV
    // CP932 is Microsoft's Shift-JIS, a superset of what PSP games use.
    iconv_t converter = iconv_open("UTF-8", "CP932");
    if (converter == reinterpret_cast<iconv_t>(-1)) converter = iconv_open("UTF-8", "SHIFT_JIS");
    if (converter != reinterpret_cast<iconv_t>(-1)) {
        std::size_t at = 0u;
        while (at < text.size()) {
            const auto byte = static_cast<unsigned char>(text[at]);
            if (byte < 0x80u) {
                out += static_cast<char>(byte);
                ++at;
                continue;
            }
            // One character: a lead byte, plus a trail byte for double-byte ones.
            const bool double_byte = (byte >= 0x81u && byte <= 0x9Fu) || (byte >= 0xE0u && byte <= 0xFCu);
            const std::size_t length = double_byte && at + 1u < text.size() ? 2u : 1u;
            char input[2] = {text[at], length == 2u ? text[at + 1u] : '\0'};
            char output[8];
            char *in = input, *result = output;
            std::size_t in_left = length, out_left = sizeof output;
            if (iconv(converter, &in, &in_left, &result, &out_left) != static_cast<std::size_t>(-1) && in_left == 0u) {
                out.append(output, sizeof output - out_left);
                at += length;
            } else {
                iconv(converter, nullptr, nullptr, nullptr, nullptr);
                escape(out, byte);
                ++at;
            }
        }
        iconv_close(converter);
        return out;
    }
#endif
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x80u) out += c;
        else escape(out, byte);
    }
    return out;
}

} // namespace sarusaru::text
