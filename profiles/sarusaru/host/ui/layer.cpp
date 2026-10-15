#include "ui/layer.hpp"

#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace sarusaru::ui {
namespace {

// A stick pushed this far (of 32767) counts as using the gamepad.
constexpr int kStickActivity = 16000;
// Mouse movement of fewer points is jitter, not a switch to the mouse.
constexpr float kMouseActivity = 3.0f;

// Anything on any of `pads` held: a button, or a stick or trigger pushed.
bool pad_input_held(const std::vector<SDL_Gamepad *> &pads) {
    for (SDL_Gamepad *pad : pads) {
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button))) return true;
        for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
            if (std::abs(static_cast<int>(SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis)))) > 9000) return true;
    }
    return false;
}

void open_pad(std::vector<SDL_Gamepad *> &pads, SDL_JoystickID id) {
    for (SDL_Gamepad *pad : pads)
        if (SDL_GetGamepadID(pad) == id) return;
    if (SDL_Gamepad *pad = SDL_OpenGamepad(id)) pads.push_back(pad);
}

} // namespace

Layer::Layer(SDL_Window *window, shell::VulkanContext &vulkan) : window_(window), vulkan_(vulkan), renderer_(vulkan) {}

Layer::~Layer() { destroy(); }

bool Layer::create(std::string &error) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    context_ = true;
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    // Keep the focused item highlighted with a gamepad or keyboard, and do not
    // drop the focus on Esc: Esc means Back here.
    io.ConfigNavCursorVisibleAlways = true;
    io.ConfigNavEscapeClearFocusItem = false;
    io.ConfigNavEscapeClearFocusWindow = false;
    // The west face button clears a binding; held, it would otherwise open
    // Dear ImGui's window switcher.
    GImGui->ConfigNavWindowingWithGamepad = false;
    if (!ImGui_ImplSDL3_InitForVulkan(window_)) {
        error = "cannot attach the interface to the window";
        return false;
    }
    sdl_backend_ = true;
    // Any connected pad drives the interface.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll);
    if (!load_fonts()) std::cerr << "sarusaru: some interface fonts are missing; using fallbacks\n";
    if (!renderer_.create(error)) return false;
    int count = 0;
    if (SDL_JoystickID *pads = SDL_GetGamepads(&count); pads != nullptr) {
        if (count > 0) {
            device_ = Device::Gamepad;
            pad_type_ = SDL_GetGamepadTypeForID(pads[0]);
        }
        for (int i = 0; i < count; ++i) open_pad(pads_, pads[i]);
        SDL_free(pads);
    }
    return true;
}

void Layer::destroy() {
    for (SDL_Gamepad *pad : pads_) SDL_CloseGamepad(pad);
    pads_.clear();
    vulkan_.destroy_buffer(capture_buffer_);
    renderer_.destroy();
    if (sdl_backend_) ImGui_ImplSDL3_Shutdown();
    sdl_backend_ = false;
    if (context_) ImGui::DestroyContext();
    context_ = false;
}

void Layer::handle(const SDL_Event &event) {
    if (observe(event)) return;
    if (event_filter_ && event_filter_(event)) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool Layer::observe(const SDL_Event &event) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        closed_ = true;
        break;
    case SDL_EVENT_KEY_DOWN:
        device_ = Device::Keyboard;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which == SDL_TOUCH_MOUSEID)
            device_ = Device::Touch;
        else if (std::abs(event.motion.xrel) + std::abs(event.motion.yrel) >= kMouseActivity)
            device_ = Device::Mouse;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        device_ = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? event.button.which : event.wheel.which) ==
                SDL_TOUCH_MOUSEID ? Device::Touch : Device::Mouse;
        break;
    case SDL_EVENT_FINGER_DOWN:
        device_ = Device::Touch;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        device_ = Device::Gamepad;
        pad_type_ = SDL_GetGamepadTypeForID(event.gbutton.which);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(static_cast<int>(event.gaxis.value)) > kStickActivity) {
            device_ = Device::Gamepad;
            pad_type_ = SDL_GetGamepadTypeForID(event.gaxis.which);
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        open_pad(pads_, event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (auto it = pads_.begin(); it != pads_.end(); ++it)
            if (SDL_GetGamepadID(*it) == event.gdevice.which) {
                SDL_CloseGamepad(*it);
                pads_.erase(it);
                break;
            }
        break;
    case SDL_EVENT_DROP_FILE:
        if (event.drop.data != nullptr) {
            const std::string utf8 = event.drop.data;
            dropped_ = std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }
        return true;
    default:
        break;
    }
    return false;
}

void Layer::block_pads(bool blocked) {
    if (blocked == pads_blocked_) return;
    pads_blocked_ = blocked;
    if (blocked) {
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
        // Nothing held as the block started stays held in the interface.
        ImGui::GetIO().ClearInputKeys();
    } else {
        pads_quiet_ = true;  // begin_frame() lets them back once let go of
    }
}

