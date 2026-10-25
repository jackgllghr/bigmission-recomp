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
#include "shell/worker_pool.hpp"
#include "textures/hd_pack.hpp"
#include "textures/texture_dump.hpp"
#include "translation/texture_pack.hpp"
#include "psprecomp/guest_memory.hpp"

#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <set>
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
    // Also before create(): where the pipeline cache and the pipeline states
    // used so far are kept between runs (shell/pipeline_store.hpp). The
    // states recorded there are built on a worker thread from create() on.
    void set_pipeline_directory(std::filesystem::path directory) { pipeline_directory_ = std::move(directory); }

    // Per frame: `begin_frame` with the frame's command buffer, draws, then
    // `presentable` for the image VulkanContext::end_frame shows.
    void begin_frame(VkCommandBuffer commands);
    void draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call);
    // At most this many in-between pictures between two game frames.
    static constexpr std::size_t kMaxPictures = 3u;
    // Queues a draw of each in-between picture (`calls[i]` for picture i + 1),
    // to be drawn by draw_interpolated.
    void queue_interpolated(const psprecomp::GuestMemory &memory, std::span<const gpu::DrawCall> calls);
    // Draws the queued in-between draws into their own targets.
    void draw_interpolated();
    // Ends any render pass and returns the displayed framebuffer as an image in
    // TRANSFER_SRC layout (uploaded from guest memory when the GPU never drew
    // it), with its size in pixels; nullptr when nothing is displayed.
    // `picture` 1 to kMaxPictures picks that in-between picture, when one
    // was drawn there; 0 is the game's own.
    const Image *presentable(const psprecomp::GuestMemory &memory, const display::Display::Framebuffer &framebuffer,
                             std::uint32_t &width, std::uint32_t &height, std::size_t picture = 0u);

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
    std::unordered_set<VkImageView> completion_number_views_;
    const translation::TexturePack *translation_pack_{};
    const textures::HdTexturePack *hd_pack_{};
    textures::TextureDump *texture_dump_{};
    struct Target {
        std::uint32_t address{};
        Image color, depth;
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        VkImageLayout color_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    };
    // A texture's HD image and translation lettering, made on a worker
    // thread from its decoded texels (hashes included). The worker writes
    // only the outputs, and only before `done`; the renderer reads them only
    // after.
    struct TextureJob {
        gpu::TextureState texture;
        std::vector<std::uint32_t> texels;  // as decoded
        bool look_hd{};                     // the HD pack has images of its size
        bool wait{};                        // translated: worth waiting for
        unsigned prompt_atlas{};            // button prompts lettered over the art, on the renderer's thread
        std::uint64_t queued_ns{}, started_ns{};
        // Outputs: `art`, width x height, or nothing (no HD image, no translation).
        gpu::TextureIdentity identity;
        std::vector<std::uint32_t> art;
        std::uint32_t width{}, height{};
        std::atomic<bool> done{};
    };
    // Wakes the renderer waiting for jobs at the end of a frame.
    struct JobSignal {
        std::mutex mutex;
        std::condition_variable finished;
    };
    struct Texture {
        Image image;
        std::uint64_t last_frame{};
        // Until its art is in the image. Draws may use the image meanwhile:
        // nothing recorded with it is submitted before resolve_art() has
        // filled it, with the art or, when that is late, the decoded texels.
        std::shared_ptr<TextureJob> job;
        bool filled{};
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
    // The game's own targets, then those of each in-between picture.
    enum Layer : std::size_t { kGame = 0u, kInterpolated = 1u, kLayers = kInterpolated + kMaxPictures };
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
    VkPipelineCache pipeline_cache_{VK_NULL_HANDLE};
    std::filesystem::path pipeline_directory_;
    // Every state built, saved for the next run's warm-up when changed.
    std::set<PipelineKey> known_keys_;
    bool keys_dirty_{};
    std::uint64_t keys_saved_frame_{};
    // Built by the warm-up on a worker; pipeline() takes them from here.
    std::mutex warm_mutex_;
    std::map<PipelineKey, VkPipeline> warmed_;
    std::atomic<bool> stop_warming_{};
    std::map<std::uint32_t, VkSampler> samplers_;
    // What the CPU writes for a submission, one set per VulkanContext slot;
    // the members below name the current slot's.
    struct SlotResources {
        Buffer vertices, uniforms, staging;
        VkDescriptorPool frame_pool{VK_NULL_HANDLE};
        VkDescriptorSet uniform_set{VK_NULL_HANDLE};
    };
    std::array<SlotResources, VulkanContext::kFramesInFlight> slots_{};
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
    std::array<std::vector<QueuedDraw>, kMaxPictures> queued_;
    std::array<std::size_t, kMaxPictures> queued_count_{};
    std::unordered_map<std::uint64_t, Texture> textures_;
    VkCommandBuffer commands_{VK_NULL_HANDLE};
    Target *pass_target_{};
    std::unique_ptr<WorkerPool> workers_;  // made on first use
    // Keys of textures waiting for their art, and what wakes the wait.
    std::vector<std::uint64_t> pending_;
    std::shared_ptr<JobSignal> job_signal_ = std::make_shared<JobSignal>();
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
    // Submits what is recorded and waits for it (to read back GPU output).
    void flush();
    // Submits what is recorded without waiting, continuing in the next slot:
    // for when this slot's buffers or descriptor sets run out.
    void next_slot();
    // Makes the context's current slot's resources the ones written, empty.
    void use_slot();
    // Ensures `bytes` of room in a per-slot buffer, moving to the next slot when full.
    VkDeviceSize reserve(Buffer &buffer, VkDeviceSize &cursor, VkDeviceSize bytes, VkDeviceSize alignment);
    VkPipeline pipeline(const PipelineKey &key);
    // Safe on any thread once create() has run.
    VkPipeline create_pipeline(const PipelineKey &key) const;
    void load_pipelines();
    // Writes the cache and the known states, on a worker unless `now`.
    void save_pipelines(bool now);
    WorkerPool &workers();
    VkSampler sampler(const gpu::TextureState &texture);
    // The image view to sample for the draw's texture, uploading it if needed.
    VkImageView texture_view(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call, float &u_scale, float &v_scale);
    VkDescriptorSet texture_set(VkImageView view, VkSampler sampler);
    // Makes `job.art`, on a worker thread.
    static void build_art(TextureJob &job, const textures::HdTexturePack *hd_pack,
                          const translation::TexturePack *translation_pack, std::uint32_t scale);
    // Puts finished art into the images waiting for it, ahead of the draws
    // recorded with them. Before anything is submitted. When `wait`, first
    // waits a little for this frame's unfinished translation jobs
    // (kArtWait*); images still without art get their decoded texels,
    // enlarged, meanwhile.
    void resolve_art(bool wait);
    // Uploads into `image` (its whole size) in the slot's prologue.
    void upload_before(Image &image, std::span<const std::uint32_t> texels);
    // Fills `image` in the prologue with `texels` (width x height) enlarged.
    void fill_before(Image &image, std::span<const std::uint32_t> texels, std::uint32_t width, std::uint32_t height);
    // Stages `bytes` for a copy recorded now: in the slot's staging buffer,
    // or a buffer of its own retired with the slot. Never moves on a slot.
    VkBuffer stage(const void *data, VkDeviceSize bytes, VkDeviceSize &offset);
    // Letters `prompt_atlas`'s button prompts into `texels` (width x height,
    // a multiple of the GE's `texture_width`) at `prompt_scale` times the
    // texture's size, enlarging the art to that first.
    void letter_prompts(std::vector<std::uint32_t> &texels, std::uint32_t &width, std::uint32_t &height,
                        std::uint32_t texture_width, unsigned prompt_atlas, unsigned prompt_scale);
    // The scale button prompts are lettered at over art of `art_scale`.
    [[nodiscard]] unsigned prompt_scale(unsigned art_scale) const noexcept { return std::max({4u, scale_, art_scale}); }
    // Puts a finished (or, when `late`, unfinished) job's result in its
    // image. Art of another size than the image (made at the lettering's
    // size, or the texture's own while HD art may still come) is fitted into
    // it for this frame, and a finished job's texture then gets an image of
    // the art's own size.
    void fill_art(Texture &entry, TextureJob &job, bool late);
    // Letters the button prompts, then creates and uploads the entry's image.
    bool make_image(Texture &entry, std::vector<std::uint32_t> &texels, std::uint32_t &width, std::uint32_t &height,
                    std::uint32_t texture_width, unsigned prompt_atlas);
    // How long a frame's end may wait for art, and how long art may take
    // from being queued before its texture is first shown without it.
    static constexpr std::uint64_t kArtWaitNs = 5'000'000u, kArtLatencyNs = 12'000'000u;
    // Uploads `image`'s texels (its whole size), as upload_before().
    void upload(Image &image, const std::uint32_t *texels, std::uint32_t width, std::uint32_t height);
};

} // namespace sarusaru::shell
