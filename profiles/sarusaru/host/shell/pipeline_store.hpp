#pragma once

// What GeRenderer keeps between runs so it does not compile pipelines while
// the game plays: the driver's VkPipelineCache data, and the pipeline states
// the game has used (each five words, GeRenderer::PipelineKey), built ahead on
// a worker thread at the next start. Both live beside the settings file.
#include "shell/vulkan_loader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

namespace sarusaru::shell::pipeline_store {

using Key = std::array<std::uint32_t, 5>;

inline constexpr const char *kCacheName = "ge_pipelines.cache";
inline constexpr const char *kKeysName = "ge_pipelines.keys";
inline constexpr std::uint32_t kKeysMagic = 0x4B505353u;  // "SSPK"
inline constexpr std::uint32_t kKeysVersion = 1u;

inline std::uint32_t checksum(std::span<const std::uint32_t> words) {
    std::uint32_t hash = 0x811C9DC5u;
    for (const auto word : words) hash = (hash ^ word) * 0x01000193u;
    return hash;
}

// magic, version, count, checksum of the keys, then the keys.
inline std::vector<std::uint8_t> serialize_keys(std::span<const Key> keys) {
    std::vector<std::uint32_t> words;
    for (const auto &key : keys) words.insert(words.end(), key.begin(), key.end());
    std::vector<std::uint32_t> all{kKeysMagic, kKeysVersion, static_cast<std::uint32_t>(keys.size()), checksum(words)};
    all.insert(all.end(), words.begin(), words.end());
    std::vector<std::uint8_t> bytes(all.size() * 4u);
    std::memcpy(bytes.data(), all.data(), bytes.size());
    return bytes;
}

// Nothing for a file of another version or a damaged one.
inline std::optional<std::vector<Key>> parse_keys(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 16u || bytes.size() % 4u != 0u) return std::nullopt;
    std::vector<std::uint32_t> words(bytes.size() / 4u);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    const std::uint32_t count = words[2];
    if (words[0] != kKeysMagic || words[1] != kKeysVersion || words.size() != 4u + std::size_t(count) * 5u) return std::nullopt;
    const std::span<const std::uint32_t> payload(words.data() + 4u, words.size() - 4u);
    if (checksum(payload) != words[3]) return std::nullopt;
    std::vector<Key> keys(count);
    for (std::uint32_t i = 0; i < count; ++i) std::copy_n(payload.data() + i * 5u, 5u, keys[i].begin());
    return keys;
}

// Whether `data` is VkPipelineCache data this device made (drivers should
// reject foreign data themselves; not all of them do it gracefully).
inline bool cache_matches(std::span<const std::uint8_t> data, const VkPhysicalDeviceProperties &properties) {
    if (data.size() < 32u) return false;
    std::uint32_t header[4];
    std::memcpy(header, data.data(), sizeof header);
    return header[0] >= 32u && header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header[2] == properties.vendorID && header[3] == properties.deviceID &&
           std::memcmp(data.data() + 16u, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

inline std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

// Written beside and renamed over, so a crash never leaves half a file.
inline bool write_file(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

} // namespace sarusaru::shell::pipeline_store
