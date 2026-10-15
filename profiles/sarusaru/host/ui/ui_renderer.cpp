#include "ui/ui_renderer.hpp"

#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <cstring>

namespace sarusaru::ui {
namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Dear ImGui's backend resolves every Vulkan function through the shell's
// loader: there is no linked Vulkan library to take them from.
PFN_vkVoidFunction load_function(const char *name, void *user) {
    auto &vk = static_cast<shell::VulkanContext *>(user)->vk;
    return vk.vkGetInstanceProcAddr(vk.instance, name);
}

} // namespace

bool UiRenderer::create(std::string &error) {
    VkAttachmentDescription color{};
    color.format = kFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Ready for end_frame's blit into the swapchain.
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0u;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[1] = dependencies[0];
    dependencies[1].srcSubpass = 0u;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1u;
    info.pAttachments = &color;
    info.subpassCount = 1u;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2u;
    info.pDependencies = dependencies;
    if (vulkan_.vkCreateRenderPass(vulkan_.device, &info, nullptr, &render_pass_) != VK_SUCCESS) {
        error = "cannot create the interface's render pass";
        return false;
    }
    // Compatible with the first (load operations do not count), so the
    // pipelines and framebuffer made for it serve this one too.
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (vulkan_.vkCreateRenderPass(vulkan_.device, &info, nullptr, &load_pass_) != VK_SUCCESS) {
        error = "cannot create the interface's render pass";
        return false;
    }

    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_1, load_function, &vulkan_)) {
        error = "cannot load Vulkan functions for the interface";
        return false;
    }
    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = VK_API_VERSION_1_1;
    init.Instance = vulkan_.vk.instance;
    init.PhysicalDevice = vulkan_.physical;
    init.Device = vulkan_.device;
    init.QueueFamily = vulkan_.queue_family;
    init.Queue = vulkan_.queue;
    init.DescriptorPoolSize = 64u;
    // Frames are serialised (VulkanContext waits for each one), so two sets
    // of buffers are more than enough.
    init.MinImageCount = 2u;
    init.ImageCount = 2u;
    init.PipelineInfoMain.RenderPass = render_pass_;
    init.PipelineInfoMain.Subpass = 0u;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&init)) {
        error = "cannot start the interface's Vulkan backend";
        return false;
    }
    backend_ = true;
    return true;
}

void UiRenderer::destroy_target() {
    if (framebuffer_ != VK_NULL_HANDLE) vulkan_.vkDestroyFramebuffer(vulkan_.device, framebuffer_, nullptr);
    framebuffer_ = VK_NULL_HANDLE;
    vulkan_.destroy_image(target_);
}

