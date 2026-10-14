#include "ui/launcher.hpp"

#include "game_module.hpp"

#include "ui/text.hpp"
#include "ui/theme.hpp"
#include "ui/ui_script.hpp"
#include "ui/widgets.hpp"

#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <tuple>
#include <utility>

namespace sarusaru::ui {
namespace {

// Interface frames are paced to about 60 a second; the swapchain may not wait for vsync.
constexpr std::uint64_t kFrameNs = 16'666'667u;


ImVec2 display() { return ImGui::GetIO().DisplaySize; }

// What is wrong with a disc image, in the player's language.
std::string disc_problem(const DiscCheck &check, std::string_view supported) {
    switch (check.result) {
    case DiscCheck::Result::Supported: return {};
    case DiscCheck::Result::Missing: return std::string(tr(Text::DiscMissing));
    case DiscCheck::Result::Unreadable: return std::string(tr(Text::DiscUnreadable));
    case DiscCheck::Result::NotAnIso: return std::string(tr(Text::DiscNotIso));
    case DiscCheck::Result::NotPspGame: return std::string(tr(Text::DiscNotPsp));
    case DiscCheck::Result::OtherGame: return format(Text::DiscOtherGame, {check.disc_id, supported});
    case DiscCheck::Result::OtherExecutable: return std::string(tr(Text::DiscOtherExecutable));
    }
    return {};
}

std::string place_name(const Place &place) {
    switch (place.kind) {
    case PlaceKind::Home: return std::string(tr(Text::PlaceHome));
    case PlaceKind::Desktop: return std::string(tr(Text::PlaceDesktop));
    case PlaceKind::Downloads: return std::string(tr(Text::PlaceDownloads));
    case PlaceKind::Documents: return std::string(tr(Text::PlaceDocuments));
    case PlaceKind::Storage: return std::string(tr(Text::PlaceStorage));
    case PlaceKind::Computer: return std::string(tr(Text::PlaceComputer));
    case PlaceKind::Drive: break;
    }
    return place.name;
}

// The panel most screens put their content in: centred, below the title,
// above the hints. Returns its inner rectangle.
ImRect content_panel(const Layer &layer, float top, float max_width) {
    const float u = layer.unit();
    const ImVec2 size = display();
    const float width = std::min(size.x - 48.0f * u, max_width * u);
    const ImRect inner(ImVec2((size.x - width) * 0.5f, top + 8.0f * u), ImVec2((size.x + width) * 0.5f, size.y - 96.0f * u));
    panel(layer, inner.Min, inner.Max);
    return inner;
}

void status_dot(ImDrawList *draw, ImVec2 center, float radius, ImU32 color) {
    draw->AddCircleFilled(center, radius + radius * 0.35f, colors::kBlue);
    draw->AddCircleFilled(center, radius, color);
    draw->AddCircleFilled(ImVec2(center.x - radius * 0.3f, center.y - radius * 0.3f), radius * 0.35f, IM_COL32(255, 255, 255, 170));
}

} // namespace

Launcher::Launcher(Layer &layer, shell::Settings &settings, std::filesystem::path settings_path, DiscIdentity identity)
    : layer_(layer), settings_(settings), settings_path_(std::move(settings_path)), identity_(identity) {
    if (!settings_.game_disc.empty()) disc_check_ = check_disc(settings_.game_disc, identity_);
    load_title_art();
}

Launcher::~Launcher() = default;

void Launcher::show_start_failure(std::string detail) {
    start_failure_ = std::move(detail);
}

void Launcher::load_title_art() {
    const auto path = resource_path("title.png");
    const auto utf8 = path.u8string();
    SDL_Surface *loaded = SDL_LoadPNG(std::string(utf8.begin(), utf8.end()).c_str());
    if (loaded == nullptr) {
        std::cerr << "sarusaru: cannot load the title art: " << SDL_GetError() << '\n';
        return;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) return;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4u);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->w) * 4u,
                    static_cast<const std::uint8_t *>(rgba->pixels) + static_cast<std::ptrdiff_t>(y) * rgba->pitch,
                    static_cast<std::size_t>(rgba->w) * 4u);
    title_art_ = layer_.renderer().add_texture(pixels.data(), static_cast<std::uint32_t>(rgba->w),
                                               static_cast<std::uint32_t>(rgba->h));
    title_size_ = ImVec2(static_cast<float>(rgba->w), static_cast<float>(rgba->h));
    SDL_DestroySurface(rgba);
}

Launcher::Outcome Launcher::run(UiScript *script) {
    outcome_.reset();
    screen_ = Screen::Home;
    go(Screen::Home, start_failure_.empty() ? "play" : "problem_ok");
    layer_.set_frame_hook(script != nullptr ? std::function<void(Layer &)>([script](Layer &l) { script->tick(l); })
                                            : std::function<void(Layer &)>());
    layer_.set_event_filter([this](const SDL_Event &event) { return capture_event(event); });
    while (!outcome_) {
        const std::uint64_t start = SDL_GetTicksNS();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Alt+Enter or F11 toggles fullscreen here too.
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                (event.key.scancode == SDL_SCANCODE_F11 ||
                 (event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT) != 0))) {
                settings_.fullscreen = !settings_.fullscreen;
                SDL_SetWindowFullscreen(layer_.window(), settings_.fullscreen);
                save();
                continue;
            }
            layer_.handle(event);
        }
        if (layer_.closed()) {
            outcome_ = Outcome::Quit;
            break;
        }
        layer_.begin_frame(settings_.confirm_south);
        frame();
        layer_.present();
        const std::uint64_t spent = SDL_GetTicksNS() - start;
        if (spent < kFrameNs) SDL_DelayPrecise(kFrameNs - spent);
    }
    layer_.set_frame_hook({});
    layer_.set_event_filter({});
    layer_.block_pads(false);
    return *outcome_;
}

void Launcher::go(Screen screen, std::string_view focus) {
    screen_ = screen;
    focus_control(focus);
}

