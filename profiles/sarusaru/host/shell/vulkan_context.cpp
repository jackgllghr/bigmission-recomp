#include "shell/vulkan_context.hpp"
#include "shell/hitch_trace.hpp"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace sarusaru::shell {

std::optional<VkSurfaceFormatKHR> encoded_swapchain_format(std::span<const VkSurfaceFormatKHR> formats) {
    for (const auto format : formats) {
        if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) return format;
    }
    return std::nullopt;
}

bool VulkanContext::create(SDL_Window *window, std::string &error) {
    window_ = window;
    if (!vk.load(error)) return false;
    Uint32 count = 0;
    const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (names == nullptr) {
        error = std::string("no Vulkan surface extensions: ") + SDL_GetError();
        return false;
    }
    std::vector<const char *> extensions(names, names + count);
    if (!vk.create_instance(extensions, false, error)) return false;
    if (!SDL_Vulkan_CreateSurface(window, vk.instance, nullptr, &surface)) {
        error = std::string("cannot create a Vulkan surface: ") + SDL_GetError();
        return false;
    }
    SDL_AddEventWatch(watch_lifecycle, this);

    // The first device with a queue that both draws and presents.
    std::uint32_t devices = 0;
    vk.vkEnumeratePhysicalDevices(vk.instance, &devices, nullptr);
    std::vector<VkPhysicalDevice> candidates(devices);
    vk.vkEnumeratePhysicalDevices(vk.instance, &devices, candidates.data());
    for (const auto candidate : candidates) {
        std::uint32_t families = 0;
        vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> properties(families);
        vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, properties.data());
        for (std::uint32_t family = 0; family < families; ++family) {
            VkBool32 present = VK_FALSE;
            vk.vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &present);
            if ((properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && present == VK_TRUE) {
                physical = candidate;
                queue_family = family;
                break;
            }
        }
        if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) {
        error = "no Vulkan device can draw to this window";
        return false;
    }
    vk.vkGetPhysicalDeviceProperties(physical, &properties);
    // A depth format with stencil, D32S8 or else D24S8 (both hold the GE's
    // 16-bit depth exactly). Without either the stencil test is not emulated.
    for (const VkFormat format : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties format_properties{};
        vk.vkGetPhysicalDeviceFormatProperties(physical, format, &format_properties);
        if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u) {
            depth_format = format;
            depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
        }
    }

    std::uint32_t available_count = 0;
    vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &available_count, nullptr);
    std::vector<VkExtensionProperties> available(available_count);
    vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &available_count, available.data());
    std::vector<const char *> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // MoltenVK requires VK_KHR_portability_subset to be enabled when it is listed.
    if (std::ranges::any_of(available, [](const auto &e) { return std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0; }))
        device_extensions.push_back("VK_KHR_portability_subset");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1u;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1u;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();
    if (vk.vkCreateDevice(physical, &device_info, nullptr, &device) != VK_SUCCESS) {
        error = "vkCreateDevice failed";
        return false;
    }
#define SARUSARU_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(device, #name));
    SARUSARU_VK_DEVICE_FUNCTIONS(SARUSARU_VK_LOAD)
#undef SARUSARU_VK_LOAD
    vkGetDeviceQueue(device, queue_family, 0u, &queue);

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;
    vkCreateCommandPool(device, &pool_info, nullptr, &pool_);
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1u;
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto &slot : slots_) {
        vkAllocateCommandBuffers(device, &allocate, &slot.commands);
        vkCreateFence(device, &fence_info, nullptr, &slot.fence);
        vkCreateSemaphore(device, &semaphore_info, nullptr, &slot.acquired);
    }
    vk.vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties_);
    return create_swapchain(error);
}

bool SDLCALL VulkanContext::watch_lifecycle(void *context, SDL_Event *event) {
    if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) static_cast<VulkanContext *>(context)->surface_lost_ = true;
    return true;
}

void VulkanContext::recreate_surface() {
    vkDeviceWaitIdle(device);
    destroy_swapchain();
    if (surface != VK_NULL_HANDLE) vk.vkDestroySurfaceKHR(vk.instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, vk.instance, nullptr, &surface)) {
        // No native window yet: frames go unpresented until the next try.
        surface = VK_NULL_HANDLE;
        surface_lost_ = true;
        return;
    }
    swapchain_stale_ = true;
}

bool VulkanContext::surface_changed() const {
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities) != VK_SUCCESS) return true;
    return capabilities.currentTransform != reported_transform_ ||
           capabilities.currentExtent.width != reported_extent_.width ||
           capabilities.currentExtent.height != reported_extent_.height;
}

