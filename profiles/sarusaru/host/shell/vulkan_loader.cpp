#include "shell/vulkan_loader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace sarusaru::shell {

bool VulkanLoader::load(std::string &error) {
    const char *library = nullptr;
#if defined(__APPLE__)
    // Errors only, unless MVK_CONFIG_LOG_LEVEL is already set.
    setenv("MVK_CONFIG_LOG_LEVEL", "1", 0);
    // The pinned MoltenVK sits in lib/ next to the executable.
    std::string path = std::string(SDL_GetBasePath() != nullptr ? SDL_GetBasePath() : "") + "lib/libMoltenVK.dylib";
    library = path.c_str();
#endif
    if (!SDL_Vulkan_LoadLibrary(library)) {
        error = std::string("cannot load Vulkan: ") + SDL_GetError();
        return false;
    }
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (vkGetInstanceProcAddr == nullptr) {
        error = "the Vulkan library has no vkGetInstanceProcAddr";
        return false;
    }
#define SARUSARU_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));
    SARUSARU_VK_GLOBAL_FUNCTIONS(SARUSARU_VK_LOAD)
#undef SARUSARU_VK_LOAD
    if (vkCreateInstance == nullptr || vkEnumerateInstanceExtensionProperties == nullptr) {
        error = "the Vulkan library lacks the global functions";
        return false;
    }
    return true;
}

bool VulkanLoader::create_instance(std::vector<const char *> extensions, bool validation, std::string &error) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    const auto has = [&](const char *name) {
        return std::ranges::any_of(available, [name](const auto &e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    // MoltenVK is a "portability" implementation; through a loader it is only
    // enumerated when the application opts in.
    VkInstanceCreateFlags flags = 0;
    if (has(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        portability = true;
    }
    if (has(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    for (const char *name : extensions) {
        if (!has(name)) {
            error = std::string("missing instance extension ") + name;
            return false;
        }
    }
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "SaruSaru Recomp";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = flags;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = validation ? 1u : 0u;
    info.ppEnabledLayerNames = layers;
    if (const VkResult result = vkCreateInstance(&info, nullptr, &instance); result != VK_SUCCESS) {
        error = "vkCreateInstance failed (" + std::to_string(result) + ")";
        return false;
    }
#define SARUSARU_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
    SARUSARU_VK_INSTANCE_FUNCTIONS(SARUSARU_VK_LOAD)
#undef SARUSARU_VK_LOAD
    return true;
}

void VulkanLoader::destroy() {
    if (instance != VK_NULL_HANDLE && vkDestroyInstance != nullptr) vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
    SDL_Vulkan_UnloadLibrary();
}

} // namespace sarusaru::shell