void Launcher::play() {
    if (find_game_module()) {
        outcome_ = Outcome::Play;
        return;
    }
    install_error_.clear();
    toolchain_.reset();
    go(Screen::Install, "install_start");
}

void Launcher::save() {
    try {
        shell::save_settings(settings_path_, settings_);
        settings_error_.clear();
    } catch (const std::exception &error) {
        settings_error_ = format(Text::SaveFailed, {error.what()});
        std::cerr << "sarusaru: " << settings_error_ << '\n';
    }
}

void Launcher::frame() {
    set_language(resolve_language(settings_.language));
    draw_backdrop(layer_);
    if (const auto dropped = layer_.take_dropped_file()) choose_disc(*dropped);
    {
        std::optional<std::filesystem::path> chosen;
        {
            std::lock_guard lock(dialog_mutex_);
            chosen = std::exchange(dialog_choice_, std::nullopt);
        }
        if (chosen) choose_disc(*chosen);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display());
    if (GImGui->NavWindow == nullptr) ImGui::SetNextWindowFocus();
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (!start_failure_.empty()) {
        problem();
    } else {
        switch (screen_) {
        case Screen::Home: home(); break;
        case Screen::Disc: disc(); break;
        case Screen::Browser: browser(); break;
        case Screen::Install: install(); break;
        case Screen::Settings: settings(); break;
        case Screen::Bindings: bindings(); break;
        case Screen::Pack: pack(); break;
        }
    }
    ImGui::End();
}

// --- Main menu -------------------------------------------------------------

void Launcher::home() {
    const float u = layer_.unit(), t = layer_.time();
    const ImVec2 size = display();
    ImDrawList *draw = ImGui::GetWindowDrawList();

    // From the top: the title art, whether the game can start, and the
    // buttons, kept clear of the swoosh and hints along the bottom. The art
    // takes the height that is left.
    const bool ready = disc_check_ && disc_check_->supported();
    const ImVec2 button(300.0f * u, 48.0f * u);
    const float gap = 12.0f * u, status_height = 30.0f * u, bottom = 88.0f * u;
    const float buttons_height = 4.0f * button.y + 3.0f * gap;
    float art_height = 0.0f, art_width = 0.0f;
    if (title_art_ != ImTextureID{}) {
        const float aspect = title_size_.x / title_size_.y;
        art_height = std::clamp(size.y - bottom - buttons_height - status_height - 40.0f * u, 0.0f, 250.0f * u);
        art_width = std::min(art_height * aspect, size.x * 0.8f);
        art_height = art_width / aspect;
    }
    const float stack = art_height + 8.0f * u + status_height + 14.0f * u + buttons_height;
    float y = std::max(10.0f * u, (size.y - bottom - stack) * 0.5f);
    if (art_height > 0.0f) {
        const float bob = std::sin(t * 1.6f) * 3.0f * u;
        const ImVec2 min((size.x - art_width) * 0.5f, y + bob);
        draw->AddImage(title_art_, min, ImVec2(min.x + art_width, min.y + art_height));
        y += art_height + 8.0f * u;
    }

    {
        ImFont *font = fonts().strong;
        const float text_px = 16.0f * u;
        const std::string_view status = tr(ready ? Text::StatusReady : settings_.game_disc.empty()
            ? Text::StatusChoose : Text::StatusAttention);
        const ImVec2 extent = text_size(font, text_px, status);
        const ImVec2 min((size.x - extent.x) * 0.5f - 20.0f * u, y), max((size.x + extent.x) * 0.5f + 14.0f * u, y + status_height);
        draw->AddRectFilled(ImVec2(min.x - 3.0f * u, min.y - 3.0f * u), ImVec2(max.x + 3.0f * u, max.y + 3.0f * u),
                            colors::kBlue, 18.0f * u);
        draw->AddRectFilled(min, max, colors::kWhite, 15.0f * u);
        status_dot(draw, ImVec2(min.x + 15.0f * u, (min.y + max.y) * 0.5f), 5.0f * u, ready ? colors::kGood : colors::kRed);
        draw->AddText(font, text_px, ImVec2(min.x + 28.0f * u, (min.y + max.y - extent.y) * 0.5f), colors::kInk,
                      status.data(), status.data() + status.size());
        y += status_height + 14.0f * u;
    }

    const float x = (size.x - button.x) * 0.5f;
    const auto place = [&](int index) { ImGui::SetCursorScreenPos(ImVec2(x, y + static_cast<float>(index) * (button.y + gap))); };
    place(0);
    if (menu_button(layer_, "play", tr(Text::Play), button, true)) {
        if (ready) {
            play();
        } else {
            disc_message_ = settings_.game_disc.empty() ? std::string(tr(Text::ChooseDiscFirst)) : "";
            disc_message_good_ = false;
            go(Screen::Disc, "browse");
        }
    }
    place(1);
    if (menu_button(layer_, "settings", tr(Text::Settings), button)) go(Screen::Settings, "row0");
    place(2);
    if (menu_button(layer_, "disc", tr(Text::GameDisc), button)) go(Screen::Disc, "browse");
    place(3);
    if (menu_button(layer_, "quit", tr(Text::Quit), button)) outcome_ = Outcome::Quit;

    // The disclaimer along the bottom left, on the swoosh.
    {
        ImFont *font = fonts().body;
        const float text_px = 12.0f * u;
        const std::string_view disclaimer = tr(Text::Disclaimer);
        const ImVec2 extent = text_size(font, text_px, disclaimer, size.x * 0.5f);
        draw->AddText(font, text_px, ImVec2(14.0f * u, size.y - extent.y - 10.0f * u), colors::kBlueDeep,
                      disclaimer.data(), disclaimer.data() + disclaimer.size(), size.x * 0.5f);
    }
    if (layer_.back()) outcome_ = Outcome::Quit;
    hints(layer_, {{Control::Confirm, tr(Text::HintSelect)}, {Control::Back, tr(Text::HintQuit)}});
}

// --- The game disc ---------------------------------------------------------

