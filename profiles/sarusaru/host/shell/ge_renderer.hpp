#pragma once

// Draws the GE's decoded draw calls (gpu::GeState) with Vulkan.
//
// Every framebuffer address the game draws to gets a GPU render target at
// `scale` times the PSP's resolution, with its own depth buffer. Textures are
// decoded from guest memory and cached by content; a texture whose address is
// a render target samples that target instead (render to texture). Rendering
// is not written back to guest memory, so a game that reads its framebuffer
// with the CPU sees what was there before.
//
// For frame interpolation a second, independent set of render targets is
// drawn alongside: the in-between picture (gpu::FrameInterpolator), from draws
// queued as the game makes them and replayed at the next vblank. Textures are
// resolved when a draw is queued, so later guest-memory changes cannot leak
// into it; render-to-texture samples the in-between targets.
#include "display/display.hpp"
#include "gpu/ge_state.hpp"
#include "shell/vulkan_context.hpp"
#include "translation/texture_pack.hpp"
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
    // Set once before the first draw; the session keeps the pack alive.
    void set_translation_pack(const translation::TexturePack *pack) noexcept { translation_pack_ = pack; }

    // Per frame: `begin_frame` with the frame's command buffer, draws, then
    // `presentable` for the image VulkanContext::end_frame shows.
    void begin_frame(VkCommandBuffer commands);
    void draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call);
    // Queues a draw of the in-between picture, to be drawn by draw_interpolated.
    void queue_interpolated(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call);
    // Draws the queued in-between draws into their own targets.
    void draw_interpolated();
    // Ends any render pass and returns the displayed framebuffer as an image in
    // TRANSFER_SRC layout (uploaded from guest memory when the GPU never drew
    // it), with its size in pixels; nullptr when nothing is displayed.
    // `interpolated` picks the in-between picture, when one was drawn there.
    const Image *presentable(const psprecomp::GuestMemory &memory, const display::Display::Framebuffer &framebuffer,
                             std::uint32_t &width, std::uint32_t &height, bool interpolated = false);

    [[nodiscard]] std::uint64_t draws() const noexcept { return draws_; }
    // Prints every draw of the next `frames` vblanks to stderr (SARUSARU_TRACE_DRAWS).
    void trace_frames(std::uint32_t frames) noexcept { trace_ = frames; }

private:
    const translation::TexturePack *translation_pack_{};
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
    std::unordered_map<std::uint32_t, Texture> host_textures_;
    struct PipelineKey {
        std::uint32_t topology{}, blend{}, depth{}, raster{};
        bool operator<(const PipelineKey &other) const noexcept {
            return std::tie(topology, blend, depth, raster) < std::tie(other.topology, other.blend, other.depth, other.raster);
        }
    };
    // A texture as resolved when an in-between draw was queued: a cached
    // image, or a render target to sample from the in-between targets.
    struct QueuedTexture {
        VkImageView view{VK_NULL_HANDLE};
        float u_scale{1.0f}, v_scale{1.0f};
        bool render_target{};
    };
    struct QueuedDraw {
        gpu::DrawCall call;
        QueuedTexture texture;
    };
    enum Layer : std::size_t { kGame = 0u, kInterpolated = 1u, kLayers = 2u };
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

    std::array<std::map<std::uint32_t, std::unique_ptr<Target>>, kLayers> targets_;
    std::vector<QueuedDraw> queued_;
    std::size_t queued_count_{};
    std::unordered_map<std::uint64_t, Texture> textures_;
    VkCommandBuffer commands_{VK_NULL_HANDLE};
    Target *pass_target_{};
    std::uint64_t frame_{};
    std::uint64_t draws_{};
    std::uint32_t trace_{};

    [[nodiscard]] static std::uint32_t canonical(std::uint32_t address) noexcept;
    Target *target(std::size_t layer, std::uint32_t address);
    // Readies `source` for sampling and returns its view, with the scale that
    // maps the texture's size onto the target.
    VkImageView sample_target(Target &source, const gpu::TextureState &texture, float &u_scale, float &v_scale);
    // Draws `call` into the layer's targets. A queued texture replaces the
    // lookup in guest memory, which `memory` is then not needed for.
    void draw_into(std::size_t layer, const psprecomp::GuestMemory *memory, const gpu::DrawCall &call,
                   const QueuedTexture *queued);
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
