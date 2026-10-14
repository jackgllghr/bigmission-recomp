// Loads Vulkan through SDL (MoltenVK on macOS), creates an instance and lists
// the physical devices: a check that the pinned graphics dependencies work on
// this machine, run as a test and usable by hand.
#include "shell/vulkan_loader.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    sarusaru::shell::VulkanLoader vk;
    std::string error;
    if (!vk.load(error) || !vk.create_instance({}, false, error)) {
        std::fprintf(stderr, "Vulkan unavailable: %s\n", error.c_str());
        SDL_Quit();
        return 1;
    }
    std::uint32_t count = 0;
    vk.vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vk.vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());
    for (auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vk.vkGetPhysicalDeviceProperties(device, &properties);
        std::printf("GPU: %s (Vulkan %u.%u)\n", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
                    VK_API_VERSION_MINOR(properties.apiVersion));
    }
    vk.destroy();
    SDL_Quit();
    return count > 0 ? 0 : 1;
}
