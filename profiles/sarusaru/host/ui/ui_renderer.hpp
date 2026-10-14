#pragma once

// Draws Dear ImGui's frames with the shell's Vulkan context, into an image the
// size of the window that VulkanContext::end_frame then presents like a game
// frame (and that SARUSARU_CAPTURE can save).
#include "shell/vulkan_context.hpp"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sarusaru::ui {

class UiRenderer {
public:
    explicit UiRenderer(shell::VulkanContext &vulkan) : vulkan_(vulkan) {}
    ~UiRenderer() { destroy(); }
    UiRenderer(const UiRenderer &) = delete;
    UiRenderer &operator=(const UiRenderer &) = delete;

    // Needs the ImGui context. False with `error` set on failure.
    bool create(std::string &error);
    void destroy();

    // Records `data` into `commands`, drawn over a clear to `clear` at
    // width x height pixels. Returns the image to present (in TRANSFER_SRC
    // layout), or null when it cannot be drawn at that size.
    const shell::Image *render(VkCommandBuffer commands, ImDrawData *data, std::uint32_t width, std::uint32_t height);

    // An RGBA8 picture for ImGui::Image, uploaded with the next render().
    // Lives until destroy(). Null on failure.
    ImTextureID add_texture(const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height);

private:
    struct Texture {
        shell::Image image;
        shell::Buffer staging;
        VkDescriptorSet descriptor{VK_NULL_HANDLE};
        bool uploaded{};
    };
    shell::VulkanContext &vulkan_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    shell::Image target_;
    VkFramebuffer framebuffer_{VK_NULL_HANDLE};
    std::vector<Texture> textures_;
    bool backend_{};

    bool resize(std::uint32_t width, std::uint32_t height);
    void destroy_target();
};

} // namespace sarusaru::ui
