#include "shell/vulkan_context.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

} // namespace

int main() {
    using namespace sarusaru::shell;

    // The first matching UNORM format may advertise a linear presentation
    // space. Selecting it makes PSP's display-encoded colours too bright.
    const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    const auto selected = encoded_swapchain_format(formats);
    require(selected && selected->format == formats[1].format && selected->colorSpace == formats[1].colorSpace,
            "did not select the sRGB presentation pair");

    const VkSurfaceFormatKHR double_encoded[] = {
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    require(!encoded_swapchain_format(double_encoded), "accepted an sRGB image format for encoded GE output");
}
