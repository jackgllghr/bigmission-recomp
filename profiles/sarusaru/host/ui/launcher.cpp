#include "save_state/native_state.hpp"
#include "ui/launcher.hpp"

#include "game_module.hpp"
#include "platform/paths.hpp"
#include "utility/savedata_dialog.hpp"
#include "save_data/savedata_store.hpp"
#include <ctime>
#include "platform/background_work.hpp"
#include "platform/documents.hpp"

#include "ui/text.hpp"
#include "ui/theme.hpp"
#include "ui/ui_script.hpp"
#include "release_version.hpp"
#include <ctime>
#include "ui/widgets.hpp"

#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
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

// The settings screen's focus ID for `row`, on its tab (rows are "row" and their index there).
std::string row_id(SettingRow row) {
    for (const auto &tab : settings_tabs())
        if (const auto at = std::find(tab.rows.begin(), tab.rows.end(), row); at != tab.rows.end())
            return "row" + std::to_string(at - tab.rows.begin());
    return "row0";
}

std::string utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

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

Launcher::Launcher(Layer &layer, shell::Settings &settings, std::filesystem::path settings_path, DiscIdentity identity,
                   Mode mode)
    : layer_(layer), mode_(mode), settings_(settings), settings_path_(std::move(settings_path)), identity_(identity) {
    set_offered_downloads(install::english_pack_source().has_value(), hd_offered());
    if (in_game()) return;
    if (!settings_.game_disc.empty()) disc_check_ = check_disc(settings_.game_disc, identity_);
    load_title_art();
}

Launcher::~Launcher() {
    clear_slot_previews();
    layer_.renderer().remove_texture(title_art_);
}

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
    // The art is sized for the game's HD title texture; the launcher draws it
    // far smaller and without mipmaps, so halve it (a 2x2 average) first.
    while (rgba->h > 1024) {
        SDL_Surface *half = SDL_ScaleSurface(rgba, rgba->w / 2, rgba->h / 2, SDL_SCALEMODE_LINEAR);
        if (half == nullptr) break;
        SDL_DestroySurface(rgba);
        rgba = half;
    }
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
    settings_error_.clear();
    if (in_game())
        go(Screen::Paused, "resume");
    else
        go(Screen::Home, start_failure_.empty() ? "play" : "problem_ok");
#if !defined(__ANDROID__)
    if (!in_game() && settings_.check_for_updates && !update_check_ &&
        install::update_check_due(std::time(nullptr), settings_.last_update_check)) check_updates();
#endif
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
        layer_.set_prompt_style(settings_.prompt_style);
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
    if (screen == Screen::Saves) refresh_slots();
    screen_ = screen;
    focus_control(focus);
}

bool Launcher::hd_offered() {
    return install::prebuilt_pack_source() || (install::texture_recipe_source() && install::upscaler_source());
}

