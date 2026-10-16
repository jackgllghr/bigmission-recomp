#include "shell/settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif

namespace sarusaru::shell {
namespace {
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
int integer(std::string_view text, int minimum, int maximum) {
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum || value > maximum)
        throw std::runtime_error("expected an integer from " + std::to_string(minimum) + " to " + std::to_string(maximum));
    return value;
}
float number(std::string_view text, float minimum, float maximum) {
    // Classic locale, full consumption, and explicit finite checks on all platforms.
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    float value{};
    if (!(in >> value) || in.peek() != std::char_traits<char>::eof() || !std::isfinite(value) ||
        value < minimum || value > maximum) throw std::runtime_error("number out of range");
    return value;
}
std::string decimal(float value) {
    // Shortest round-tripping form; floating-point std::to_chars needs macOS 13.3+.
    if (!std::isfinite(value)) throw std::runtime_error("Cannot serialize settings number");
    std::string text;
    for (int precision = 0; precision <= 50; ++precision) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed;
        out.precision(precision);
        out << value;
        text = out.str();
        std::istringstream in{text};
        in.imbue(std::locale::classic());
        float parsed{};
        if (in >> parsed && parsed == value) break;
    }
    return text;
}
std::size_t action_index(std::string_view key) {
    for (std::size_t i = 0; i < kActionCount; ++i) if (kActionNames[i] == key) return i;
    throw std::runtime_error("unknown control action: " + std::string(key));
}
std::vector<Binding> bindings(std::string_view value, bool keyboard) {
    std::vector<Binding> result;
    if (value == "none") return result;
    while (!value.empty()) {
        const auto comma = value.find(',');
        const auto token = trim(value.substr(0, comma));
        Binding binding{Binding::Kind::Key, 0};
        if (keyboard) {
            binding.code = token.starts_with("scancode:") ? integer(token.substr(9), 1, SDL_SCANCODE_COUNT - 1)
                : static_cast<int>(SDL_GetScancodeFromName(std::string(token).c_str()));
            if (binding.code == SDL_SCANCODE_UNKNOWN) throw std::runtime_error("unknown keyboard scancode: " + std::string(token));
        } else if (token.starts_with("axis:")) {
            if (token.size() < 7 || (token.back() != '+' && token.back() != '-'))
                throw std::runtime_error("axis bindings need a + or - direction");
            binding.kind = Binding::Kind::Axis;
            binding.direction = token.back() == '+' ? 1 : -1;
            binding.code = static_cast<int>(SDL_GetGamepadAxisFromString(std::string(token.substr(5, token.size() - 6)).c_str()));
            if (binding.code == SDL_GAMEPAD_AXIS_INVALID) throw std::runtime_error("unknown gamepad axis: " + std::string(token));
        } else {
            binding.kind = Binding::Kind::Button;
            const auto button = [](std::string_view name) {
                const auto code = static_cast<int>(SDL_GetGamepadButtonFromString(std::string(name).c_str()));
                if (code == SDL_GAMEPAD_BUTTON_INVALID) throw std::runtime_error("unknown gamepad button: " + std::string(name));
                return code;
            };
            // Two buttons held together: leftstick+rightstick.
            if (const auto plus = token.find('+'); plus != token.npos) {
                const int first = button(trim(token.substr(0, plus))), second = button(trim(token.substr(plus + 1)));
                if (first == second) throw std::runtime_error("a button chord needs two different buttons");
                binding.code = std::min(first, second);
                binding.chord = std::max(first, second);
            } else {
                binding.code = button(token);
            }
        }
        result.push_back(binding);
        if (comma == value.npos) break;
        value = trim(value.substr(comma + 1));
        if (value.empty()) throw std::runtime_error("empty binding after comma");
    }
    if (result.empty()) throw std::runtime_error("use none to unbind a control");
    return result;
}
std::string binding_names(const std::vector<Binding> &bindings) {
    if (bindings.empty()) return "none";
    std::string out;
    for (const auto &binding : bindings) {
        if (!out.empty()) out += ", ";
        if (binding.kind == Binding::Kind::Key) {
            // Numeric physical scancodes also cover keys whose SDL names contain delimiters.
            const std::string name = SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.code));
            out += !name.empty() && name.find_first_of(",#") == name.npos
                ? name : "scancode:" + std::to_string(binding.code);
        } else if (binding.kind == Binding::Kind::Button) {
            out += SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(binding.code));
            if (binding.chord >= 0) {
                out += '+';
                out += SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(binding.chord));
            }
        } else {
            out += "axis:";
            out += SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(binding.code));
            out += binding.direction == 1 ? '+' : '-';
        }
    }
    return out;
}
// Paths are written as UTF-8 with '%', '#' (the comment mark), control
// characters and spaces at either end as %XX, so any path survives the file.
std::string encode_path(const std::filesystem::path &path) {
    const auto utf8 = path.u8string();
    const std::string text(utf8.begin(), utf8.end());
    std::string out;
    const auto first = text.find_first_not_of(' '), last = text.find_last_not_of(' ');
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool edge_space = c == ' ' && (first == text.npos || i < first || i > last);
        if (c == '%' || c == '#' || c < 0x20u || c == 0x7Fu || edge_space) {
            char escaped[4];
            std::snprintf(escaped, sizeof escaped, "%%%02X", c);
            out += escaped;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}
std::filesystem::path decode_path(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out += text[i];
            continue;
        }
        if (i + 2 >= text.size()) throw std::runtime_error("incomplete %XX escape in a path");
        int value{};
        const auto [end, error] = std::from_chars(text.data() + i + 1, text.data() + i + 3, value, 16);
        if (error != std::errc{} || end != text.data() + i + 3) throw std::runtime_error("invalid %XX escape in a path");
        out += static_cast<char>(value);
        i += 2;
    }
    return std::filesystem::path(std::u8string(out.begin(), out.end()));
}
} // namespace