void Launcher::choose_disc(const std::filesystem::path &path) {
    const DiscCheck check = check_disc(path, identity_);
    const std::string name = utf8(path.filename());
    if (check.supported()) {
        settings_.game_disc = path;
        disc_check_ = check;
        save();
        disc_message_ = format(Text::FoundIt, {name});
        disc_message_good_ = true;
        go(Screen::Disc, "play_now");
    } else {
        disc_message_ = format(Text::DiscProblemFor, {name, disc_problem(check, identity_.disc_id)});
        disc_message_good_ = false;
        browser_message_ = disc_message_;
        if (screen_ == Screen::Home) go(Screen::Disc, "browse");
    }
}

void Launcher::open_system_dialog() {
    if (dialog_open_) return;
    dialog_open_ = true;
    // SDL keeps the filters until the dialog closes.
    static std::string filter_name;
    filter_name = tr(Text::PickerFilter);
    static const SDL_DialogFileFilter kFilters[] = {{filter_name.c_str(), "iso;ISO"}};
    const std::string start = settings_.game_disc.empty() ? std::string() : utf8(settings_.game_disc.parent_path());
    SDL_ShowOpenFileDialog(
        [](void *user, const char *const *files, int) {
            auto *self = static_cast<Launcher *>(user);
            if (files != nullptr && files[0] != nullptr) {
                const std::string chosen = files[0];
                std::lock_guard lock(self->dialog_mutex_);
                self->dialog_choice_ = std::filesystem::path(std::u8string(chosen.begin(), chosen.end()));
            }
            self->dialog_open_ = false;
        },
        this, layer_.window(), kFilters, 1, start.empty() ? nullptr : start.c_str(), false);
}

void Launcher::disc() {
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::DiscTitle));
    const ImRect inner = content_panel(layer_, top, 700.0f);
    const float pad = 22.0f * u, width = inner.GetWidth() - 2.0f * pad - 12.0f * u;
    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, inner.Min.y + pad * 0.8f));
    begin_list("##disc", ImVec2(inner.GetWidth() - 2.0f * pad, inner.GetHeight() - 1.6f * pad));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    paragraph(layer_, format(Text::DiscIntro, {identity_.disc_id}), width, colors::kInkDim);
    ImGui::Dummy(ImVec2(0, 4.0f * u));

    // The disc in use and what was found.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float height = 74.0f * u;
        draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), IM_COL32(220, 232, 250, 255), 14.0f * u);
        const bool ready = disc_check_ && disc_check_->supported();
        const ImVec2 icon(at.x + 34.0f * u, at.y + height * 0.5f);
        const float r = 22.0f * u;
        draw->AddCircleFilled(icon, r + 3.0f * u, colors::kBlue);
        draw->AddCircleFilled(icon, r, ready ? colors::kYellow : colors::kHelmet);
        draw->AddCircleFilled(ImVec2(icon.x - r * 0.3f, icon.y - r * 0.3f), r * 0.35f, IM_COL32(255, 255, 255, 200));
        draw->AddCircleFilled(icon, r * 0.25f, colors::kBlue);
        ImFont *strong = fonts().strong, *body = fonts().body;
        const float text_x = at.x + 70.0f * u;
        const ImVec4 clip(text_x, at.y, at.x + width - 12.0f * u, at.y + height);
        const std::string name = settings_.game_disc.empty() ? std::string(tr(Text::NoDiscChosen))
                                                             : utf8(settings_.game_disc.filename());
        draw->AddText(strong, 19.0f * u, ImVec2(text_x, at.y + 10.0f * u), colors::kInk, name.c_str(), nullptr, 0.0f, &clip);
        std::string detail;
        ImU32 detail_color = colors::kInkDim;
        if (ready) {
            detail = format(Text::ReadyIn, {utf8(settings_.game_disc.parent_path())});
            detail_color = colors::kGood;
        } else if (disc_check_) {
            detail = disc_problem(*disc_check_, identity_.disc_id);
            detail_color = colors::kRed;
        } else {
            detail = tr(Text::ChooseBelow);
        }
        draw->AddText(body, 15.0f * u, ImVec2(text_x, at.y + 40.0f * u), detail_color, detail.c_str(), nullptr, 0.0f, &clip);
        ImGui::Dummy(ImVec2(width, height));
    }
    ImGui::Dummy(ImVec2(0, 2.0f * u));
    if (!disc_message_.empty()) {
        paragraph(layer_, disc_message_, width, disc_message_good_ ? colors::kGood : colors::kRed);
        ImGui::Dummy(ImVec2(0, 2.0f * u));
    }

    const bool ready = disc_check_ && disc_check_->supported();
    if (ready && action_row(layer_, "play_now", tr(Text::PlayNow), "", width)) play();
    if (action_row(layer_, "browse", tr(Text::FindDisc), tr(Text::WorksWithController), width)) {
        browser_message_.clear();
        std::filesystem::path start;
        if (!settings_.game_disc.empty()) start = settings_.game_disc.parent_path();
        if (start.empty() || !std::filesystem::is_directory(start)) {
            places_ = places();
            start = places_.empty() ? std::filesystem::current_path() : places_.front().path;
        }
        open_folder(start);
        go(Screen::Browser, "entry0");
    }
    if (action_row(layer_, "system_picker", tr(dialog_open_ ? Text::WaitingForPicker : Text::SystemPicker), "",
                   width, !dialog_open_))
        open_system_dialog();
    if (layer_.device() == Device::Mouse) {
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        paragraph(layer_, tr(Text::DropHint), width, colors::kInkDim);
    }
    end_list(layer_);

    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) go(Screen::Home, "disc");
    hints(layer_, {{Control::Confirm, tr(Text::HintSelect)}, {Control::Back, tr(Text::HintBack)}});
}

// --- The file browser ------------------------------------------------------

void Launcher::open_folder(const std::filesystem::path &folder) {
    try {
        auto entries = list_folder(folder, all_files_);
        folder_ = folder;
        entries_ = std::move(entries);
        places_ = places();
        focus_control("entry0");
    } catch (const std::exception &) {
        browser_message_ = tr(Text::FolderUnreadable);
    }
}