void Launcher::play() {
    // A new player's first Play: the Modern preset's downloads the build
    // offers, each on its own screen (Back skips it), then the game.
    if (settings_.modern_downloads_pending) {
        settings_.modern_downloads_pending = false;
        save();
        if (settings_.internal_resolution == kModernResolution && settings_.draw_distance == kModernDrawDistance &&
            settings_.skip_intro_logos) {
            preset_pending_.clear();
            if (install::english_pack_source()) preset_pending_.push_back(SettingRow::GameText);
            if (hd_offered()) preset_pending_.push_back(SettingRow::Textures);
            pack_return_screen_ = Screen::Home;
            preset_active_ = true;
            play_after_preset_ = true;
            const Screen before = screen_;
            continue_preset();
            if (screen_ != before || outcome_) return;
            play_after_preset_ = false;
        }
    }
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
    if (in_game())
        // The paused game, dimmed in the series' blue.
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display(), IM_COL32(8, 26, 102, 170));
    else
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
        case Screen::Install: install(); break;
        case Screen::Settings: settings(); break;
        case Screen::Bindings: bindings(); break;
        case Screen::Pack: pack(); break;
        case Screen::Textures: textures(); break;
        case Screen::Paused: paused(); break;
        case Screen::Saves: saves(); break;
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
    const auto update = update_check_ ? update_check_->result() : install::UpdateResult{};
    const bool has_update = update_check_ && update.state == install::UpdateResult::State::Available;
    const float buttons_height = (has_update ? 5.0f : 4.0f) * button.y + (has_update ? 4.0f : 3.0f) * gap;
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
        // Not ready: what is wrong with the disc last chosen (now, or before).
        std::string status;
        if (ready) status = tr(Text::StatusReady);
        else if (!disc_message_.empty()) status = disc_message_;
        else if (disc_check_)
            status = format(Text::DiscProblemFor, {platform::display_name(settings_.game_disc), disc_problem(*disc_check_, identity_.disc_id)});
        else status = format(Text::StatusChoose, {identity_.disc_id});
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
    // Without a disc to play, the first button finds one with the system's file picker.
    if (ready) {
        if (menu_button(layer_, "play", tr(Text::Play), button, true)) play();
    } else if (menu_button(layer_, "locate", tr(dialog_open_ ? Text::WaitingForPicker : Text::LocateDisc), button, true,
                           !dialog_open_)) {
        open_system_dialog();
    }
    place(1);
    if (menu_button(layer_, "settings", tr(Text::Settings), button)) go(Screen::Settings, "row0");
    place(2);
    if (menu_button(layer_, "saves", tr(Text::Saves), button))
        go(Screen::Saves, "slot"+std::to_string(settings_.state_slot));
    place(3);
    if (menu_button(layer_, "quit", tr(Text::Quit), button)) outcome_ = Outcome::Quit;

    if (has_update) {
        place(4);
        if (menu_button(layer_, "update_notice", format(Text::UpdateAvailable, {update.version}), button)) {
            tab_ = static_cast<int>(settings_tabs().size()) - 1;
            go(Screen::Settings, "row0");
        }
    }

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

// --- The pause menu ----------------------------------------------------------

void Launcher::paused() {
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::PausedTitle));
    const ImVec2 size = display();
    const ImVec2 button(300.0f * u, 48.0f * u);
    const float gap = 12.0f * u;
    const float x = (size.x - button.x) * 0.5f;
    const float buttons_height = 4.0f * button.y + 3.0f * gap;
    const float y = std::max(top + 10.0f * u, (size.y - buttons_height) * 0.5f);
    const auto place = [&](int index) { ImGui::SetCursorScreenPos(ImVec2(x, y + static_cast<float>(index) * (button.y + gap))); };
    place(0);
    if (menu_button(layer_, "resume", tr(Text::Resume), button, true)) outcome_ = Outcome::Resume;
    place(1);
    if (menu_button(layer_, "settings", tr(Text::Settings), button)) go(Screen::Settings, "row0");
    place(2);
    if (menu_button(layer_, "saves", tr(Text::Saves), button))
        go(Screen::Saves, "slot"+std::to_string(settings_.state_slot));
    place(3);
    if (menu_button(layer_, "quit", tr(Text::QuitGame), button)) outcome_ = Outcome::Quit;
    if (layer_.back()) outcome_ = Outcome::Resume;
    hints(layer_, {{Control::Confirm, tr(Text::HintSelect)}, {Control::Back, tr(Text::HintResume)}});
}

// --- Save states and the game's memory-stick saves -----------------------------

void Launcher::clear_slot_previews() {
    for (auto &texture : slot_previews_) {
        layer_.renderer().remove_texture(texture);
        texture = ImTextureID{};
    }
}

void Launcher::refresh_slots() {
    clear_slot_previews();
    saves_error_.clear();
    for (std::uint32_t slot=1; slot<=save_state::kSlotCount; ++slot) {
        auto &info=slots_[slot-1];
        info=save_state::slot_info(save_state::slot_path(settings_path_,slot));
        if (!info.thumbnail.rgba.empty())
            slot_previews_[slot-1]=layer_.renderer().add_texture(info.thumbnail.rgba.data(),info.thumbnail.width,info.thumbnail.height);
    }
}

