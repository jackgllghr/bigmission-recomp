#include "text/shift_jis.hpp"

#include <cstdio>

#if SARUSARU_HAS_ICONV
#include <iconv.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sarusaru::text {
namespace {

void escape(std::string &out, unsigned char byte) {
    char hex[5];
    std::snprintf(hex, sizeof hex, "\\x%02X", byte);
    out += hex;
}

// Decodes one Shift-JIS character (one or two bytes) to UTF-8 with the
// platform's converter: iconv, or Windows' code page 932.
class Decoder {
public:
#if SARUSARU_HAS_ICONV
    // CP932 is Microsoft's Shift-JIS, a superset of what PSP games use.
    Decoder() : converter_(iconv_open("UTF-8", "CP932")) {
        if (converter_ == kNone) converter_ = iconv_open("UTF-8", "SHIFT_JIS");
    }
    ~Decoder() {
        if (converter_ != kNone) iconv_close(converter_);
    }
    [[nodiscard]] bool available() const { return converter_ != kNone; }
    bool decode(const char *bytes, std::size_t length, std::string &out) {
        char input[2] = {bytes[0], length == 2u ? bytes[1] : '\0'};
        char output[8];
        char *in = input, *result = output;
        std::size_t in_left = length, out_left = sizeof output;
        if (iconv(converter_, &in, &in_left, &result, &out_left) != static_cast<std::size_t>(-1) && in_left == 0u) {
            out.append(output, sizeof output - out_left);
            return true;
        }
        iconv(converter_, nullptr, nullptr, nullptr, nullptr);
        return false;
    }

private:
    static inline const iconv_t kNone = reinterpret_cast<iconv_t>(-1);
    iconv_t converter_;
#elif defined(_WIN32)
    [[nodiscard]] bool available() const { return IsValidCodePage(932) != 0; }
    bool decode(const char *bytes, std::size_t length, std::string &out) {
        wchar_t wide[2];
        const int units = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(length), wide, 2);
        if (units <= 0) return false;
        char output[8];
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide, units, output, sizeof output, nullptr, nullptr);
        if (size <= 0) return false;
        out.append(output, static_cast<std::size_t>(size));
        return true;
    }
#else
    [[nodiscard]] bool available() const { return false; }
    bool decode(const char *, std::size_t, std::string &) { return false; }
#endif
};

} // namespace

std::string shift_jis_to_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 3u / 2u);
    Decoder decoder;
    const bool converting = decoder.available();
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
        if (converting && decoder.decode(text.data() + at, length, out)) {
            at += length;
        } else {
            escape(out, byte);
            ++at;
        }
    }
    return out;
}

} // namespace sarusaru::text