Settings default_settings() {
    Settings settings;
    const char *keys[] = {"Up", "Down", "Left", "Right", "Z", "X", "A", "S", "Q", "W", "Return", "Backspace",
                          "J", "L", "I", "K", "Tab", "Escape", "Keypad 4", "Keypad 6", "Keypad 8", "Keypad 2", "Keypad 5", "F5", "F8"};
    const char *pads[] = {"dpup", "dpdown", "dpleft", "dpright", "b", "a", "x", "y",
                         "leftshoulder, axis:lefttrigger+", "rightshoulder, axis:righttrigger+", "start", "back",
                         "axis:leftx-", "axis:leftx+", "axis:lefty-", "axis:lefty+", "none", "guide, leftstick+rightstick",
                         "axis:rightx-", "axis:rightx+", "axis:righty-", "axis:righty+", "rightstick", "back+rightshoulder", "back+leftshoulder"};
    for (std::size_t i = 0; i < kActionCount; ++i) {
        settings.controls.keyboard[i] = bindings(keys[i], true);
        settings.controls.gamepad[i] = bindings(pads[i], false);
    }
    return settings;
}

Settings parse_settings(std::string_view text) {
    Settings settings = default_settings();
    std::istringstream in{std::string(text)};
    std::string section, line;
    std::set<std::string> seen;
    std::size_t line_number = 0;
    std::optional<bool> legacy_interpolation;
    std::array<std::optional<std::vector<Binding>>, 2> legacy_quit;  // keyboard, gamepad
    while (std::getline(in, line)) {
        ++line_number;
        auto content = trim(std::string_view(line).substr(0, line.find('#')));
        if (content.empty()) continue;
        try {
            if (content.front() == '[' && content.back() == ']') {
                section = trim(content.substr(1, content.size() - 2));
                if (section != "graphics" && section != "input" && section != "keyboard" && section != "gamepad" &&
                    section != "translation" && section != "game" && section != "interface")
                    throw std::runtime_error("unknown section: " + section);
                continue;
            }
            const auto equals = content.find('=');
            if (equals == content.npos) throw std::runtime_error("expected key = value");
            const auto key = trim(content.substr(0, equals));
            const auto value = trim(content.substr(equals + 1));
            const std::string full = section + "." + std::string(key);
            if (!seen.insert(full).second) throw std::runtime_error("duplicate setting: " + full);
            if (section == "keyboard" || section == "gamepad") {
                auto &controls = section == "keyboard" ? settings.controls.keyboard : settings.controls.gamepad;
                if (key == "quit") legacy_quit[section == "keyboard" ? 0 : 1] = bindings(value, section == "keyboard");
                else controls[action_index(key)] = bindings(value, section == "keyboard");
            } else if (full == "graphics.internal_resolution") {
                settings.internal_resolution = static_cast<std::uint32_t>(integer(value, 1, 8));
            } else if (full == "graphics.window_width") {
                settings.window_width = static_cast<std::uint32_t>(integer(value, 320, 16384));
            } else if (full == "graphics.window_height") {
                settings.window_height = static_cast<std::uint32_t>(integer(value, 180, 16384));
            } else if (full == "graphics.fullscreen") {
                settings.fullscreen = integer(value, 0, 1) != 0;
            } else if (full == "graphics.frame_rate") {
                settings.frame_rate = static_cast<std::uint32_t>(integer(value, 0, 60));
            } else if (full == "graphics.experimental_game_frame_rate") {
                // No longer a setting: the game always updates at its native rate.
            } else if (full == "graphics.cel_shading") {
                settings.cel_shading = static_cast<std::uint32_t>(integer(value, 0, 2));
            } else if (full == "graphics.frame_interpolation") {
                // Now frame_rate = 60; applied once frame_rate is read too.
                legacy_interpolation = integer(value, 0, 1) != 0;
            } else if (full == "graphics.fov_degrees") {
                settings.fov_degrees = number(value, 0.0f, 120.0f);
                if (settings.fov_degrees != 0.0f && settings.fov_degrees < 30.0f)
                    throw std::runtime_error("FOV must be 0 (native) or 30-120 degrees");
            } else if (full == "graphics.texture_pack") {
                if (value.empty()) throw std::runtime_error("texture_pack needs a directory or none");
                settings.texture_pack = value == "none" ? std::filesystem::path{} : decode_path(value);
            } else if (full == "translation.language") {
                if (value != "ja" && value != "en") throw std::runtime_error("language must be ja or en");
                settings.translation_language = value;
            } else if (full == "translation.pack") {
                if (value.empty()) throw std::runtime_error("pack needs a path or none");
                settings.translation_pack = value == "none" ? std::filesystem::path{} : std::filesystem::path(value);
            } else if (full == "translation.subtitles") {
                settings.subtitles = integer(value, 0, 1) != 0;
            } else if (full == "input.dead_zone") {
                settings.controls.dead_zone = number(value, 0.0f, 0.95f);
            } else if (full == "game.skip_intro_logos") {
                settings.skip_intro_logos = integer(value, 0, 1) != 0;
            } else if (full == "game.state_slot") {
                settings.state_slot = static_cast<std::uint32_t>(integer(value, 1, 5));
            } else if (full == "game.disc") {
                settings.game_disc = decode_path(value);
            } else if (full == "interface.check_for_updates") {
                settings.check_for_updates = integer(value, 0, 1) != 0;
            } else if (full == "interface.last_update_check") {
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), settings.last_update_check);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || settings.last_update_check < 0)
                    throw std::runtime_error("last_update_check must be a nonnegative timestamp");
            } else if (full == "interface.confirm") {
                if (value != "east" && value != "south") throw std::runtime_error("confirm must be east or south");
                settings.confirm_south = value == "south";
            } else if (full == "interface.controller_buttons") {
                const auto found = std::find(kPromptStyleNames.begin(), kPromptStyleNames.end(), value);
                if (found == kPromptStyleNames.end()) throw std::runtime_error("controller_buttons must be auto, xbox, switch, playstation or keyboard");
                settings.prompt_style = static_cast<PromptStyle>(found - kPromptStyleNames.begin());
            } else if (full == "interface.language") {
                if (value == "auto") settings.language = Language::Auto;
                else if (value == "en") settings.language = Language::English;
                else if (value == "ja") settings.language = Language::Japanese;
                else throw std::runtime_error("language must be auto, en or ja");
            } else throw std::runtime_error("unknown setting: " + full);
        } catch (const std::exception &error) {
            throw std::runtime_error("settings line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    for (std::size_t i = 0; i < 2u; ++i) {
        const std::string menu = i == 0u ? "keyboard.menu" : "gamepad.menu";
        if (legacy_quit[i] && !legacy_quit[i]->empty() && !seen.contains(menu))
            (i == 0u ? settings.controls.keyboard : settings.controls.gamepad)[static_cast<std::size_t>(Action::Menu)] =
                *legacy_quit[i];
    }
    // An earlier version's interpolation; its cap of 60 alone showed the game's own frames.
    if (legacy_interpolation && *legacy_interpolation) settings.frame_rate = 60u;
    else if (legacy_interpolation && settings.frame_rate == 60u) settings.frame_rate = 0u;
    return settings;
}