void Launcher::open_saves_folder() {
    try {
        const auto folder=savedata::savedata_root(utility::default_memory_stick());
        std::filesystem::create_directories(folder);
        if (!SDL_OpenURL(platform::directory_url(folder).c_str())) throw std::runtime_error(SDL_GetError());
        saves_error_.clear();
    } catch (const std::exception &error) {
        saves_error_=format(Text::OpenSavesFailed,{error.what()});
    }
}

void Launcher::saves() {
    const float u=layer_.unit();
    const float top=screen_title(layer_,tr(Text::Saves))+8*u;
    const auto size=display();
    const float width=std::min(780*u,size.x-64*u), x=(size.x-width)*0.5f;
    const float row_height=54*u, gap=8*u;
    auto *draw=ImGui::GetWindowDrawList();
    for (std::uint32_t slot=1; slot<=save_state::kSlotCount; ++slot) {
        const auto &info=slots_[slot-1];
        const float y=top+(slot-1)*(row_height+gap);
        ImGui::SetCursorScreenPos(ImVec2(x,y));
        if (menu_button(layer_,"slot"+std::to_string(slot),"",ImVec2(width,row_height))) {
            settings_.state_slot=slot;
            save();
            saves_error_=settings_error_;
        }
        const ImVec2 image_min(x+42*u,y+4*u), image_max(x+123*u,y+50*u);
        if (slot_previews_[slot-1]!=ImTextureID{}) draw->AddImage(slot_previews_[slot-1],image_min,image_max);
        else {
            draw->AddRectFilled(image_min,image_max,colors::kBlueDeep,4*u);
            const auto text=tr(info.occupied ? Text::NoThumbnail : Text::EmptySlot);
            const auto extent=text_size(fonts().body,12*u,text,76*u);
            draw->AddText(fonts().body,12*u,ImVec2(image_min.x+3*u,image_min.y+(46*u-extent.y)*0.5f),colors::kWhite,
                          text.data(),text.data()+text.size(),76*u);
        }
        const auto label=format(settings_.state_slot==slot ? Text::SelectedStateSlot : Text::StateSlot,{std::to_string(slot)});
        draw->AddText(fonts().strong,21*u,ImVec2(x+142*u,y+5*u),colors::kBlue,label.c_str());
        std::string detail(tr(Text::EmptySlot));
        if (info.occupied) {
            detail=std::string(tr(Text::NoThumbnail));
            if (info.saved_at>0) {
                const std::time_t stamp=static_cast<std::time_t>(info.saved_at);
                std::tm local{};
#if defined(_WIN32)
                const bool valid=localtime_s(&local,&stamp)==0;
#else
                const bool valid=localtime_r(&stamp,&local)!=nullptr;
#endif
                char time[64]{};
                if (valid && std::strftime(time,sizeof time,"%Y-%m-%d  %H:%M:%S",&local)) detail=time;
            }
        }
        draw->AddText(fonts().body,16*u,ImVec2(x+142*u,y+30*u),colors::kInkDim,detail.c_str());
    }
    const float action_y=top+save_state::kSlotCount*(row_height+gap)+8*u;
    const float save_width=180*u, folder_width=280*u, action_gap=16*u;
    const float action_x=(size.x-2*save_width-folder_width-2*action_gap)*0.5f;
    ImGui::SetCursorScreenPos(ImVec2(action_x,action_y));
    if (menu_button(layer_,"save_state",tr(Text::SaveState),ImVec2(save_width,36*u),false,in_game())) outcome_=Outcome::SaveState;
    ImGui::SetCursorScreenPos(ImVec2(action_x+save_width+action_gap,action_y));
    const bool can_load=(in_game() || (disc_check_ && disc_check_->supported() && find_game_module().has_value())) && slots_[settings_.state_slot-1].occupied;
    if (menu_button(layer_,"load_state",tr(Text::LoadState),ImVec2(save_width,36*u),false,can_load)) outcome_=Outcome::LoadState;
    ImGui::SetCursorScreenPos(ImVec2(action_x+2*(save_width+action_gap),action_y));
    if (menu_button(layer_,"open_saves",tr(Text::OpenSavesFolder),ImVec2(folder_width,36*u))) open_saves_folder();
    ImGui::SetCursorScreenPos(ImVec2(x,action_y+48*u));
    const auto help=format(Text::StateSlotShortcuts,{std::to_string(settings_.state_slot)});
    paragraph(layer_,saves_error_.empty() ? help : saves_error_,width,saves_error_.empty() ? colors::kInkDim : colors::kRed);
    if (layer_.back() || back_button(layer_,ImVec2(24*u,size.y-70*u)))
        go(in_game() ? Screen::Paused : Screen::Home,"saves");
    hints(layer_,{{Control::Confirm,tr(Text::HintSelect)},{Control::Back,tr(Text::Back)}});
}