void Launcher::browser() {
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::BrowserTitle));
    const ImRect inner = content_panel(layer_, top, 900.0f);
    const float pad = 16.0f * u;
    ImDrawList *draw = ImGui::GetWindowDrawList();

    // Where we are, cut from the left when long.
    {
        ImFont *font = fonts().body;
        const std::string where = utf8(folder_);
        const float size = 15.0f * u, room = inner.GetWidth() - 2.0f * pad;
        std::string_view shown = where;
        std::string clipped;
        while (shown.size() > 4 && text_size(font, size, shown).x > room) {
            shown.remove_prefix(1);
            clipped = "…" + std::string(shown);
        }
        if (!clipped.empty() && text_size(font, size, shown).x <= room) shown = clipped;
        draw->AddText(font, size, ImVec2(inner.Min.x + pad + 4.0f * u, inner.Min.y + 12.0f * u), colors::kInkDim,
                      shown.data(), shown.data() + shown.size());
    }

    const float list_top = inner.Min.y + 40.0f * u;
    const float message_room = browser_message_.empty() ? 0.0f : 34.0f * u;
    const float list_height = inner.Max.y - list_top - pad - message_room;
    const float places_width = std::min(220.0f * u, inner.GetWidth() * 0.3f);

    // Places on the left; the filter toggle at their end.
    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, list_top));
    begin_list("##places", ImVec2(places_width, list_height));
    std::optional<std::filesystem::path> open;
    for (std::size_t i = 0; i < places_.size(); ++i)
        if (list_row(layer_, "place" + std::to_string(i), ListIcon::Place, place_name(places_[i]), "", places_width - 10.0f * u,
                     places_[i].path == folder_))
            open = places_[i].path;
    if (list_row(layer_, "filter", ListIcon::Disc, tr(all_files_ ? Text::ShowingAll : Text::ShowingIso), "",
                 places_width - 10.0f * u)) {
        all_files_ = !all_files_;
        open = folder_;
    }
    end_list(layer_);

    draw->AddLine(ImVec2(inner.Min.x + pad + places_width + 6.0f * u, list_top),
                  ImVec2(inner.Min.x + pad + places_width + 6.0f * u, list_top + list_height), IM_COL32(180, 196, 226, 255),
                  2.0f * u);

    // The folder's contents.
    const float entries_x = inner.Min.x + pad + places_width + 14.0f * u;
    const float entries_width = inner.Max.x - pad - entries_x;
    ImGui::SetCursorScreenPos(ImVec2(entries_x, list_top));
    begin_list("##entries", ImVec2(entries_width, list_height));
    std::size_t index = 0;
    const float row_width = entries_width - 14.0f * u;
    if (folder_.has_parent_path() && folder_.parent_path() != folder_) {
        if (list_row(layer_, "entry" + std::to_string(index++), ListIcon::Up, tr(Text::UpFolder), "", row_width))
            open = folder_.parent_path();
    }
    std::optional<std::filesystem::path> chosen;
    for (const auto &entry : entries_) {
        const bool iso = !entry.directory;
        if (list_row(layer_, "entry" + std::to_string(index++), entry.directory ? ListIcon::Folder : ListIcon::Disc,
                     entry.name, iso ? format_size(entry.size) : "", row_width)) {
            if (entry.directory) open = entry.path; else chosen = entry.path;
        }
    }
    if (entries_.empty()) {
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        paragraph(layer_, tr(all_files_ ? Text::FolderEmpty : Text::NothingHere), row_width, colors::kInkDim);
    }
    end_list(layer_);

    if (!browser_message_.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad + 4.0f * u, inner.Max.y - pad - message_room + 8.0f * u));
        paragraph(layer_, browser_message_, inner.GetWidth() - 2.0f * pad, colors::kRed);
    }
    if (open) {
        browser_message_.clear();
        open_folder(*open);
    }
    if (chosen) choose_disc(*chosen);

    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) go(Screen::Disc, "browse");
    hints(layer_, {{Control::Confirm, tr(Text::HintOpen)}, {Control::Back, tr(Text::HintBack)}});
}

// --- Installing the game -------------------------------------------------------

