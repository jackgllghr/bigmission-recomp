#pragma once

#include "gpu/ge_state.hpp"
#include "shell/settings.hpp"
#include "ui/bindings.hpp"

#include <span>

namespace sarusaru::shell {
// Source-verified atlas patches, applied after translation and HD textures.
// Only button regions change; UVs, captions and guest memory stay intact.
class ButtonPrompts {
public:
    ButtonPrompts();
    void configure(const ControlSettings &controls, ui::BindingDevice device, SDL_GamepadType type);
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    // Identify before translation changes the source pixels. Zero is no match.
    [[nodiscard]] static unsigned identify(const gpu::TextureState &texture, std::span<const std::uint32_t> pixels);
    void apply(unsigned atlas, std::vector<std::uint32_t> &pixels, unsigned width, unsigned scale) const;
private:
    ControlSettings controls_;
    ui::BindingDevice device_{ui::BindingDevice::Keyboard};
    SDL_GamepadType type_{SDL_GAMEPAD_TYPE_STANDARD};
    std::uint64_t revision_{};
    std::vector<unsigned char> font_;
    void draw(std::vector<std::uint32_t> &pixels, unsigned width, unsigned scale,
              int x, int y, int w, int h, Action action) const;
};
} // namespace sarusaru::shell