std::array<float, SDL_GAMEPAD_AXIS_COUNT> Layer::pad_axes() const {
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> axes{};
    for (SDL_Gamepad *pad : pads_)
        for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
            const float value = static_cast<float>(SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis))) / 32767.0f;
            if (std::abs(value) > std::abs(axes[static_cast<std::size_t>(axis)])) axes[static_cast<std::size_t>(axis)] = value;
        }
    return axes;
}

std::optional<std::filesystem::path> Layer::take_dropped_file() {
    auto dropped = std::move(dropped_);
    dropped_.reset();
    return dropped;
}

void Layer::reset_input() {
    ImGuiIO &io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
}

void Layer::update_time() {
    time_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
}

void Layer::begin_frame(bool confirm_south) {
    confirm_south_ = confirm_south;
    update_time();
    if (pads_quiet_ && !pads_blocked_ && !pad_input_held(pads_)) {
        pads_quiet_ = false;
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll);
    }
    ImGui_ImplSDL3_NewFrame();
    if (frame_hook_) frame_hook_(*this);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigNavSwapGamepadButtons = !confirm_south;
    // Everything is laid out in units of the default window (960x544 points).
    unit_ = std::clamp(std::min(io.DisplaySize.x / 960.0f, io.DisplaySize.y / 544.0f), 0.5f, 4.0f);
#if defined(__ANDROID__)
    // A phone's few points would make rows too small to tap.
    unit_ = std::max(unit_, 0.8f);
#endif
    // Restyle only when the size changes noticeably.
    if (std::abs(unit_ - style_unit_) > 0.01f) {
        apply_style(unit_);
        style_unit_ = unit_;
    }
    // The pointer is hidden while a gamepad or the keyboard is in use.
    io.ConfigFlags = (device_ == Device::Gamepad || device_ == Device::Keyboard)
        ? io.ConfigFlags | ImGuiConfigFlags_NoMouseCursorChange : io.ConfigFlags & ~ImGuiConfigFlags_NoMouseCursorChange;
    if (device_ == Device::Gamepad || device_ == Device::Keyboard) SDL_HideCursor(); else SDL_ShowCursor();
    ImGui::NewFrame();
    // The west face button clears a binding. Owned here, Dear ImGui does not
    // also take a tap of it as "switch to the menu layer", which drops the focus.
    ImGui::SetKeyOwner(ImGuiKey_GamepadFaceLeft, ImHashStr("sarusaru-west"));
    const ImGuiKey pad_back = confirm_south ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
    back_ = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_AppBack, false) ||
            ImGui::IsKeyPressed(pad_back, false) || ImGui::IsMouseClicked(3, false);
}

void Layer::present_over(const shell::Image *image, std::uint32_t width, std::uint32_t height,
                         const std::function<void(ImVec2 origin, ImVec2 size)> &draw) {
    if (image == nullptr || width == 0u || height == 0u) return;
    renderer_.set_backdrop(image, width, height);
    update_time();
    ImGuiIO &io = ImGui::GetIO();
    // The game owns the pointer meanwhile: Dear ImGui leaves it as it is.
    const ImGuiConfigFlags flags = io.ConfigFlags;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // Letterboxed as the game is shown.
    const float scale = std::min(io.DisplaySize.x / static_cast<float>(width), io.DisplaySize.y / static_cast<float>(height));
    const ImVec2 size{static_cast<float>(width) * scale, static_cast<float>(height) * scale};
    draw({(io.DisplaySize.x - size.x) / 2.0f, (io.DisplaySize.y - size.y) / 2.0f}, size);
    present();
    io.ConfigFlags = flags;
    renderer_.set_backdrop(nullptr, 0u, 0u);
}

void Layer::present() {
    ImGui::Render();
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    const VkCommandBuffer commands = vulkan_.begin_frame();
    const auto *image = renderer_.render(commands, ImGui::GetDrawData(), static_cast<std::uint32_t>(width),
                                         static_cast<std::uint32_t>(height));
    bool capturing = capture_ && image != nullptr;
    const auto bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    if (capturing && capture_buffer_.size < bytes) {
        vulkan_.destroy_buffer(capture_buffer_);
        capturing = vulkan_.create_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, capture_buffer_);
    }
    if (capturing) vulkan_.copy_to_buffer(*image, image->width, image->height, capture_buffer_);
    vulkan_.end_frame(image, image != nullptr ? image->width : 0u, image != nullptr ? image->height : 0u);
    if (!capturing) return;
    vulkan_.vkQueueWaitIdle(vulkan_.queue);
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ABGR8888, capture_buffer_.mapped,
                                                 width * 4);
    if (surface != nullptr) {
        const auto utf8 = capture_->u8string();
        if (!SDL_SaveBMP(surface, std::string(utf8.begin(), utf8.end()).c_str()))
            std::cerr << "sarusaru: cannot save " << capture_->string() << ": " << SDL_GetError() << '\n';
        SDL_DestroySurface(surface);
    }
    capture_.reset();
}

} // namespace sarusaru::ui