namespace {

std::string megabytes(std::uint64_t bytes) { return std::to_string((bytes + 500'000u) / 1'000'000u); }

// What the installation is doing, in the player's language.
std::string install_step(const install::Progress &progress) {
    using Step = install::Progress::Step;
    switch (progress.step) {
    case Step::Starting: return std::string(tr(Text::InstallStepStarting));
    case Step::Downloading:
        return progress.total == 0u ? std::string(tr(Text::InstallStepPreparing))
                                    : format(Text::InstallStepDownloading, {megabytes(progress.done), megabytes(progress.total)});
    case Step::Extracting: return std::string(tr(Text::InstallStepExtracting));
    case Step::Generating: return std::string(tr(Text::InstallStepGenerating));
    case Step::Compiling:
        return format(Text::InstallStepCompiling, {std::to_string(progress.done), std::to_string(progress.total)});
    case Step::Linking: return std::string(tr(Text::InstallStepLinking));
    case Step::Done:
    case Step::Failed:
    case Step::Cancelled: break;
    }
    return {};
}

// A bar of `fraction` in [0, 1], or a sweeping block while the end is unknown.
void progress_bar(const Layer &layer, float width, std::optional<float> fraction) {
    const float u = layer.unit(), height = 22.0f * u, radius = height * 0.5f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), colors::kBlue, radius);
    const float inset = 3.0f * u, room = width - 2.0f * inset;
    float from = 0.0f, to = 0.0f;
    if (fraction) {
        to = room * std::clamp(*fraction, 0.0f, 1.0f);
    } else {
        const float sweep = std::fmod(layer.time() * 0.6f, 1.0f);
        from = room * std::max(0.0f, sweep * 1.25f - 0.25f);
        to = room * std::min(1.0f, sweep * 1.25f);
    }
    if (to - from > 1.0f)
        draw->AddRectFilled(ImVec2(at.x + inset + from, at.y + inset), ImVec2(at.x + inset + to, at.y + height - inset),
                            colors::kYellow, radius - inset);
    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void Launcher::install() {
    using Status = install::ToolchainCheck::Status;
    using Step = install::Progress::Step;
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::InstallTitle));
    const ImRect inner = content_panel(layer_, top, 700.0f);
    const float pad = 22.0f * u, width = inner.GetWidth() - 2.0f * pad - 12.0f * u;

    // An installation that ended: on to the game, or back to the choices with why.
    if (builder_) {
        const auto progress = builder_->progress();
        if (progress.step == Step::Done) {
            builder_.reset();
            outcome_ = Outcome::Play;
            return;
        }
        if (progress.step == Step::Failed || progress.step == Step::Cancelled) {
            install_error_ = progress.step == Step::Failed ? format(Text::InstallFailed, {progress.error}) : "";
            builder_.reset();
            toolchain_.reset();
            focus_control("install_start");
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, inner.Min.y + pad * 0.8f));
    begin_list("##install", ImVec2(inner.GetWidth() - 2.0f * pad, inner.GetHeight() - 1.6f * pad));
    paragraph(layer_, tr(Text::InstallIntro), width, colors::kInkDim);
    ImGui::Dummy(ImVec2(0, 8.0f * u));
    if (builder_) {
        const auto progress = builder_->progress();
        paragraph(layer_, install_step(progress), width);
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        std::optional<float> fraction;
        if ((progress.step == Step::Compiling || progress.step == Step::Downloading) && progress.total != 0u)
            fraction = static_cast<float>(progress.done) / static_cast<float>(progress.total);
        progress_bar(layer_, width, fraction);
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        if (action_row(layer_, "install_cancel", tr(Text::InstallCancel), "", width)) builder_->cancel();
    } else {
        if (!toolchain_) {
            install::ProcessGroup probe;
            toolchain_ = install::check_toolchain(probe);
        }
        const auto start = [this] {
            install_error_.clear();
            builder_ = std::make_unique<install::GameBuilder>(settings_.game_disc, identity_, toolchain_->download_bytes);
            focus_control("install_cancel");
        };
        switch (toolchain_->status) {
        case Status::Ready:
            if (action_row(layer_, "install_start", tr(Text::InstallStart), "", width)) start();
            break;
        case Status::Download:
            paragraph(layer_, format(Text::InstallDownloadNote, {megabytes(toolchain_->download_bytes)}), width);
            ImGui::Dummy(ImVec2(0, 4.0f * u));
            if (action_row(layer_, "install_start", tr(Text::InstallStart), "", width)) start();
            break;
        case Status::NeedXcode:
            paragraph(layer_, tr(xcode_requested_ ? Text::InstallXcodeWaiting : Text::InstallNeedXcode), width);
            ImGui::Dummy(ImVec2(0, 4.0f * u));
            if (action_row(layer_, "install_start", tr(Text::InstallGetXcode), "", width)) {
                install::request_xcode_tools();
                xcode_requested_ = true;
            }
            if (action_row(layer_, "install_check", tr(Text::InstallCheckAgain), "", width)) toolchain_.reset();
            break;
        case Status::NeedCompiler:
            paragraph(layer_, tr(Text::InstallNeedCompiler), width);
            ImGui::Dummy(ImVec2(0, 4.0f * u));
            if (action_row(layer_, "install_start", tr(Text::InstallCheckAgain), "", width)) toolchain_.reset();
            break;
        case Status::Unsupported:
            paragraph(layer_, tr(Text::InstallUnsupported), width, colors::kRed);
            break;
        }
        if (!install_error_.empty()) {
            ImGui::Dummy(ImVec2(0, 6.0f * u));
            paragraph(layer_, install_error_, width, colors::kRed);
        }
    }
    end_list(layer_);

    // Back stops an installation under way rather than leaving it running.
    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) {
        if (builder_)
            builder_->cancel();
        else
            go(Screen::Home, "play");
    }
    hints(layer_, {{Control::Confirm, tr(Text::HintSelect)},
                   {Control::Back, tr(builder_ ? Text::HintCancel : Text::HintBack)}});
}

// --- Settings ----------------------------------------------------------------

