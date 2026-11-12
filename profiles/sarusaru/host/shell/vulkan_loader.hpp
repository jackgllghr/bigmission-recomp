#pragma once

// Vulkan without linking it: SDL loads the driver library (MoltenVK next to
// the executable on macOS, the system loader elsewhere) and every function is
// resolved through vkGetInstanceProcAddr / vkGetDeviceProcAddr.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#define SARUSARU_VK_GLOBAL_FUNCTIONS(X) \
    X(vkCreateInstance)                 \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties)

#define SARUSARU_VK_INSTANCE_FUNCTIONS(X)        \
    X(vkDestroyInstance)                         \
    X(vkEnumeratePhysicalDevices)                \
    X(vkGetPhysicalDeviceProperties)             \
    X(vkGetPhysicalDeviceFeatures)               \
    X(vkGetPhysicalDeviceQueueFamilyProperties)  \
    X(vkGetPhysicalDeviceMemoryProperties)       \
    X(vkGetPhysicalDeviceFormatProperties)       \
    X(vkEnumerateDeviceExtensionProperties)      \
    X(vkCreateDevice)                            \
    X(vkGetDeviceProcAddr)                       \
    X(vkDestroySurfaceKHR)                       \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)      \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)      \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

namespace sarusaru::shell {

struct VulkanLoader {
#define SARUSARU_VK_MEMBER(name) PFN_##name name{};
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr{};
    SARUSARU_VK_GLOBAL_FUNCTIONS(SARUSARU_VK_MEMBER)
    SARUSARU_VK_INSTANCE_FUNCTIONS(SARUSARU_VK_MEMBER)
#undef SARUSARU_VK_MEMBER

    VkInstance instance{VK_NULL_HANDLE};
    // Instance extensions the driver lists that the instance enabled.
    bool portability{};

    // Loads the Vulkan library through SDL (SDL must be initialised).
    // Loads the Vulkan library (once; later calls do nothing). Must come
    // before creating an SDL_WINDOW_VULKAN window, which otherwise loads the
    // system's default library.
    bool load(std::string &error);
    // `extensions` are required (e.g. the window's surface extensions).
    bool create_instance(std::vector<const char *> extensions, bool validation, std::string &error);
    void destroy();
};

// The performance overlay's MangoHud choice (shell/settings.hpp), before the
// instance is made: turns on MangoHud's implicit Vulkan layer for this run
// (MANGOHUD=1, unless the environment already says) and says whether the
// loader lists it. Needs load() first. Only desktop Linux has the layer.
[[nodiscard]] bool enable_mangohud(VulkanLoader &loader);

} // namespace sarusaru::shell
