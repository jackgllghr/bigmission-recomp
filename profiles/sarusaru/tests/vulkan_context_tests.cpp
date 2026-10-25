#include "shell/pipeline_store.hpp"
#include "shell/range_allocator.hpp"
#include "shell/vulkan_context.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>
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

    // Image memory suballocation: aligned, first fit, merged when freed.
    {
        RangeAllocator ranges(1024u);
        const auto a = ranges.allocate(100u, 64u), b = ranges.allocate(100u, 256u), c = ranges.allocate(50u, 16u);
        require(a == 0u && b == 256u && c == 112u, "ranges are placed first fit and aligned");
        require(!ranges.allocate(1024u, 1u), "an allocation larger than the free space succeeded");
        ranges.free(*b);
        require(ranges.allocate(200u, 128u) == 256u, "a freed range is reused");
        ranges.free(256u);
        ranges.free(*a);
        ranges.free(*c);
        require(ranges.empty() && ranges.used() == 0u && ranges.free_ranges() == 1u, "freed ranges did not merge");
        require(ranges.allocate(1024u, 1u) == 0u, "the merged block is not whole again");
        ranges.free(12345u);  // not allocated: ignored
        require(ranges.used() == 1024u, "freeing an unknown offset changed the block");
    }

    // The pipeline states kept between runs round-trip, and damage is noticed.
    {
        const std::vector<pipeline_store::Key> keys{{3u, 1u, 2u, 3u, 4u}, {1u, 0x3C000u, 0x13u, 8u, 0u}};
        auto bytes = pipeline_store::serialize_keys(keys);
        const auto parsed = pipeline_store::parse_keys(bytes);
        require(parsed && *parsed == keys, "pipeline keys did not round-trip");
        bytes[20] ^= 1u;
        require(!pipeline_store::parse_keys(bytes), "a damaged key file was accepted");
        require(!pipeline_store::parse_keys(std::vector<std::uint8_t>(8u)), "a short key file was accepted");
        require(pipeline_store::parse_keys(pipeline_store::serialize_keys({})).value().empty(), "an empty key list");

        VkPhysicalDeviceProperties device{};
        device.vendorID = 0x106Bu;
        device.deviceID = 7u;
        device.pipelineCacheUUID[3] = 9u;
        std::vector<std::uint8_t> cache(40u);
        const std::uint32_t header[4] = {32u, VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x106Bu, 7u};
        std::memcpy(cache.data(), header, sizeof header);
        std::memcpy(cache.data() + 16u, device.pipelineCacheUUID, VK_UUID_SIZE);
        require(pipeline_store::cache_matches(cache, device), "this device's pipeline cache was rejected");
        device.pipelineCacheUUID[3] = 10u;
        require(!pipeline_store::cache_matches(cache, device), "another driver's pipeline cache was accepted");
        require(!pipeline_store::cache_matches(std::span(cache).first(20u), device), "a short pipeline cache was accepted");
    }
}