void Launcher::settings() {
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::SettingsTitle));
    const auto tabs = settings_tabs();
    std::array<std::string_view, 8> titles{};
    for (std::size_t i = 0; i < tabs.size() && i < titles.size(); ++i) titles[i] = tr(tabs[i].title);
    const int previous = tab_;
    tab_ = tab_bar(layer_, std::span(titles.data(), tabs.size()), tab_, top);
    if (tab_ != previous) focus_control("row0");

    const ImRect inner = content_panel(layer_, top + 48.0f * u, 760.0f);
    const float pad = 18.0f * u;
    const float description_height = 54.0f * u;
    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, inner.Min.y + pad));
    const float list_height = inner.GetHeight() - 2.0f * pad - description_height;
    begin_list("##rows", ImVec2(inner.GetWidth() - 2.0f * pad, list_height));
    std::string description;
    const auto &rows = tabs[static_cast<std::size_t>(tab_)].rows;
    const float row_width = inner.GetWidth() - 2.0f * pad - 12.0f * u;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const RowView view = describe(rows[i], settings_);
        const int change = setting_row(layer_, "row" + std::to_string(i), view, row_width);
        if (change != 0 && rows[i] == SettingRow::GameText && settings_.translation_language != "en" && !pack_ready()) {
            // English needs its pack: the one downloaded before, or the download screen.
            const auto source = install::english_pack_source();
            const auto kept = source ? install::english_pack_path(*source) : std::nullopt;
            std::error_code missing;
            if (kept && std::filesystem::is_regular_file(*kept, missing)) {
                settings_.translation_pack = *kept;
                settings_.translation_language = "en";
                save();
            } else {
                pack_error_.clear();
                go(Screen::Pack, "pack_download");
            }
        } else if (change != 0) {
            const shell::Settings before = settings_;
            step(rows[i], settings_, change);
            if (settings_.fullscreen != before.fullscreen) SDL_SetWindowFullscreen(layer_.window(), settings_.fullscreen);
            if (!settings_.fullscreen && (settings_.window_width != before.window_width ||
                                          settings_.window_height != before.window_height))
                SDL_SetWindowSize(layer_.window(), static_cast<int>(settings_.window_width),
                                  static_cast<int>(settings_.window_height));
            save();
        }
        if (ImGui::IsItemFocused()) description = describe(rows[i], settings_).description;
    }
    if (tabs[static_cast<std::size_t>(tab_)].title == Text::TabControls) {
        if (action_row(layer_, "bindings", tr(Text::Bindings), tr(Text::BindingsDetail), row_width))
            go(Screen::Bindings, "kb0");
        if (ImGui::IsItemFocused())
            description = tr(Text::BindingsDescription);
    }
    end_list(layer_);

    // What the focused setting does.
    const ImVec2 at(inner.Min.x + pad, inner.Max.y - pad - description_height);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y), ImVec2(inner.Max.x - pad, at.y), IM_COL32(180, 196, 226, 255),
                                        2.0f * u);
    ImGui::SetCursorScreenPos(ImVec2(at.x + 8.0f * u, at.y + 10.0f * u));
    if (!settings_error_.empty())
        paragraph(layer_, settings_error_, inner.GetWidth() - 2.0f * pad - 16.0f * u, colors::kRed);
    else
        paragraph(layer_, description, inner.GetWidth() - 2.0f * pad - 16.0f * u, colors::kInkDim);

    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) go(Screen::Home, "settings");
    hints(layer_, {{Control::Change, tr(Text::HintChange)}, {Control::Tabs, tr(Text::HintTabs)},
                   {Control::Back, tr(Text::HintBack)}});
}

// --- The English game text -----------------------------------------------------

namespace {

// The host a URL names, to say where a download comes from.
std::string url_host(std::string_view url) {
    if (const auto scheme = url.find("://"); scheme != std::string_view::npos) url.remove_prefix(scheme + 3u);
    return std::string(url.substr(0u, url.find('/')));
}

} // namespace

bool Launcher::pack_ready() const {
    if (settings_.translation_pack.empty()) return false;
    auto path = settings_.translation_pack;
    if (path.is_relative()) path = settings_path_.parent_path() / path;
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

void Launcher::pack() {
    using Step = install::Progress::Step;
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::PackTitle));
    const ImRect inner = content_panel(layer_, top, 700.0f);
    const float pad = 22.0f * u, width = inner.GetWidth() - 2.0f * pad - 12.0f * u;
    const auto source = install::english_pack_source();

    // A download that ended: English on and back to the settings, or the choice again with why.
    if (pack_download_) {
        const auto progress = pack_download_->progress();
        if (progress.step == Step::Done) {
            settings_.translation_language = "en";
            settings_.translation_pack = pack_download_->destination();
            pack_download_.reset();
            save();
            go(Screen::Settings, "row1");
            return;
        }
        if (progress.step == Step::Failed || progress.step == Step::Cancelled) {
            pack_error_ = progress.step == Step::Failed ? format(Text::PackFailed, {progress.error}) : "";
            pack_download_.reset();
            focus_control("pack_download");
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, inner.Min.y + pad * 0.8f));
    begin_list("##pack", ImVec2(inner.GetWidth() - 2.0f * pad, inner.GetHeight() - 1.6f * pad));
    paragraph(layer_, tr(Text::PackIntro), width, colors::kInkDim);
    ImGui::Dummy(ImVec2(0, 8.0f * u));
    if (!source) {
        paragraph(layer_, tr(Text::PackUnavailable), width, colors::kRed);
    } else if (pack_download_) {
        const auto progress = pack_download_->progress();
        paragraph(layer_, format(Text::PackDownloading, {megabytes(progress.done), megabytes(progress.total)}), width);
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        std::optional<float> fraction;
        if (progress.total != 0u) fraction = static_cast<float>(progress.done) / static_cast<float>(progress.total);
        progress_bar(layer_, width, fraction);
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        if (action_row(layer_, "pack_cancel", tr(Text::InstallCancel), "", width)) pack_download_->cancel();
    } else {
        paragraph(layer_, format(Text::PackSource, {url_host(source->url), megabytes(source->bytes)}), width);
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        if (action_row(layer_, "pack_download", tr(Text::PackDownload), "", width)) {
            if (const auto destination = install::english_pack_path(*source)) {
                pack_error_.clear();
                pack_download_ = std::make_unique<install::PackDownload>(*source, *destination);
                focus_control("pack_cancel");
            } else {
                pack_error_ = format(Text::PackFailed, {"there is no per-user data directory"});
            }
        }
    }
    if (!pack_error_.empty()) {
        ImGui::Dummy(ImVec2(0, 6.0f * u));
        paragraph(layer_, pack_error_, width, colors::kRed);
    }
    end_list(layer_);

    // Back stops a download under way rather than leaving it running.
    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) {
        if (pack_download_)
            pack_download_->cancel();
        else
            go(Screen::Settings, "row1");
    }
    if (source)
        hints(layer_, {{Control::Confirm, tr(Text::HintSelect)},
                       {Control::Back, tr(pack_download_ ? Text::HintCancel : Text::HintBack)}});
    else
        hints(layer_, {{Control::Back, tr(Text::HintBack)}});
}

// --- A problem -----------------------------------------------------------------

