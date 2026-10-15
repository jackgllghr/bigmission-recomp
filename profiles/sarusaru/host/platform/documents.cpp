#include "platform/documents.hpp"

#include <string_view>

#if defined(__ANDROID__)
#include <SDL3/SDL_iostream.h>
#include <unistd.h>
#endif

namespace sarusaru::platform {
namespace {

constexpr std::string_view kContentScheme = "content://";

std::string utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decoded(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex_digit(text[i + 1]) >= 0 && hex_digit(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_digit(text[i + 1]) * 16 + hex_digit(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

#if defined(__ANDROID__)
bool is_document(const std::filesystem::path &path) { return utf8(path).starts_with(kContentScheme); }

int open_document(const std::filesystem::path &path) {
    // SDL opens content:// URIs through the activity's content resolver.
    SDL_IOStream *stream = SDL_IOFromFile(utf8(path).c_str(), "rb");
    if (stream == nullptr) return -1;
    const auto descriptor = SDL_GetNumberProperty(SDL_GetIOProperties(stream),
        SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1);
    const int copy = descriptor >= 0 ? dup(static_cast<int>(descriptor)) : -1;
    SDL_CloseIO(stream);
    return copy;
}
#endif

std::string display_name(const std::filesystem::path &path) {
    const std::string text = utf8(path);
    if (!text.starts_with(kContentScheme)) return utf8(path.filename());
    // content://<authority>/document/primary%3ADownload%2FGame.iso
    std::string name = percent_decoded(text.substr(text.find_last_of('/') + 1u));
    if (const auto folder = name.find_last_of("/:"); folder != std::string::npos) name.erase(0, folder + 1u);
    return name;
}

} // namespace sarusaru::platform
