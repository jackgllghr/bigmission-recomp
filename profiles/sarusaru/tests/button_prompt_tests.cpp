#include "shell/button_prompts.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace sarusaru;
void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    shell::ButtonPrompts prompts;
    auto controls = shell::default_settings().controls;
    const auto render = [&](ui::BindingDevice device, SDL_GamepadType type) {
        prompts.configure(controls, device, type);
        std::vector<std::uint32_t> pixels(128*32*16, 0x12345678);
        prompts.apply(1, pixels, 128*4, 4);
        return pixels;
    };
    const auto keyboard = render(ui::BindingDevice::Keyboard, SDL_GAMEPAD_TYPE_STANDARD);
    const auto xbox = render(ui::BindingDevice::Gamepad, SDL_GAMEPAD_TYPE_XBOXONE);
    const auto nintendo = render(ui::BindingDevice::Gamepad, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO);
    const auto ps = render(ui::BindingDevice::Gamepad, SDL_GAMEPAD_TYPE_PS5);
    require(keyboard != xbox && xbox != nintendo && ps != xbox, "device glyphs did not change");
    for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 512; ++x)
        if (x >= 384 || y >= 96) require(xbox[y*512+x] == 0x12345678, "patch escaped its atlas region");
    const shell::Binding chord{shell::Binding::Kind::Button, SDL_GAMEPAD_BUTTON_SOUTH, 1, SDL_GAMEPAD_BUTTON_EAST};
    require(ui::glyph(chord, SDL_GAMEPAD_TYPE_XBOXONE).label == "A+B" &&
            ui::glyph(chord, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO).label == "B+A" &&
            ui::glyph(chord, SDL_GAMEPAD_TYPE_PS5).label == "×+○", "chord ignored controller family");
    const auto revision = prompts.revision();
    render(ui::BindingDevice::Gamepad, SDL_GAMEPAD_TYPE_PS5);
    require(prompts.revision() == revision, "unchanged input invalidates textures");
    controls.gamepad[static_cast<unsigned>(shell::Action::Circle)] = {{shell::Binding::Kind::Axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1}};
    require(render(ui::BindingDevice::Gamepad, SDL_GAMEPAD_TYPE_PS5) != ps, "rebound trigger ignored");
    controls.gamepad[static_cast<unsigned>(shell::Action::Circle)].clear();
    require(render(ui::BindingDevice::Gamepad, SDL_GAMEPAD_TYPE_PS5) != ps, "unbound action retained PSP glyph");
    gpu::TextureState texture; texture.width = texture.buffer_width = 128; texture.height = 128;
    require(shell::ButtonPrompts::identify(texture, {}) == 0, "short pixels accepted");
    std::vector<std::uint32_t> unrelated(128*128);
    require(shell::ButtonPrompts::identify(texture, unrelated) == 0, "unrelated same-sized texture accepted");
    std::vector<std::uint32_t> preview(512*128*4);
    const std::vector<std::uint32_t> *strips[] = {&keyboard, &xbox, &nintendo, &ps};
    for (int i = 0; i < 4; ++i) std::copy(strips[i]->begin(), strips[i]->end(), preview.begin()+i*512*128);
    auto *surface = SDL_CreateSurfaceFrom(512, 512, SDL_PIXELFORMAT_ABGR8888, preview.data(), 512*4);
    if (const char *path = std::getenv("SARUSARU_PROMPT_PREVIEW")) {
        require(surface && SDL_SaveBMP(surface, path), "could not write prompt preview");
    }
    SDL_DestroySurface(surface);
}