void Launcher::problem() {
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::CouldNotStart));
    const std::string message = format(Text::CouldNotStartMessage, {start_failure_});
    const ImVec2 size = display();
    const float width = std::min(size.x - 48.0f * u, 620.0f * u);
    const float pad = 26.0f * u;
    const float text_height = text_size(fonts().body, 18.0f * u, message, width - 2.0f * pad).y;
    const float height = text_height + 2.0f * pad + 80.0f * u;
    const ImVec2 min((size.x - width) * 0.5f, std::max(top + 10.0f * u, (size.y - height) * 0.5f));
    panel(layer_, min, ImVec2(min.x + width, min.y + height));
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad, min.y + pad));
    paragraph(layer_, message, width - 2.0f * pad);
    const ImVec2 button(200.0f * u, 50.0f * u);
    ImGui::SetCursorScreenPos(ImVec2(min.x + (width - button.x) * 0.5f, min.y + height - pad - button.y + 6.0f * u));
    if (menu_button(layer_, "problem_ok", tr(Text::Ok), button) || layer_.back()) {
        start_failure_.clear();
        go(screen_, screen_ == Screen::Home ? "play" : "");
    }
    hints(layer_, {{Control::Confirm, tr(Text::HintOk)}});
}

// --- Bindings ------------------------------------------------------------------

namespace {
double seconds_now() { return static_cast<double>(SDL_GetTicksNS()) * 1e-9; }
std::string cell_id(BindingDevice device, std::size_t action) {
    return (device == BindingDevice::Keyboard ? "kb" : "pad") + std::to_string(action);
}
} // namespace

bool Launcher::capture_event(const SDL_Event &event) {
    if (!capture_.active()) return false;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN: capture_.key(event.key.scancode, event.key.repeat); return true;
    case SDL_EVENT_KEY_UP: return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        capture_.button(static_cast<SDL_GamepadButton>(event.gbutton.button), event.gbutton.down, seconds_now());
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        capture_.axis(static_cast<SDL_GamepadAxis>(event.gaxis.axis), static_cast<float>(event.gaxis.value) / 32767.0f);
        return true;
    // A click or tap cancels; its release then presses nothing.
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_FINGER_DOWN: capture_.pointer(); return true;
    default: return false;
    }
}

void Launcher::bindings() {
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::BindingsTitle));
    const ImRect inner = content_panel(layer_, top, 900.0f);
    const float pad = 18.0f * u;
    ImDrawList *draw = ImGui::GetWindowDrawList();

    // Whatever the capture made of the last input.
    capture_.update(seconds_now());
    if (capture_.outcome() == BindingCapture::Outcome::Bound || capture_.outcome() == BindingCapture::Outcome::Cancelled) {
        const BindingDevice device = capture_.device();
        if (const auto binding = capture_.take()) {
            if (capture_add_)
                add_binding(settings_.controls, device, capture_action_, *binding);
            else
                set_binding(settings_.controls, device, capture_action_, *binding);
            save();
        }
        layer_.block_pads(false);
        focus_control(cell_id(device, static_cast<std::size_t>(capture_action_)));
    }
    const bool capturing = capture_.active();

    // Column headings, then a row per PSP control.
    const float width = inner.GetWidth() - 2.0f * pad - 12.0f * u;
    const float label_width = std::min(240.0f * u, width * 0.34f);
    const float gap = 10.0f * u;
    const float cell_width = (width - label_width - gap) * 0.5f - gap * 0.5f;
    const float keyboard_x = inner.Min.x + pad + label_width + gap, gamepad_x = keyboard_x + cell_width + gap;
    {
        ImFont *font = fonts().strong;
        const float size = 16.0f * u, y = inner.Min.y + 12.0f * u;
        draw->AddText(font, size, ImVec2(keyboard_x + 10.0f * u, y), colors::kInkDim, tr(Text::ColumnKeyboard).data(),
                      tr(Text::ColumnKeyboard).data() + tr(Text::ColumnKeyboard).size());
        draw->AddText(font, size, ImVec2(gamepad_x + 10.0f * u, y), colors::kInkDim, tr(Text::ColumnGamepad).data(),
                      tr(Text::ColumnGamepad).data() + tr(Text::ColumnGamepad).size());
    }
    const float list_top = inner.Min.y + 38.0f * u;
    const float description_height = 52.0f * u;
    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, list_top));
    ImGui::BeginDisabled(capturing);
    begin_list("##bindings", ImVec2(inner.GetWidth() - 2.0f * pad, inner.Max.y - pad - description_height - list_top));
    std::string description;
    const float row_height = 40.0f * u;
    for (std::size_t i = 0; i < shell::kActionCount; ++i) {
        const auto action = static_cast<shell::Action>(i);
        const float y = ImGui::GetCursorScreenPos().y;
        const std::string_view name = tr(action_text(action));
        ImFont *font = fonts().body;
        const ImVec2 extent = text_size(font, 17.0f * u, name);
        ImGui::GetWindowDrawList()->AddText(font, 17.0f * u, ImVec2(inner.Min.x + pad + 10.0f * u, y + (row_height - extent.y) * 0.5f),
                                            colors::kInk, name.data(), name.data() + name.size());
        for (const BindingDevice device : {BindingDevice::Keyboard, BindingDevice::Gamepad}) {
            const auto &bound = bindings_of(settings_.controls, device, action);
            std::vector<Glyph> glyphs;
            for (const auto &binding : bound) glyphs.push_back(glyph(binding, layer_.pad_type()));
            ImGui::SetCursorScreenPos(ImVec2(device == BindingDevice::Keyboard ? keyboard_x : gamepad_x, y));
            const CellAction act = binding_cell(layer_, cell_id(device, i), glyphs, ImVec2(cell_width, row_height));
            if (ImGui::IsItemFocused() && !capturing) {
                std::string shared;
                for (const auto &binding : bound)
                    for (const auto other : sharing(settings_.controls, device, action, binding)) {
                        if (!shared.empty()) shared += ", ";
                        shared += tr(action_text(other));
                    }
                description = bound.empty() ? std::string(tr(Text::NotBound))
                            : shared.empty() ? std::string(tr(Text::DescBindingCell))
                                             : format(Text::AlsoUsedFor, {shared});
            }
            switch (act) {
            case CellAction::None: break;
            case CellAction::Clear:
                clear_bindings(settings_.controls, device, action);
                save();
                break;
            case CellAction::Set:
            case CellAction::Add: {
                capture_action_ = action;
                capture_add_ = act == CellAction::Add;
                const auto axes = layer_.pad_axes();
                const SDL_GamepadButton back = settings_.confirm_south ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
                capture_.start(device, seconds_now(), back, axes);
                if (device == BindingDevice::Gamepad) layer_.block_pads(true);
                break;
            }
            }
        }
    }
    ImGui::Dummy(ImVec2(0, 4.0f * u));
    for (const auto &[id, text, device] : {std::tuple{"reset_kb", Text::ResetKeyboard, BindingDevice::Keyboard},
                                           std::tuple{"reset_pad", Text::ResetGamepad, BindingDevice::Gamepad}}) {
        if (action_row(layer_, id, tr(text), "", width)) {
            reset_bindings(settings_.controls, device);
            save();
        }
        if (ImGui::IsItemFocused()) description = tr(Text::DescReset);
    }
    end_list(layer_);
    ImGui::EndDisabled();

    // What the focused cell is bound to, or what went wrong saving.
    const ImVec2 at(inner.Min.x + pad, inner.Max.y - pad - description_height);
    draw->AddLine(at, ImVec2(inner.Max.x - pad, at.y), IM_COL32(180, 196, 226, 255), 2.0f * u);
    ImGui::SetCursorScreenPos(ImVec2(at.x + 8.0f * u, at.y + 10.0f * u));
    paragraph(layer_, settings_error_.empty() ? description : settings_error_, inner.GetWidth() - 2.0f * pad - 16.0f * u,
              settings_error_.empty() ? colors::kInkDim : colors::kRed);

    if (capturing) {
        capture_overlay();
        return;
    }
    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) go(Screen::Settings, "bindings");
    hints(layer_, {{Control::Confirm, tr(Text::HintSet)}, {Control::Add, tr(Text::HintAdd)},
                   {Control::Clear, tr(Text::HintClear)}, {Control::Back, tr(Text::HintBack)}});
}

