#pragma once

// The shared machinery of the port's screens: Dear ImGui on the game's window,
// the theme and fonts scaled to the window, and one way of navigating for
// every device. Keyboard arrows, a gamepad's d-pad or left stick move the
// focus; Enter or the confirm button activates; Esc, the other face button,
// a mouse's back button or Android's Back go back. The mouse focuses what it
// hovers and clicks it; a finger taps, and dragging a list scrolls it.
#include "shell/vulkan_context.hpp"
#include "ui/ui_renderer.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sarusaru::ui {

// What the player last used, for the button hints and the focus highlight.
enum class Device { Keyboard, Mouse, Touch, Gamepad };

class Layer {
public:
    Layer(SDL_Window *window, shell::VulkanContext &vulkan);
    ~Layer();
    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    // False with `error` set when the interface cannot be shown.
    bool create(std::string &error);
    void destroy();

    void handle(const SDL_Event &event);
    // Starts a frame of the interface. `confirm_south` picks the gamepad's
    // confirm button (east otherwise).
    void begin_frame(bool confirm_south);
    // Draws and presents it, and saves it as a BMP if a capture was asked for.
    void present();
    // The next present() also saves the picture to `path` (for scripted checks).
    void capture_next(std::filesystem::path path) { capture_ = std::move(path); }
    // Runs at the start of every frame, after the window's input has been
    // read and before Dear ImGui sees it: where ui/ui_script.hpp adds its own.
    void set_frame_hook(std::function<void(Layer &)> hook) { frame_hook_ = std::move(hook); }
    void set_device(Device device) noexcept { device_ = device; }

    // One layout unit in points: 1 in the default 960x544 window, scaling
    // with the smaller of its two dimensions.
    [[nodiscard]] float unit() const noexcept { return unit_; }
    [[nodiscard]] Device device() const noexcept { return device_; }
    // The type of the gamepad last used, for its button labels.
    [[nodiscard]] SDL_GamepadType pad_type() const noexcept { return pad_type_; }
    [[nodiscard]] bool confirm_south() const noexcept { return confirm_south_; }
    // Seconds since the interface started, for animation.
    [[nodiscard]] float time() const noexcept { return time_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    // The player went back this frame.
    [[nodiscard]] bool back() const noexcept { return back_; }
    // A file dropped on the window, once.
    std::optional<std::filesystem::path> take_dropped_file();
    [[nodiscard]] SDL_Window *window() const noexcept { return window_; }
    UiRenderer &renderer() noexcept { return renderer_; }

private:
    SDL_Window *window_;
    shell::VulkanContext &vulkan_;
    UiRenderer renderer_;
    bool context_{};
    bool sdl_backend_{};
    float unit_{1.0f};
    float style_unit_{};
    Device device_{Device::Keyboard};
    SDL_GamepadType pad_type_{SDL_GAMEPAD_TYPE_STANDARD};
    bool confirm_south_{};
    bool closed_{};
    bool back_{};
    float time_{};
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
    std::optional<std::filesystem::path> dropped_;
    std::optional<std::filesystem::path> capture_;
    std::function<void(Layer &)> frame_hook_;
    shell::Buffer capture_buffer_;
};

} // namespace sarusaru::ui