bool VulkanContext::create_swapchain(std::string &error) {
    if (surface == VK_NULL_HANDLE) return true;  // lost; recreate_surface retries
    VkSurfaceCapabilitiesKHR capabilities{};
    vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
    std::uint32_t format_count = 0;
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats.data());
    if (formats.empty()) {
        error = "the surface lists no formats";
        return false;
    }
    const auto chosen = encoded_swapchain_format(formats);
    if (!chosen) {
        error = "the surface has no sRGB presentation format compatible with the PSP renderer";
        return false;
    }
    std::uint32_t mode_count = 0;
    vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, modes.data());
    // The shell paces itself to guest time, so presentation should not block.
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto candidate : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
        if (std::ranges::find(modes, candidate) != modes.end()) mode = candidate;

    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    reported_extent_ = capabilities.currentExtent;
    reported_transform_ = capabilities.currentTransform;
    extent_ = capabilities.currentExtent;
    if (extent_.width == 0xFFFFFFFFu) {
        extent_.width = std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (extent_.width == 0u || extent_.height == 0u) return true;  // minimised; retried later
    // On a rotated display (Android) the surface reports the rotation as its
    // transform; presenting with it would promise images already rotated.
    // Everything is drawn upright, so ask for no transform and let the
    // compositor turn the picture. The images are then the window's shape:
    // drivers differ in whether the reported extent already is.
    VkSurfaceTransformFlagBitsKHR transform = capabilities.currentTransform;
    if ((capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0u) {
        constexpr auto kQuarterTurns = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
        if ((transform & kQuarterTurns) != 0u && width > 0 && height > 0 &&
            (width > height) != (extent_.width > extent_.height))
            std::swap(extent_.width, extent_.height);
        transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
#if defined(__ANDROID__)
    SDL_Log("swapchain %ux%u for a %dx%d window, surface transform %#x", extent_.width, extent_.height, width, height,
            static_cast<unsigned>(capabilities.currentTransform));
#endif
    std::uint32_t image_count = capabilities.minImageCount + 1u;
    if (capabilities.maxImageCount != 0u) image_count = std::min(image_count, capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = image_count;
    info.imageFormat = chosen->format;
    info.imageColorSpace = chosen->colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1u;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = transform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain_) != VK_SUCCESS) {
        error = "vkCreateSwapchainKHR failed";
        return false;
    }
    swapchain_format_ = chosen->format;
    vkGetSwapchainImagesKHR(device, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain_, &image_count, swapchain_images_.data());
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    rendered_.resize(image_count);
    for (auto &semaphore : rendered_) vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore);
    swapchain_stale_ = false;
    return true;
}

void VulkanContext::destroy_swapchain() {
    for (auto semaphore : rendered_) vkDestroySemaphore(device, semaphore, nullptr);
    rendered_.clear();
    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    swapchain_images_.clear();
}

void VulkanContext::destroy() {
    if (window_ != nullptr) SDL_RemoveEventWatch(watch_lifecycle, this);
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        destroy_swapchain();
        for (auto &slot : slots_) {
            destroy_retired(slot);
            vkDestroySemaphore(device, slot.acquired, nullptr);
            vkDestroyFence(device, slot.fence, nullptr);
        }
        for (auto &block : blocks_)
            if (block.memory != VK_NULL_HANDLE) vkFreeMemory(device, block.memory, nullptr);
        blocks_.clear();
        vkDestroyCommandPool(device, pool_, nullptr);
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (surface != VK_NULL_HANDLE) vk.vkDestroySurfaceKHR(vk.instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
    vk.destroy();
}

VkCommandBuffer VulkanContext::begin_frame() {
    // A slot begun and never submitted is simply recorded again: its fence
    // is reset only as it is submitted, so it stays signalled.
    if (!recording_) {
        slot_ = (slot_ + 1u) % kFramesInFlight;
        Slot &slot = slots_[slot_];
        {
            hitch::Timer timer(hitch::kFence);
            vkWaitForFences(device, 1u, &slot.fence, VK_TRUE, UINT64_MAX);
        }
        destroy_retired(slot);
    }
    commands_ = slots_[slot_].commands;
    vkResetCommandBuffer(commands_, 0u);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands_, &begin);
    recording_ = true;
    return commands_;
}