void Launcher::capture_overlay() {
    const float u = layer_.unit();
    const double now = seconds_now();
    const ImVec2 size = display();
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), size, IM_COL32(8, 26, 102, 150));

    const bool keyboard = capture_.device() == BindingDevice::Keyboard;
    const float width = std::min(size.x - 48.0f * u, 600.0f * u), pad = 26.0f * u;
    const float text_width = width - 2.0f * pad;
    const std::string title = format(keyboard ? Text::CaptureKey : Text::CapturePad, {tr(action_text(capture_action_))});
    ImFont *strong = fonts().strong, *body = fonts().body;
    const float title_size = 22.0f * u, body_size = 17.0f * u;
    const ImVec2 title_extent = text_size(strong, title_size, title, text_width);
    const float height = pad + 30.0f * u + title_extent.y + 10.0f * u + 24.0f * u + 70.0f * u + 30.0f * u + pad;
    const ImVec2 min((size.x - width) * 0.5f, (size.y - height) * 0.5f), max(min.x + width, min.y + height);
    panel(layer_, min, max, draw);

    float y = min.y + pad;
    siren_lamp(draw, ImVec2(size.x * 0.5f, y + 10.0f * u), 11.0f * u, layer_.time());
    y += 30.0f * u;
    const auto centred = [&](ImFont *font, float px, std::string_view text, ImU32 color) {
        const ImVec2 extent = text_size(font, px, text, text_width);
        // Wrapped lines start at the left edge of the text block.
        const float x = extent.x < text_width ? (size.x - extent.x) * 0.5f : min.x + pad;
        draw->AddText(font, px, ImVec2(x, y), color, text.data(), text.data() + text.size(), text_width);
        y += extent.y;
    };
    centred(strong, title_size, title, colors::kInk);
    y += 6.0f * u;
    centred(body, body_size, tr(capture_add_ ? Text::CaptureAdding : Text::CaptureReplacing), colors::kInkDim);
    y += 14.0f * u;

    // How to cancel: Esc, or holding the back button (with its progress),
    // and the time left.
    const float row_center = y + 28.0f * u;
    if (keyboard) {
        const Glyph esc{Glyph::Kind::Key, "Esc"};
        ImVec2 cursor(0.0f, 0.0f);
        draw_glyph(nullptr, cursor, u, layer_.pad_type(), esc);
        cursor = ImVec2((size.x - cursor.x) * 0.5f, row_center);
        draw_glyph(draw, cursor, u, layer_.pad_type(), esc);
        y = row_center + 22.0f * u;
        centred(body, body_size, tr(Text::CaptureKeyCancel), colors::kInkDim);
    } else {
        const Glyph back{Glyph::Kind::Face, "", settings_.confirm_south ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH};
        const std::string_view hold = tr(Text::CaptureHoldCancel);
        const ImVec2 hold_extent = text_size(strong, body_size, hold);
        ImVec2 cursor(0.0f, 0.0f);
        draw_glyph(nullptr, cursor, u, layer_.pad_type(), back);
        const float total = cursor.x + 8.0f * u + hold_extent.x;
        cursor = ImVec2((size.x - total) * 0.5f, row_center);
        const ImVec2 ring(cursor.x + 13.0f * u, row_center);
        draw_glyph(draw, cursor, u, layer_.pad_type(), back);
        if (const float progress = capture_.cancel_progress(now); progress > 0.0f) {
            draw->PathArcTo(ring, 18.0f * u, -1.5708f, -1.5708f + progress * 6.2832f, 32);
            draw->PathStroke(colors::kRed, 4.0f * u);
        }
        draw->AddText(strong, body_size, ImVec2(cursor.x + 8.0f * u, row_center - hold_extent.y * 0.5f), colors::kInk,
                      hold.data(), hold.data() + hold.size());
        y = row_center + 22.0f * u;
        if (!capture_.pressing())
            centred(body, body_size, format(Text::CaptureSeconds, {std::to_string(capture_.seconds_left(now))}),
                    colors::kInkDim);
        else
            y += body_size;
    }
    y += 6.0f * u;
    centred(body, 15.0f * u, tr(Text::CapturePointerCancel), colors::kInkDim);
}

} // namespace sarusaru::ui
