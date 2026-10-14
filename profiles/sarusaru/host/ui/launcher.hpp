#pragma once

// The launcher: what the game shows before it starts. The main menu (Play,
// Settings, Game disc, Quit), choosing the player's disc image (with the
// launcher's own browser, the system's file picker, or by dropping it on the
// window) and the settings. Every screen works with a gamepad, a keyboard, a
// mouse or a touch screen alone.
#include "game_disc.hpp"
#include "shell/settings.hpp"
#include "ui/file_list.hpp"
#include "ui/layer.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sarusaru::ui {

class UiScript;

class Launcher {
public:
    enum class Outcome { Play, Quit };

    // `settings` is edited in place and saved to `settings_path` as it changes.
    Launcher(Layer &layer, shell::Settings &settings, std::filesystem::path settings_path, DiscIdentity identity);
    ~Launcher();
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    // Shows over the main menu, when the launcher next runs, that the game
    // could not start, with `detail` (the runtime's own reason, in English).
    void show_start_failure(std::string detail);

    // Runs the screens until the player chooses to play (with a supported disc
    // in settings.game_disc) or to quit. `script` drives them, when given.
    Outcome run(UiScript *script = nullptr);

private:
    enum class Screen { Home, Disc, Browser, Settings };

    Layer &layer_;
    shell::Settings &settings_;
    std::filesystem::path settings_path_;
    DiscIdentity identity_;
    Screen screen_{Screen::Home};
    std::optional<Outcome> outcome_;

    // The disc in the settings, as last checked.
    std::optional<DiscCheck> disc_check_;
    // What happened to the last disc the player chose.
    std::string disc_message_;
    bool disc_message_good_{};

    // Why the game could not start, shown over everything until dismissed.
    std::string start_failure_;

    // The file browser.
    std::filesystem::path folder_;
    std::vector<FileEntry> entries_;
    std::vector<Place> places_;
    std::string browser_message_;
    bool all_files_{};

    // The settings screen.
    int tab_{};
    std::string settings_error_;

    // The system file dialog answers on another thread.
    std::mutex dialog_mutex_;
    std::optional<std::filesystem::path> dialog_choice_;
    std::atomic<bool> dialog_open_{};

    ImTextureID title_art_{};
    ImVec2 title_size_{};

    void frame();
    void home();
    void disc();
    void browser();
    void settings();
    void problem();

    void go(Screen screen, std::string_view focus);
    void choose_disc(const std::filesystem::path &path);
    void open_folder(const std::filesystem::path &folder);
    void open_system_dialog();
    void save();
    void load_title_art();
};

} // namespace sarusaru::ui
