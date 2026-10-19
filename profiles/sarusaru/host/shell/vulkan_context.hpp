#pragma once

// The Vulkan device, swapchain and command buffers the shell renders with.
//
// Work is recorded into one of kFramesInFlight slots, each with its own
// command buffer and fence: the CPU records a slot while the GPU may still run
// the other, and waits only for the slot it is about to reuse. Whatever the
// CPU writes for a submission (mapped buffers, descriptor pools) must
// therefore be per slot, and a resource the GPU may still read is handed to
// retire() rather than destroyed.
#include "shell/range_allocator.hpp"
#include "shell/vulkan_loader.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#define SARUSARU_VK_DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle) \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateImage) \
    X(vkDestroyImage) X(vkCreateImageView) X(vkDestroyImageView) X(vkGetImageMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateBuffer) X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdBlitImage) X(vkCmdClearColorImage) \
    X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
    X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
    X(vkCmdBindVertexBuffers) X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) \
    X(vkCmdSetBlendConstants) X(vkCmdSetStencilReference) X(vkCmdDraw) X(vkCmdClearDepthStencilImage) X(vkCmdCopyImageToBuffer) \
    X(vkCreatePipelineCache) X(vkDestroyPipelineCache) X(vkGetPipelineCacheData)

namespace sarusaru::shell {

// The GE stores and blends display-encoded PSP colours in UNORM images.  The
// presentation surface must therefore interpret those values as sRGB; an sRGB
// image format would encode them a second time.
[[nodiscard]] std::optional<VkSurfaceFormatKHR> encoded_swapchain_format(
    std::span<const VkSurfaceFormatKHR> formats);

struct Buffer {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    void *mapped{};
    VkDeviceSize size{};
};

struct Image {
    VkImage image{VK_NULL_HANDLE};
    // The image's own allocation, or the shared block it was suballocated
    // from (at `offset`, block `block`).
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkDeviceSize offset{};
    std::uint32_t block{UINT32_MAX};
    VkImageView view{VK_NULL_HANDLE};
    std::uint32_t width{}, height{};
    VkFormat format{VK_FORMAT_UNDEFINED};
};

class VulkanContext {
public:
    static constexpr std::uint32_t kFramesInFlight = 2u;

#define SARUSARU_VK_MEMBER(name) PFN_##name name{};
    SARUSARU_VK_DEVICE_FUNCTIONS(SARUSARU_VK_MEMBER)
#undef SARUSARU_VK_MEMBER

    VulkanLoader vk;
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkPhysicalDevice physical{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties{};
    VkDevice device{VK_NULL_HANDLE};
    std::uint32_t queue_family{};
    VkQueue queue{VK_NULL_HANDLE};
    // The GE targets' depth buffer, with a stencil aspect when the device has
    // a depth/stencil attachment format (the GE's stencil test needs one).
    VkFormat depth_format{VK_FORMAT_D32_SFLOAT};
    VkImageAspectFlags depth_aspect{VK_IMAGE_ASPECT_DEPTH_BIT};

    // Creates everything for `window`; false with `error` set on failure.
    bool create(SDL_Window *window, std::string &error);
    void destroy();

    // Starts recording the next slot's command buffer, waiting only for that
    // slot's previous submission.
    VkCommandBuffer begin_frame();
    // The slot being recorded, 0 to kFramesInFlight - 1.
    [[nodiscard]] std::uint32_t slot() const noexcept { return slot_; }
    // Ends the frame: copies `source` (in TRANSFER_SRC layout, or nothing when
    // null) letterboxed into the swapchain image and presents it.
    // With present=false, still submits all GE/capture work without acquiring a swapchain image.
    void end_frame(const Image *source, std::uint32_t source_width, std::uint32_t source_height, bool present = true);
    // Submits the commands recorded so far and moves on to the next slot
    // without waiting for them: for when a slot's resources run out mid-frame.
    VkCommandBuffer submit();
    // As submit(), and also waits for everything submitted: for reading back
    // what the GPU wrote.
    VkCommandBuffer flush();
    // Destroys the resource once the GPU is done with the work recorded so far.
    void retire(Image &image);
    void retire(Buffer &buffer);

    // Records a copy of the top-left `width` x `height` of `source` (an RGBA8
    // image in TRANSFER_SRC layout) into `destination`, readable once the
    // frame has finished (after end_frame and vkQueueWaitIdle).
    void copy_to_buffer(const Image &source, std::uint32_t width, std::uint32_t height, const Buffer &destination);

    [[nodiscard]] std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const;
    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer &out);
    void destroy_buffer(Buffer &buffer);
    bool create_image(std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect, Image &out);
    void destroy_image(Image &image);
    // Records a full-subresource layout transition.
    void transition(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                    VkImageLayout to) const;

private:
    // Images share large blocks of device memory, one allocation each instead
    // of one per image (drivers can take milliseconds over vkAllocateMemory).
    static constexpr VkDeviceSize kBlockBytes = 64ull << 20u;
    struct MemoryBlock {
        VkDeviceMemory memory{VK_NULL_HANDLE};
        std::uint32_t type{};
        std::unique_ptr<RangeAllocator> ranges;
    };
    std::vector<MemoryBlock> blocks_;
    VkPhysicalDeviceMemoryProperties memory_properties_{};

    struct Slot {
        VkCommandBuffer commands{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkSemaphore acquired{VK_NULL_HANDLE};
        bool submitted{};
        std::vector<Image> retired_images;
        std::vector<Buffer> retired_buffers;
    };
    std::array<Slot, kFramesInFlight> slots_{};
    std::uint32_t slot_{kFramesInFlight - 1u};
    void submit_slot(const VkSubmitInfo &submit);
    void destroy_retired(Slot &slot);
    void free_image_memory(Image &image);

    SDL_Window *window_{};
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkFormat swapchain_format_{};
    VkExtent2D extent_{};
    std::vector<VkImage> swapchain_images_;
    VkCommandPool pool_{VK_NULL_HANDLE};
    VkCommandBuffer commands_{VK_NULL_HANDLE};  // the current slot's
    std::vector<VkSemaphore> rendered_;  // one per swapchain image
    bool recording_{};
    bool swapchain_stale_{};
    // Set when the window's surface is gone: on Android the system destroys
    // it whenever the app leaves the screen (another app, the file picker),
    // and SDL gives the window a new one when it comes back. Set from the
    // thread that reports the event, read on the frame's.
    std::atomic<bool> surface_lost_{};

    // What the surface reported when the swapchain was made: a suboptimal
    // result means something changed only if these did (Android reports one
    // for every frame presented without its rotation, as the shell does).
    VkExtent2D reported_extent_{};
    VkSurfaceTransformFlagBitsKHR reported_transform_{};

    static bool SDLCALL watch_lifecycle(void *context, SDL_Event *event);
    [[nodiscard]] bool surface_changed() const;
    bool create_swapchain(std::string &error);
    void destroy_swapchain();
    // A new surface (and so swapchain) for the window's current native window.
    void recreate_surface();
};

} // namespace sarusaru::shell