std::string serialize_settings(const Settings &settings) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# SaruSaru settings. Restart the game after editing.\n"
           "# Keyboard values: SDL physical key names or scancode:N; comma separates alternatives.\n"
           "# Gamepad values: SDL button names, two held together as a+b, or axis:NAME+ / axis:NAME-; none unbinds.\n"
           "[graphics]\n# Rendering scale: 1-8 times the PSP's 480x272.\ninternal_resolution = " << settings.internal_resolution << '\n'
        << "# Cel shading: 0 = off, 1 = banded, 2 = banded with black world and character outlines.\n"
        << "cel_shading = " << settings.cel_shading << '\n'
        << "# Window size at startup, in points; the window stays resizable and the picture keeps its aspect.\n"
        << "window_width = " << settings.window_width << "\nwindow_height = " << settings.window_height << '\n'
        << "# 1 = fullscreen at the desktop resolution (Alt+Enter or F11 toggles while playing).\n"
        << "fullscreen = " << (settings.fullscreen ? 1 : 0) << '\n'
        << "# 0 = native; 30 = capped at 30; 60 = 60 FPS motion at native game speed, an interpolated\n"
        << "# picture between game frames (adds one vblank of latency).\nframe_rate = " << settings.frame_rate << '\n'
        << "# Gameplay camera vertical FOV: 0 = native (about 36.5); 30-120. Menus, cutscenes and HUD stay native.\n"
        << "fov_degrees = " << decimal(settings.fov_degrees) << '\n'
        << "# HD texture pack directory, relative to this settings file, or none for the original textures.\n"
        << "texture_pack = " << (settings.texture_pack.empty() ? "none" : encode_path(settings.texture_pack)) << '\n'
        << "\n[translation]\n# ja = original; en = English menu pack. Restart to change languages.\n"
        << "language = " << settings.translation_language << '\n'
        << "# Pack path relative to this settings file, or none.\npack = "
        << (settings.translation_pack.empty() ? "none" : settings.translation_pack.generic_string()) << '\n'
        << "# 1 = subtitles over the movies that have them.\nsubtitles = " << (settings.subtitles ? 1 : 0) << '\n'
        << "\n[input]\ndead_zone = " << decimal(settings.controls.dead_zone) << '\n'
        << "\n[game]\n# The disc image (.iso) the launcher plays; empty until one is chosen. % and # are written as %XX.\n"
        << "disc = " << encode_path(settings.game_disc) << '\n'
        << "# 1 = skip the opening logos. Start also skips them with this off.\nskip_intro_logos = "
        << (settings.skip_intro_logos ? 1 : 0) << '\n'
        << "# Selected state slot for shortcuts (1-5).\nstate_slot = " << settings.state_slot << '\n'
        << "\n[interface]\n# The gamepad button that confirms in the launcher: south (cross) or east (circle, as in the game).\n"
        << "confirm = " << (settings.confirm_south ? "south" : "east") << '\n'
        << "check_for_updates = " << (settings.check_for_updates ? 1 : 0) << '\n'
        << "last_update_check = " << settings.last_update_check << '\n'
        << "controller_buttons = " << kPromptStyleNames[static_cast<std::size_t>(settings.prompt_style)] << '\n'
        << "# The launcher's language: auto (the system's), en or ja. The game's own text stays Japanese.\n"
        << "language = "
        << (settings.language == Language::English ? "en" : settings.language == Language::Japanese ? "ja" : "auto")
        << '\n';
    for (const bool keyboard : {true, false}) {
        out << (keyboard ? "\n[keyboard]\n" : "\n[gamepad]\n");
        const auto &controls = keyboard ? settings.controls.keyboard : settings.controls.gamepad;
        for (std::size_t i = 0; i < kActionCount; ++i)
            out << kActionNames[i] << " = " << binding_names(controls[i]) << '\n';
    }
    return out.str();
}