// --- The game disc ---------------------------------------------------------

void Launcher::choose_disc(const std::filesystem::path &path) {
    const DiscCheck check = check_disc(path, identity_);
    const std::string name = platform::display_name(path);
    if (check.supported()) {
        settings_.game_disc = path;
        disc_check_ = check;
        save();
        disc_message_.clear();
        if (screen_ == Screen::Home) focus_control("play");
    } else {
        disc_message_ = format(Text::DiscProblemFor, {name, disc_problem(check, identity_.disc_id)});
        if (screen_ == Screen::Home) focus_control("locate");
    }
}

void Launcher::open_system_dialog() {
    if (dialog_open_) return;
    dialog_open_ = true;
    // SDL keeps the filters until the dialog closes.
    static std::string filter_name;
    filter_name = tr(Text::PickerFilter);
    static const SDL_DialogFileFilter kFilters[] = {{filter_name.c_str(), "iso;ISO"}};
    // A document URI has no folder to start in; the picker remembers its own.
    const std::string start = settings_.game_disc.empty() || platform::is_document(settings_.game_disc)
                                  ? std::string()
                                  : utf8(settings_.game_disc.parent_path());
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

// How far the step has got, while that is known.
std::optional<float> install_fraction(const install::Progress &progress) {
    using Step = install::Progress::Step;
    if ((progress.step == Step::Compiling || progress.step == Step::Downloading) && progress.total != 0u)
        return static_cast<float>(progress.done) / static_cast<float>(progress.total);
    return std::nullopt;
}

// The installation's progress for the system (platform/background_work.hpp),
// on the installation's own threads: the launcher's may be paused meanwhile.
install::GameBuilder::Observer install_notification() {
    using Clock = std::chrono::steady_clock;
    using Step = install::Progress::Step;
    return [last = std::optional<Step>{}, shown = Clock::time_point{}](const install::Progress &progress) mutable {
        const auto title = tr(Text::InstallNotifyTitle);
        if (progress.finished()) {
            if (progress.step == Step::Done) platform::end_background_work(title, tr(Text::InstallNotifyDone));
            else if (progress.step == Step::Failed) platform::end_background_work(title, tr(Text::InstallNotifyFailed));
            else platform::end_background_work(title, "");
            return;
        }
        // Each step at once; counts within one at most twice a second.
        const auto now = Clock::now();
        if (last == progress.step && now - shown < std::chrono::milliseconds(500)) return;
        last = progress.step;
        shown = now;
        platform::show_background_work(title, install_step(progress), install_fraction(progress));
    };
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
        progress_bar(layer_, width, install_fraction(progress));
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        if (action_row(layer_, "install_cancel", tr(Text::InstallCancel), "", width)) builder_->cancel();
    } else {
        paragraph(layer_, tr(Text::InstallOptions), width, colors::kInkDim);
        for (const auto row : {SettingRow::Textures, SettingRow::GameText}) {
            const auto id = row == SettingRow::Textures ? "install_textures" : "install_game_text";
            const int change = setting_row(layer_, id, describe(row, settings_), width);
            if (ImGui::IsItemFocused())
                paragraph(layer_, describe(row, settings_).description, width, colors::kInkDim);
            if (change != 0) {
                pack_return_screen_ = Screen::Install;
                if (row == SettingRow::Textures) choose_textures(change);
                else choose_game_text(change);
            }
        }
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        if (!settings_error_.empty()) paragraph(layer_, settings_error_, width, colors::kRed);
        if (!toolchain_) {
            install::ProcessGroup probe;
            toolchain_ = install::check_toolchain(probe);
        }
        const auto start = [this] {
            install_error_.clear();
            builder_ = std::make_unique<install::GameBuilder>(settings_.game_disc, identity_, toolchain_->download_bytes,
                                                              install_notification());
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
    if (builder_)
        hints(layer_, {{Control::Back, tr(Text::HintCancel)}});
    else
        hints(layer_, {{Control::Confirm, tr(Text::HintSelect)}, {Control::Change, tr(Text::HintChange)},
                       {Control::Back, tr(Text::HintBack)}});
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
        const RowView view = describe(rows[i], settings_, in_game());
        const int change = setting_row(layer_, "row" + std::to_string(i), view, row_width);
        if (change != 0 && rows[i] == SettingRow::Preset) {
            pack_return_screen_ = Screen::Settings;
            choose_preset(change);
        } else if (change != 0 && rows[i] == SettingRow::Textures) {
            pack_return_screen_ = Screen::Settings;
            choose_textures(change);
        } else if (change != 0 && rows[i] == SettingRow::GameText) {
            pack_return_screen_ = Screen::Settings;
            choose_game_text(change);
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
        if (ImGui::IsItemFocused()) description = describe(rows[i], settings_, in_game()).description;
    }
    if (tabs[static_cast<std::size_t>(tab_)].title == Text::TabControls) {
        if (action_row(layer_, "bindings", tr(Text::Bindings), tr(Text::BindingsDetail), row_width))
            go(Screen::Bindings, "kb0");
        if (ImGui::IsItemFocused())
            description = tr(Text::BindingsDescription);
    }
    if (tabs[static_cast<std::size_t>(tab_)].title == Text::TabUpdates) updates(row_width);
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

    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back())
        go(in_game() ? Screen::Paused : Screen::Home, "settings");
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

void Launcher::return_from_pack(SettingRow row) {
    go(pack_return_screen_, pack_return_screen_ == Screen::Install ? (row == SettingRow::Textures ? "install_textures" : "install_game_text")
                            : pack_return_screen_ == Screen::Home ? "play"
                                                                   : row_id(preset_active_ ? SettingRow::Preset : row));
    continue_preset();
}

void Launcher::choose_preset(int direction) {
    settings_error_.clear();
    const Preset preset = next_preset(preset_of(settings_), direction);
    apply_preset(settings_, preset);
    save();
    preset_pending_.clear();
    if (preset == Preset::Modern) preset_pending_ = {SettingRow::GameText, SettingRow::Textures};
    preset_active_ = true;
    continue_preset();
}

// Each step either turns its row on at once (continuing to the next) or opens
// its download screen, which comes back here through return_from_pack(); a
// download cancelled there leaves that row as it was, and the preset Custom.
void Launcher::continue_preset() {
    while (!preset_pending_.empty()) {
        const SettingRow row = preset_pending_.front();
        preset_pending_.erase(preset_pending_.begin());
        const Screen before = screen_;
        if (row == SettingRow::GameText && settings_.translation_language != "en" && install::english_pack_source())
            choose_game_text(1);
        if (row == SettingRow::Textures && texture_scale(settings_) != 4u && hd_offered()) select_textures(4u);
        if (screen_ != before) return;
    }
    preset_active_ = false;
    if (play_after_preset_) {
        play_after_preset_ = false;
        play();
    }
}

void Launcher::choose_game_text(int direction) {
    if (settings_.translation_language != "en" && !pack_ready()) {
        // English needs its pack: one downloaded before, or the download screen.
        const auto source = install::english_pack_source();
        const auto kept = source ? install::english_pack_path(*source) : std::nullopt;
        std::error_code missing;
        if (kept && std::filesystem::is_regular_file(*kept, missing)) {
            settings_.translation_pack = *kept;
            settings_.translation_language = "en";
            save();
        } else if (in_game()) {
            settings_error_ = tr(Text::GameTextInLauncher);
        } else {
            pack_error_.clear();
            go(Screen::Pack, "pack_download");
        }
    } else {
        step(SettingRow::GameText, settings_, direction);
        save();
    }
}

void Launcher::choose_textures(int direction) {
    select_textures(next_texture_scale(texture_scale(settings_), direction));
}

void Launcher::select_textures(std::uint32_t scale) {
    if (scale == 0u) {
        settings_.texture_pack.clear();
        save();
        return;
    }
    // HD is the prebuilt pack, downloaded once, when the build offers one.
    if (const auto prebuilt = install::prebuilt_pack_source(); prebuilt && scale == 4u) {
        const auto directory = install::prebuilt_pack_directory(*prebuilt);
        if (directory && install::texture_pack_state(*directory) != install::TexturePackState::Missing) {
            settings_.texture_pack = *directory;
            save();
        } else if (in_game()) {
            settings_error_ = tr(Text::MakeInLauncher);
        } else {
            texture_scale_ = scale;
            texture_error_.clear();
            go(Screen::Textures, "textures_make");
        }
        return;
    }
    const auto recipe = install::texture_recipe_source();
    const auto directory = recipe ? install::texture_pack_directory(*recipe, scale) : std::nullopt;
    // In game a pack made before serves as it is, even with textures collected to add to it.
    const auto state = directory ? install::texture_pack_state(*directory) : install::TexturePackState::Missing;
    if (state == install::TexturePackState::Ready || (in_game() && state == install::TexturePackState::Collected)) {
        settings_.texture_pack = *directory;
        save();
        return;
    }
    // Not made yet, or the game collected textures to add: the screen that
    // makes them, which is the launcher's.
    if (in_game()) {
        settings_error_ = tr(Text::MakeInLauncher);
        return;
    }
    texture_scale_ = scale;
    texture_error_.clear();
    go(Screen::Textures, "textures_make");
}

void Launcher::textures() {
    if (texture_scale_ == 4u && install::prebuilt_pack_source()) {
        prebuilt_textures();
        return;
    }
    using Step = install::TextureProgress::Step;
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::TexturesTitle));
    const ImRect inner = content_panel(layer_, top, 700.0f);
    const float pad = 22.0f * u, width = inner.GetWidth() - 2.0f * pad - 12.0f * u;
    const auto recipe = install::texture_recipe_source();
    const auto upscaler = install::upscaler_source();
    const auto directory = recipe ? install::texture_pack_directory(*recipe, texture_scale_) : std::nullopt;
    const std::string choice(tr(texture_scale_ == 8u ? Text::ValueTextures4k : Text::ValueTexturesHd));

    // A build that ended: the pack on and back to the settings, or the choice again with why.
    if (texture_build_) {
        const auto progress = texture_build_->progress();
        if (progress.step == Step::Done) {
            settings_.texture_pack = texture_build_->destination();
            texture_build_.reset();
            save();
            return_from_pack(SettingRow::Textures);
            return;
        }
        if (progress.step == Step::Failed || progress.step == Step::Cancelled) {
            texture_error_ = progress.step == Step::Failed ? format(Text::TexturesFailed, {progress.error}) : "";
            texture_build_.reset();
            focus_control("textures_make");
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, inner.Min.y + pad * 0.8f));
    begin_list("##textures", ImVec2(inner.GetWidth() - 2.0f * pad, inner.GetHeight() - 1.6f * pad));
    paragraph(layer_, tr(Text::TexturesIntro), width, colors::kInkDim);
    ImGui::Dummy(ImVec2(0, 8.0f * u));
    if (!recipe || !directory) {
        paragraph(layer_, !recipe ? std::string(tr(Text::TexturesUnavailable))
                                  : format(Text::TexturesFailed, {"there is no per-user data directory"}),
                  width, colors::kRed);
    } else if (!upscaler) {
        paragraph(layer_, tr(Text::TexturesNoUpscaler), width, colors::kRed);
    } else if (settings_.game_disc.empty() || !disc_present(settings_.game_disc)) {
        paragraph(layer_, tr(Text::TexturesNeedDisc), width, colors::kRed);
    } else if (texture_build_) {
        const auto progress = texture_build_->progress();
        std::string status;
        switch (progress.step) {
        case Step::Downloading:
            status = format(Text::TexturesDownloading, {megabytes(progress.done), megabytes(progress.total)});
            break;
        case Step::Unpacking: status = tr(Text::TexturesUnpacking); break;
        case Step::Reading: status = tr(Text::TexturesReading); break;
        case Step::Upscaling:
            status = format(Text::TexturesUpscaling, {std::to_string(progress.done), std::to_string(progress.total)});
            break;
        default:
            status = format(Text::TexturesWriting, {std::to_string(progress.done), std::to_string(progress.total)});
            break;
        }
        paragraph(layer_, status, width);
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        std::optional<float> fraction;
        if (progress.total != 0u && progress.step != Step::Reading && progress.step != Step::Unpacking)
            fraction = static_cast<float>(progress.done) / static_cast<float>(progress.total);
        progress_bar(layer_, width, fraction);
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        if (action_row(layer_, "textures_cancel", tr(Text::InstallCancel), "", width)) texture_build_->cancel();
    } else {
        if (const auto bytes = install::texture_download_bytes(*recipe, *upscaler); bytes != 0u) {
            paragraph(layer_, format(Text::TexturesSource, {megabytes(bytes), url_host(upscaler->archive.url)}), width);
            ImGui::Dummy(ImVec2(0, 4.0f * u));
        }
        const bool update = install::texture_pack_state(*directory) == install::TexturePackState::Collected;
        const std::string label = update ? std::string(tr(Text::TexturesUpdate)) : format(Text::TexturesMake, {choice});
        if (action_row(layer_, "textures_make", label, "", width)) {
            texture_error_.clear();
            texture_build_ = std::make_unique<install::TextureBuild>(*recipe, *upscaler, settings_.game_disc,
                                                                     std::string(identity_.disc_id), texture_scale_, *directory);
            focus_control("textures_cancel");
        }
    }
    if (!texture_error_.empty()) {
        ImGui::Dummy(ImVec2(0, 6.0f * u));
        paragraph(layer_, texture_error_, width, colors::kRed);
    }
    end_list(layer_);

    // Back stops a build under way rather than leaving it running.
    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) {
        if (texture_build_)
            texture_build_->cancel();
        else
            return_from_pack(SettingRow::Textures);
    }
    hints(layer_, {{Control::Confirm, tr(Text::HintSelect)},
                   {Control::Back, tr(texture_build_ ? Text::HintCancel : Text::HintBack)}});
}

