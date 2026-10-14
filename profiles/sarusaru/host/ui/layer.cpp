#include "ui/layer.hpp"

#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

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
        SDL_free(pads);
    }
    return true;
}

void Layer::destroy() {
    vulkan_.destroy_buffer(capture_buffer_);
    renderer_.destroy();
    if (sdl_backend_) ImGui_ImplSDL3_Shutdown();
    sdl_backend_ = false;
    if (context_) ImGui::DestroyContext();
    context_ = false;
}

void Layer::handle(const SDL_Event &event) {
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
    case SDL_EVENT_DROP_FILE:
        if (event.drop.data != nullptr) {
            const std::string utf8 = event.drop.data;
            dropped_ = std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }
        return;
    default:
        break;
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
}

std::optional<std::filesystem::path> Layer::take_dropped_file() {
    auto dropped = std::move(dropped_);
    dropped_.reset();
    return dropped;
}

void Layer::begin_frame(bool confirm_south) {
    confirm_south_ = confirm_south;
    time_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
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
    const ImGuiKey pad_back = confirm_south ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
    back_ = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_AppBack, false) ||
            ImGui::IsKeyPressed(pad_back, false) || ImGui::IsMouseClicked(3, false);
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