void UiRenderer::destroy() {
    if (vulkan_.device == VK_NULL_HANDLE) return;
    if (render_pass_ == VK_NULL_HANDLE && load_pass_ == VK_NULL_HANDLE && !backend_ && textures_.empty()) return;
    vulkan_.vkDeviceWaitIdle(vulkan_.device);
    for (auto &texture : textures_) {
        if (backend_ && texture.descriptor != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
        vulkan_.destroy_image(texture.image);
        vulkan_.destroy_buffer(texture.staging);
    }
    textures_.clear();
    if (backend_) ImGui_ImplVulkan_Shutdown();
    backend_ = false;
    destroy_target();
    vulkan_.destroy_image(backdrop_);
    backdrop_shown_ = false;
    backdrop_source_ = nullptr;
    if (render_pass_ != VK_NULL_HANDLE) vulkan_.vkDestroyRenderPass(vulkan_.device, render_pass_, nullptr);
    if (load_pass_ != VK_NULL_HANDLE) vulkan_.vkDestroyRenderPass(vulkan_.device, load_pass_, nullptr);
    render_pass_ = load_pass_ = VK_NULL_HANDLE;
}

bool UiRenderer::resize(std::uint32_t width, std::uint32_t height) {
    if (target_.image != VK_NULL_HANDLE && target_.width == width && target_.height == height) return true;
    // The previous frame may still read the old target.
    vulkan_.vkDeviceWaitIdle(vulkan_.device);
    destroy_target();
    if (!vulkan_.create_image(width, height, kFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, target_))
        return false;
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = render_pass_;
    info.attachmentCount = 1u;
    info.pAttachments = &target_.view;
    info.width = width;
    info.height = height;
    info.layers = 1u;
    return vulkan_.vkCreateFramebuffer(vulkan_.device, &info, nullptr, &framebuffer_) == VK_SUCCESS;
}

void UiRenderer::set_backdrop(const shell::Image *source, std::uint32_t width, std::uint32_t height) {
    backdrop_source_ = source != nullptr && width != 0u && height != 0u ? source : nullptr;
    backdrop_width_ = width;
    backdrop_height_ = height;
    if (backdrop_source_ == nullptr) backdrop_shown_ = false;
}

ImTextureID UiRenderer::add_texture(const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height) {
    Texture texture;
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4u;
    if (!vulkan_.create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, texture.staging) ||
        !vulkan_.create_image(width, height, kFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, texture.image)) {
        vulkan_.destroy_buffer(texture.staging);
        vulkan_.destroy_image(texture.image);
        return ImTextureID_Invalid;
    }
    std::memcpy(texture.staging.mapped, rgba, static_cast<std::size_t>(size));
    texture.descriptor = ImGui_ImplVulkan_AddTexture(texture.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    textures_.push_back(texture);
    return reinterpret_cast<ImTextureID>(texture.descriptor);
}

const shell::Image *UiRenderer::render(VkCommandBuffer commands, ImDrawData *data, std::uint32_t width,
                                       std::uint32_t height) {
    if (width == 0u || height == 0u || !resize(width, height)) return nullptr;
    for (auto &texture : textures_) {
        if (texture.uploaded) continue;
        vulkan_.transition(commands, texture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        copy.imageExtent = {texture.image.width, texture.image.height, 1u};
        vulkan_.vkCmdCopyBufferToImage(commands, texture.staging.buffer, texture.image.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
        vulkan_.transition(commands, texture.image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        texture.uploaded = true;
    }
    if (backdrop_source_ != nullptr) {
        // A copy of the game's picture, which may change or go once the game runs on.
        if (backdrop_.image == VK_NULL_HANDLE || backdrop_.width != backdrop_width_ || backdrop_.height != backdrop_height_) {
            vulkan_.vkDeviceWaitIdle(vulkan_.device);
            vulkan_.destroy_image(backdrop_);
            if (!vulkan_.create_image(backdrop_width_, backdrop_height_, kFormat,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, backdrop_))
                backdrop_source_ = nullptr;
        }
        if (backdrop_source_ != nullptr) {
            vulkan_.transition(commands, backdrop_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit blit{};
            blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
            blit.srcOffsets[1] = blit.dstOffsets[1] = {static_cast<std::int32_t>(backdrop_width_),
                                                       static_cast<std::int32_t>(backdrop_height_), 1};
            vulkan_.vkCmdBlitImage(commands, backdrop_source_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   backdrop_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit, VK_FILTER_NEAREST);
            vulkan_.transition(commands, backdrop_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            backdrop_shown_ = true;
        }
        backdrop_source_ = nullptr;
    }
    if (backdrop_shown_) {
        // The backdrop letterboxed at its aspect ratio over black, as the game is shown.
        vulkan_.transition(commands, target_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue black{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        vulkan_.vkCmdClearColorImage(commands, target_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1u, &range);
        const double scale = std::min(static_cast<double>(width) / backdrop_.width,
                                      static_cast<double>(height) / backdrop_.height);
        const auto out_width = static_cast<std::int32_t>(backdrop_.width * scale);
        const auto out_height = static_cast<std::int32_t>(backdrop_.height * scale);
        const auto x = (static_cast<std::int32_t>(width) - out_width) / 2;
        const auto y = (static_cast<std::int32_t>(height) - out_height) / 2;
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        blit.srcOffsets[1] = {static_cast<std::int32_t>(backdrop_.width), static_cast<std::int32_t>(backdrop_.height), 1};
        blit.dstOffsets[0] = {x, y, 0};
        blit.dstOffsets[1] = {x + out_width, y + out_height, 1};
        vulkan_.vkCmdBlitImage(commands, backdrop_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target_.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit, VK_FILTER_LINEAR);
        vulkan_.transition(commands, target_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = backdrop_shown_ ? load_pass_ : render_pass_;
    begin.framebuffer = framebuffer_;
    begin.renderArea.extent = {width, height};
    begin.clearValueCount = 1u;
    begin.pClearValues = &clear;
    vulkan_.vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    if (data != nullptr) ImGui_ImplVulkan_RenderDrawData(data, commands);
    vulkan_.vkCmdEndRenderPass(commands);
    return &target_;
}

} // namespace sarusaru::ui