void Launcher::prebuilt_textures() {
    using Step = install::TextureProgress::Step;
    const float u = layer_.unit();
    const float top = screen_title(layer_, tr(Text::TexturesTitle));
    const ImRect inner = content_panel(layer_, top, 700.0f);
    const float pad = 22.0f * u, width = inner.GetWidth() - 2.0f * pad - 12.0f * u;
    const auto source = install::prebuilt_pack_source();
    const auto directory = install::prebuilt_pack_directory(*source);

    // A download that ended: HD on and back to the settings, or the choice again with why.
    if (prebuilt_download_) {
        const auto progress = prebuilt_download_->progress();
        if (progress.step == Step::Done) {
            settings_.texture_pack = prebuilt_download_->destination();
            prebuilt_download_.reset();
            save();
            return_from_pack(SettingRow::Textures);
            return;
        }
        if (progress.step == Step::Failed || progress.step == Step::Cancelled) {
            texture_error_ = progress.step == Step::Failed ? format(Text::TexturesNotDownloaded, {progress.error}) : "";
            prebuilt_download_.reset();
            focus_control("textures_make");
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(inner.Min.x + pad, inner.Min.y + pad * 0.8f));
    begin_list("##textures", ImVec2(inner.GetWidth() - 2.0f * pad, inner.GetHeight() - 1.6f * pad));
    paragraph(layer_, tr(Text::TexturesPrebuiltIntro), width, colors::kInkDim);
    ImGui::Dummy(ImVec2(0, 8.0f * u));
    if (!directory) {
        paragraph(layer_, format(Text::TexturesNotDownloaded, {"there is no per-user data directory"}), width, colors::kRed);
    } else if (prebuilt_download_) {
        const auto progress = prebuilt_download_->progress();
        paragraph(layer_, progress.step == Step::Unpacking
                              ? std::string(tr(Text::TexturesUnpackingPack))
                              : format(Text::TexturesDownloading, {megabytes(progress.done), megabytes(progress.total)}),
                  width);
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        std::optional<float> fraction;
        if (progress.total != 0u && progress.step == Step::Downloading)
            fraction = static_cast<float>(progress.done) / static_cast<float>(progress.total);
        progress_bar(layer_, width, fraction);
        ImGui::Dummy(ImVec2(0, 8.0f * u));
        if (action_row(layer_, "textures_cancel", tr(Text::InstallCancel), "", width)) prebuilt_download_->cancel();
    } else {
        paragraph(layer_, format(Text::PackSource, {url_host(source->url), megabytes(source->bytes)}), width);
        ImGui::Dummy(ImVec2(0, 4.0f * u));
        if (action_row(layer_, "textures_make", tr(Text::TexturesDownload), "", width)) {
            texture_error_.clear();
            prebuilt_download_ = std::make_unique<install::PrebuiltPackDownload>(*source, std::string(identity_.disc_id),
                                                                                *directory);
            focus_control("textures_cancel");
        }
    }
    if (!texture_error_.empty()) {
        ImGui::Dummy(ImVec2(0, 6.0f * u));
        paragraph(layer_, texture_error_, width, colors::kRed);
    }
    end_list(layer_);

    // Back stops a download under way rather than leaving it running.
    if (back_button(layer_, ImVec2(18.0f * u, 18.0f * u)) || layer_.back()) {
        if (prebuilt_download_)
            prebuilt_download_->cancel();
        else
            return_from_pack(SettingRow::Textures);
    }
    hints(layer_, {{Control::Confirm, tr(Text::HintSelect)},
                   {Control::Back, tr(prebuilt_download_ ? Text::HintCancel : Text::HintBack)}});
}

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
            return_from_pack(SettingRow::GameText);
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
            return_from_pack(SettingRow::GameText);
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

namespace sarusaru::ui {
void Launcher::check_updates() {
#if !defined(__ANDROID__)
    if (update_check_ && update_check_->result().state == install::UpdateResult::State::Checking) return;
    update_check_ = std::make_unique<install::UpdateCheck>();
    settings_.last_update_check = std::time(nullptr);
    save();
#endif
}
void Launcher::updates(float width) {
    paragraph(layer_, format(Text::InstalledVersion, {release_version}), width);
#if defined(__ANDROID__)
    paragraph(layer_, tr(Text::UpdatesAndroid), width);
    if (action_row(layer_, "obtainium", tr(Text::UpdatesObtainium), "", width)) {
        if (!SDL_OpenURL(std::string(install::kObtainium).c_str()))
            SDL_OpenURL("https://obtainium.imranr.dev/");
    }
#else
    const auto result = update_check_ ? update_check_->result() : install::UpdateResult{};
    const bool checking = update_check_ && result.state == install::UpdateResult::State::Checking;
    if (action_row(layer_, "update_check", tr(checking ? Text::UpdatesChecking : Text::UpdatesCheck), "", width, !checking))
        check_updates();
    if (update_check_) {
        using State = install::UpdateResult::State;
        switch (result.state) {
        case State::Checking: break;
        case State::Available: paragraph(layer_, format(Text::UpdateAvailable, {result.version}), width, colors::kGood); break;
        case State::Current: paragraph(layer_, tr(Text::UpdatesCurrent), width); break;
        case State::Unsupported: paragraph(layer_, tr(Text::UpdatesUnsupported), width); break;
        case State::Failed: paragraph(layer_, tr(Text::UpdatesFailed), width, colors::kRed); break;
        }
    }
    if (std::getenv("FLATPAK_ID")) paragraph(layer_, tr(Text::UpdatesFlatpak), width);
    else paragraph(layer_, tr(Text::UpdatesDownloadHelp), width);
#endif
    if (action_row(layer_, "update_download", tr(Text::UpdatesDownloads), "", width))
        SDL_OpenURL(std::string(install::kReleasePage).c_str());
}
} // namespace sarusaru::ui