void VulkanContext::submit_slot(const VkSubmitInfo &submit) {
    Slot &slot = slots_[slot_];
    vkEndCommandBuffer(slot.commands);
    vkResetFences(device, 1u, &slot.fence);
    vkQueueSubmit(queue, 1u, &submit, slot.fence);
    recording_ = false;
}

void VulkanContext::destroy_retired(Slot &slot) {
    for (auto &image : slot.retired_images) destroy_image(image);
    for (auto &buffer : slot.retired_buffers) destroy_buffer(buffer);
    slot.retired_images.clear();
    slot.retired_buffers.clear();
}

void VulkanContext::retire(Image &image) {
    if (image.image != VK_NULL_HANDLE || image.view != VK_NULL_HANDLE) slots_[slot_].retired_images.push_back(image);
    image = Image{};
}

void VulkanContext::retire(Buffer &buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) slots_[slot_].retired_buffers.push_back(buffer);
    buffer = Buffer{};
}

void VulkanContext::copy_to_buffer(const Image &source, std::uint32_t width, std::uint32_t height,
                                   const Buffer &destination) {
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    copy.imageExtent = {width, height, 1u};
    vkCmdCopyImageToBuffer(commands_, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.buffer, 1u, &copy);
}

VkCommandBuffer VulkanContext::submit() {
    {
        hitch::Timer timer(hitch::kFlush);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &commands_;
        submit_slot(submit);
    }
    return begin_frame();
}

VkCommandBuffer VulkanContext::flush() {
    {
        hitch::Timer timer(hitch::kFlush);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &commands_;
        submit_slot(submit);
        // The queue runs in order: this slot done means all of it is.
        vkWaitForFences(device, 1u, &slots_[slot_].fence, VK_TRUE, UINT64_MAX);
    }
    return begin_frame();
}

