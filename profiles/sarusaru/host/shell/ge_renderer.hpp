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
#include "save_state/slots.hpp"
#include "shell/button_prompts.hpp"
#include "gpu/ge_state.hpp"
#include "shell/vulkan_context.hpp"
#include "textures/hd_pack.hpp"
#include "textures/texture_dump.hpp"
#include "translation/texture_pack.hpp"
#include "psprecomp/guest_memory.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sarusaru::shell {

class GeRenderer {
public:
    GeRenderer(VulkanContext &vulkan, std::uint32_t scale) : vk_(vulkan), scale_(scale) {}
    // Set before drawing; applies equally to native and interpolated frames.
    void set_cel_shading(std::uint32_t preset) noexcept { cel_shading_ = preset; }
    bool create(std::string &error);
    void set_button_prompts(ButtonPrompts *prompts) { button_prompts_ = prompts; }
    void destroy();
    // Set once before the first draw; the session keeps the pack alive.
    void set_translation_pack(const translation::TexturePack *pack) noexcept { translation_pack_ = pack; }
    // Also set once before the first draw, and kept alive by the caller:
    // decoded textures are replaced from `pack` and/or written to `dump`
    // (after any translation), by their gpu::TextureIdentity.
    void set_hd_textures(const textures::HdTexturePack *pack) noexcept { hd_pack_ = pack; }
    void set_texture_dump(textures::TextureDump *dump) noexcept { texture_dump_ = dump; }

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

    // Lossless native render-target snapshot, including depth. Calls require
    // an active command buffer and no guest frames running.
    [[nodiscard]] save_state::Thumbnail thumbnail(const psprecomp::GuestMemory &, const display::Display::Framebuffer &);
    void finish_commands() { flush(); }
    [[nodiscard]] std::vector<std::uint8_t> save_state();
    void load_state(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::uint64_t draws() const noexcept { return draws_; }
    // Prints every draw of the next `frames` vblanks to stderr (SARUSARU_TRACE_DRAWS).
    void trace_frames(std::uint32_t frames) noexcept { trace_ = frames; }

private:
    ButtonPrompts *button_prompts_{};
    std::unordered_map<std::uint64_t, unsigned> prompt_atlases_;
    std::unordered_set<VkImageView> action_hint_views_;
    const translation::TexturePack *translation_pack_{};
    const textures::HdTexturePack *hd_pack_{};
    textures::TextureDump *texture_dump_{};
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
        // stencil: bit 0 the test, 1-3 its compare op, 4-6 the fail op, 7-9
        // the pass op, 10-12 the depth-fail op, 13-20 the compare mask, 21-28
        // the write mask (the reference is dynamic).
        std::uint32_t topology{}, blend{}, depth{}, raster{}, stencil{};
        bool operator<(const PipelineKey &other) const noexcept {
            return std::tie(topology, blend, depth, raster, stencil) <
                   std::tie(other.topology, other.blend, other.depth, other.raster, other.stencil);
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

    struct WorldContours {
        std::vector<GpuVertex> vertices;
        std::vector<std::array<float, 3>> positions;
        std::vector<float> facing;
        PipelineKey key;
        VkViewport viewport{};
        VkRect2D scissor{};
    } world_contours_;

    VulkanContext &vk_;
    std::uint32_t scale_;
    std::uint32_t cel_shading_{};
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
    // This frame's texture descriptor sets, by image view and sampler: a
    // texture drawn again reuses its set, so the in-between picture of 60 FPS
    // motion does not run the pool dry (which forced a stalling flush).
    std::unordered_map<std::uint64_t, VkDescriptorSet> frame_set_cache_;
    // The texture the latest game draw sampled, for queue_interpolated to
    // reuse instead of hashing the texture a second time.
    struct LastTexture {
        bool valid{};
        VkImageView view{VK_NULL_HANDLE};
        float u_scale{1.0f}, v_scale{1.0f};
    } last_game_texture_;
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
    void draw_world_contours();
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