std::filesystem::path default_settings_path() {
    if (const char *path = std::getenv("SARUSARU_SETTINGS"); path && *path) return path;
#if defined(_WIN32)
    if (const char *base = std::getenv("APPDATA"); base && *base)
        return std::filesystem::path(base) / "SaruSaruRecomp" / "settings.ini";
#elif defined(__ANDROID__)
    if (const char *base = SDL_GetAndroidInternalStoragePath(); base && *base)
        return std::filesystem::path(base) / "settings.ini";
#elif defined(__APPLE__)
    if (const char *base = std::getenv("HOME"); base && *base)
        return std::filesystem::path(base) / "Library" / "Application Support" / "SaruSaruRecomp" / "settings.ini";
#else
    if (const char *base = std::getenv("XDG_CONFIG_HOME"); base && *base)
        return std::filesystem::path(base) / "SaruSaruRecomp" / "settings.ini";
    if (const char *base = std::getenv("HOME"); base && *base)
        return std::filesystem::path(base) / ".config" / "SaruSaruRecomp" / "settings.ini";
#endif
    throw std::runtime_error("Cannot determine the settings directory; use --settings FILE");
}

Settings load_settings(const std::filesystem::path &path) {
    if (std::filesystem::exists(path)) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot read settings: " + path.string());
        std::ostringstream text;
        text << in.rdbuf();
        if (in.bad()) throw std::runtime_error("Cannot read settings: " + path.string());
        try { return parse_settings(text.str()); }
        catch (const std::runtime_error &error) {
            throw std::runtime_error(path.string() + ": " + error.what());
        }
    }
    const Settings settings = default_settings();
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    // Exclusive creation protects an existing file if another launcher creates it concurrently.
#if defined(_WIN32)
    FILE *file = _wfopen(path.c_str(), L"wx");
#else
    FILE *file = std::fopen(path.c_str(), "wx");
#endif
    if (!file) throw std::runtime_error("Cannot create settings: " + path.string());
    const std::string text = serialize_settings(settings);
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) throw std::runtime_error("Cannot write settings: " + path.string());
    return settings;
}
void save_settings(const std::filesystem::path &path, const Settings &settings) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".new";
    const std::string text = serialize_settings(settings);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("Cannot write settings: " + temporary.string());
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("Cannot replace settings: " + path.string());
    }
}
} // namespace sarusaru::shell