void VulkanContext::end_frame(const Image *source, std::uint32_t source_width, std::uint32_t source_height, bool present) {
    std::string error;
    if (surface_lost_.exchange(false)) recreate_surface();
    if (swapchain_stale_ || swapchain_ == VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        destroy_swapchain();
        (void)create_swapchain(error);
    }
    std::uint32_t index = 0u;
    present = present && swapchain_ != VK_NULL_HANDLE;
    VkSemaphore acquired_semaphore = slots_[slot_].acquired;
    if (present) {
        hitch::Timer timer(hitch::kAcquire);
        const VkResult acquired = vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, acquired_semaphore, VK_NULL_HANDLE, &index);
        if (acquired == VK_ERROR_SURFACE_LOST_KHR) {
            surface_lost_ = true;
            present = false;
        } else if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_stale_ = true;
            present = false;
        } else if (acquired == VK_SUBOPTIMAL_KHR) {
            swapchain_stale_ = surface_changed();
        }
    }
    if (present) {
        const VkImage target = swapchain_images_[index];
        transition(commands_, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue black{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        vkCmdClearColorImage(commands_, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1u, &range);
        if (source != nullptr && source_width != 0u && source_height != 0u) {
            // Letterbox the guest picture at its aspect ratio.
            const double scale = std::min(static_cast<double>(extent_.width) / source_width,
                                          static_cast<double>(extent_.height) / source_height);
            const auto out_width = static_cast<std::int32_t>(source_width * scale);
            const auto out_height = static_cast<std::int32_t>(source_height * scale);
            const auto x = (static_cast<std::int32_t>(extent_.width) - out_width) / 2;
            const auto y = (static_cast<std::int32_t>(extent_.height) - out_height) / 2;
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
            blit.srcOffsets[1] = {static_cast<std::int32_t>(source_width), static_cast<std::int32_t>(source_height), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
            blit.dstOffsets[0] = {x, y, 0};
            blit.dstOffsets[1] = {x + out_width, y + out_height, 1};
            vkCmdBlitImage(commands_, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit, VK_FILTER_LINEAR);
        }
        transition(commands_, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1u;
    submit.pCommandBuffers = &commands_;
    if (present) {
        submit.waitSemaphoreCount = 1u;
        submit.pWaitSemaphores = &acquired_semaphore;
        submit.pWaitDstStageMask = &wait_stage;
        submit.signalSemaphoreCount = 1u;
        submit.pSignalSemaphores = &rendered_[index];
    }
    {
        hitch::Timer timer(hitch::kSubmit);
        submit_slot(submit);
    }
    if (present) {
        hitch::Timer timer(hitch::kPresent);
        VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        info.waitSemaphoreCount = 1u;
        info.pWaitSemaphores = &rendered_[index];
        info.swapchainCount = 1u;
        info.pSwapchains = &swapchain_;
        info.pImageIndices = &index;
        const VkResult result = vkQueuePresentKHR(queue, &info);
        if (result == VK_ERROR_SURFACE_LOST_KHR) surface_lost_ = true;
        else if (result == VK_ERROR_OUT_OF_DATE_KHR) swapchain_stale_ = true;
        else if (result == VK_SUBOPTIMAL_KHR && !swapchain_stale_) swapchain_stale_ = surface_changed();
    }
}

std::uint32_t VulkanContext::memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    const auto &memory = memory_properties_;
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) != 0u && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}

bool VulkanContext::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer &out) {
    hitch::Timer timer(hitch::kAlloc);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    if (vkCreateBuffer(device, &info, nullptr, &out.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out.buffer, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memory_type(requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device, &allocate, nullptr, &out.memory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(device, out.buffer, out.memory, 0u);
    vkMapMemory(device, out.memory, 0u, size, 0u, &out.mapped);
    out.size = size;
    return true;
}

void VulkanContext::destroy_buffer(Buffer &buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory != VK_NULL_HANDLE) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = Buffer{};
}

bool VulkanContext::create_image(std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage,
                                 VkImageAspectFlags aspect, Image &out) {
    hitch::Timer timer(hitch::kAlloc);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1u};
    info.mipLevels = 1u;
    info.arrayLayers = 1u;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &out.image) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, out.image, &requirements);
    const std::uint32_t type = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return false;
    // Small images share a block (only optimal-tiling images live in them, so
    // bufferImageGranularity does not apply); large ones get their own memory.
    if (requirements.size <= kBlockBytes / 4u) {
        const auto place = [&](std::uint32_t index) {
            auto &block = blocks_[index];
            if (block.memory == VK_NULL_HANDLE || block.type != type) return false;
            const auto offset = block.ranges->allocate(requirements.size, requirements.alignment);
            if (!offset) return false;
            out.memory = block.memory;
            out.offset = *offset;
            out.block = index;
            return true;
        };
        bool placed = false;
        for (std::uint32_t i = 0; i < blocks_.size() && !placed; ++i) placed = place(i);
        if (!placed) {
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocate.allocationSize = kBlockBytes;
            allocate.memoryTypeIndex = type;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            if (vkAllocateMemory(device, &allocate, nullptr, &memory) == VK_SUCCESS) {
                std::uint32_t index = 0;
                while (index < blocks_.size() && blocks_[index].memory != VK_NULL_HANDLE) ++index;
                if (index == blocks_.size()) blocks_.emplace_back();
                blocks_[index] = {memory, type, std::make_unique<RangeAllocator>(kBlockBytes)};
                placed = place(index);
            }
        }
        if (placed) {
            vkBindImageMemory(device, out.image, out.memory, out.offset);
        }
    }
    if (out.memory == VK_NULL_HANDLE) {
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        if (vkAllocateMemory(device, &allocate, nullptr, &out.memory) != VK_SUCCESS) return false;
        vkBindImageMemory(device, out.image, out.memory, 0u);
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0u, 1u, 0u, 1u};
    if (vkCreateImageView(device, &view, nullptr, &out.view) != VK_SUCCESS) return false;
    out.width = width;
    out.height = height;
    out.format = format;
    return true;
}

void VulkanContext::destroy_image(Image &image) {
    if (image.view != VK_NULL_HANDLE) vkDestroyImageView(device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE) vkDestroyImage(device, image.image, nullptr);
    free_image_memory(image);
    image = Image{};
}

void VulkanContext::free_image_memory(Image &image) {
    if (image.memory == VK_NULL_HANDLE) return;
    if (image.block == UINT32_MAX) {
        vkFreeMemory(device, image.memory, nullptr);
        return;
    }
    auto &block = blocks_[image.block];
    block.ranges->free(image.offset);
    if (!block.ranges->empty()) return;
    // An empty block goes unless it is its memory type's last.
    for (std::uint32_t i = 0; i < blocks_.size(); ++i) {
        if (i == image.block || blocks_[i].memory == VK_NULL_HANDLE || blocks_[i].type != block.type) continue;
        vkFreeMemory(device, block.memory, nullptr);
        block = MemoryBlock{};
        return;
    }
}

void VulkanContext::transition(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                               VkImageLayout to) const {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0u, 1u, 0u, 1u};
    // Coarse but correct: every earlier write is visible to every later access.
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u,
                         nullptr, 0u, nullptr, 1u, &barrier);
}

} // namespace sarusaru::shell
