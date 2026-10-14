#pragma once

// Draws the GE's decoded draw calls (gpu::GeState) with Vulkan.
//
// Every framebuffer address the game draws to gets a GPU render target at
// `scale` times the PSP's resolution, with its own depth buffer. Textures are
// decoded from guest memory and cached by content; a texture whose address is
// a render target samples that target instead (render to texture). Rendering
// is not written back to guest memory, so a game that reads its framebuffer
// with the CPU sees what was there before.
#include "display/display.hpp"
#include "gpu/ge_state.hpp"
#include "shell/vulkan_context.hpp"
#include "psprecomp/guest_memory.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace sarusaru::shell {

class GeRenderer {
public:
    GeRenderer(VulkanContext &vulkan, std::uint32_t scale) : vk_(vulkan), scale_(scale) {}
    bool create(std::string &error);
    void destroy();

    // Per frame: `begin_frame` with the frame's command buffer, draws, then
    // `presentable` for the image VulkanContext::end_frame shows.
    void begin_frame(VkCommandBuffer commands);
    void draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call);
    // Ends any render pass and returns the displayed framebuffer as an image in
    // TRANSFER_SRC layout (uploaded from guest memory when the GPU never drew
    // it), with its size in pixels; nullptr when nothing is displayed.
    const Image *presentable(const psprecomp::GuestMemory &memory, const display::Display::Framebuffer &framebuffer,
                             std::uint32_t &width, std::uint32_t &height);

    [[nodiscard]] std::uint64_t draws() const noexcept { return draws_; }
    // Prints every draw of the next `frames` vblanks to stderr (SARUSARU_TRACE_DRAWS).
    void trace_frames(std::uint32_t frames) noexcept { trace_ = frames; }

private:
    struct Target {
        std::uint32_t address{};
        Image color, depth;
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VkImageLayout color_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    };
    struct Texture {
        Image image;
        std::uint64_t last_frame{};
    };
    struct PipelineKey {
        std::uint32_t topology{}, blend{}, depth{}, raster{};
        bool operator<(const PipelineKey &other) const noexcept {
            return std::tie(topology, blend, depth, raster) < std::tie(other.topology, other.blend, other.depth, other.raster);
        }
    };
    struct GpuVertex {
        float position[4];
        float texcoord[2];
        std::uint32_t color;
        float normal[3];
    };

    VulkanContext &vk_;
    std::uint32_t scale_;
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    VkDescriptorSetLayout texture_layout_{VK_NULL_HANDLE}, uniform_layout_{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    VkShaderModule vertex_shader_{VK_NULL_HANDLE}, fragment_shader_{VK_NULL_HANDLE};
    std::map<PipelineKey, VkPipeline> pipelines_;
    std::map<std::uint32_t, VkSampler> samplers_;
    VkDescriptorPool persistent_pool_{VK_NULL_HANDLE}, frame_pool_{VK_NULL_HANDLE};
    VkDescriptorSet uniform_set_{VK_NULL_HANDLE};
    Buffer vertices_, uniforms_, staging_;
    VkDeviceSize vertex_cursor_{}, uniform_cursor_{}, staging_cursor_{};
    std::uint32_t frame_sets_{};
    Image white_;
    bool white_ready_{};
    Image upload_;  // the displayed framebuffer when the CPU drew it

    std::map<std::uint32_t, std::unique_ptr<Target>> targets_;
    std::unordered_map<std::uint64_t, Texture> textures_;
    VkCommandBuffer commands_{VK_NULL_HANDLE};
    Target *pass_target_{};
    std::uint64_t frame_{};
    std::uint64_t draws_{};
    std::uint32_t trace_{};

    [[nodiscard]] static std::uint32_t canonical(std::uint32_t address) noexcept;
    Target *target(std::uint32_t address);
    void begin_pass(Target &target);
    void end_pass();
    void flush();
    // Ensures `bytes` of room in a per-frame buffer, flushing the frame when full.
    VkDeviceSize reserve(Buffer &buffer, VkDeviceSize &cursor, VkDeviceSize bytes, VkDeviceSize alignment);
    VkPipeline pipeline(const PipelineKey &key);
    VkSampler sampler(const gpu::TextureState &texture);
    // The image view to sample for the draw's texture, uploading it if needed.
    VkImageView texture_view(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call, float &u_scale, float &v_scale);
    VkDescriptorSet texture_set(VkImageView view, VkSampler sampler);
    void upload(Image &image, const std::uint32_t *texels, std::uint32_t width, std::uint32_t height);
};

} // namespace sarusaru::shell
