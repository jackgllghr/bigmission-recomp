#include "ui/text.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstring>

namespace sarusaru::ui {
namespace {

struct Entry {
    std::string_view english, japanese;
};
constexpr std::array<Entry, static_cast<std::size_t>(Text::Count)> kTexts = {{
#define X(id, english, japanese) {english, japanese},
#include "ui/texts.inc"
#undef X
}};

shell::Language g_language = shell::Language::English;

shell::Language system_language() {
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    const bool japanese = locales != nullptr && count > 0 && locales[0]->language != nullptr &&
                          std::strcmp(locales[0]->language, "ja") == 0;
    SDL_free(locales);
    return japanese ? shell::Language::Japanese : shell::Language::English;
}

} // namespace

shell::Language resolve_language(shell::Language setting) {
    if (setting != shell::Language::Auto) return setting;
    // The system's language does not change while the launcher runs.
    static const shell::Language system = system_language();
    return system;
}

void set_language(shell::Language language) {
    g_language = language == shell::Language::Japanese ? language : shell::Language::English;
}

shell::Language language() { return g_language; }

std::string_view tr(Text text, shell::Language language) {
    const Entry &entry = kTexts[static_cast<std::size_t>(text)];
    return language == shell::Language::Japanese ? entry.japanese : entry.english;
}

std::string_view tr(Text text) { return tr(text, g_language); }

std::string format(Text text, std::initializer_list<std::string_view> arguments) {
    const std::string_view pattern = tr(text);
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '0' &&
            pattern[i + 1] <= '9') {
            const auto index = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (index < arguments.size()) out += *(arguments.begin() + index);
            i += 2;
            continue;
        }
        out += pattern[i];
    }
    return out;
}

std::string_view language_name(shell::Language language) {
    return language == shell::Language::Japanese ? "日本語" : "English";
}

} // namespace sarusaru::ui
