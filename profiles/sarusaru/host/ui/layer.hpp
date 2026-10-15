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

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

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
    // Only notes the device in use, a gamepad coming or going, a dropped
    // file or the window closing: for events the game takes while it runs,
    // which Dear ImGui would otherwise queue until the interface's next frame.
    // True when the event is the layer's alone (a dropped file).
    bool observe(const SDL_Event &event);
    // Starts a frame of the interface. `confirm_south` picks the gamepad's
    // confirm button (east otherwise).
    void begin_frame(bool confirm_south);
    // Draws and presents it, and saves it as a BMP if a capture was asked for.
    void present();
    // Draws the interface over the game's picture (UiRenderer::set_backdrop);
    // null for the launcher's own sky.
    void set_backdrop(const shell::Image *image, std::uint32_t width, std::uint32_t height) {
        renderer_.set_backdrop(image, width, height);
    }
    // Forgets the keys and buttons Dear ImGui last saw: the interface does
    // not see the window's input while the game has it, so a key let go
    // meanwhile would otherwise stay held when it comes back.
    void reset_input();
    // Moves time() on without a frame of the interface, for a UI script
    // ticked while the game runs.
    void update_time();
    // The next present() also saves the picture to `path` (for scripted checks).
    void capture_next(std::filesystem::path path) { capture_ = std::move(path); }
    // Runs at the start of every frame, after the window's input has been
    // read and before Dear ImGui sees it: where ui/ui_script.hpp adds its own.
    void set_frame_hook(std::function<void(Layer &)> hook) { frame_hook_ = std::move(hook); }
    void set_device(Device device) noexcept { device_ = device; }
    // Sees every window event before Dear ImGui does; returning true keeps it
    // from the interface (binding capture takes the next input this way).
    void set_event_filter(std::function<bool(const SDL_Event &)> filter) { event_filter_ = std::move(filter); }
    // While blocked, no gamepad reaches the interface, and afterwards only
    // once nothing on any pad is held, so the input just captured for a
    // binding does nothing in the menus.
    void block_pads(bool blocked);
    // Each axis's value (-1..1) on the pad pushing it furthest.
    [[nodiscard]] std::array<float, SDL_GAMEPAD_AXIS_COUNT> pad_axes() const;

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
    std::function<bool(const SDL_Event &)> event_filter_;
    // The layer's own handles: Dear ImGui's backend closes its own while
    // pads are blocked, and SDL sends no events for a pad nobody has open.
    std::vector<SDL_Gamepad *> pads_;
    bool pads_blocked_{};
    bool pads_quiet_{};
    shell::Buffer capture_buffer_;
};

} // namespace sarusaru::ui
