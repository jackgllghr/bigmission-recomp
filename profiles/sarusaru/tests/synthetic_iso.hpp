#pragma once

// Builds small invented ISO9660 images for tests; no game data is involved.

#include "umd/disc_image.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace sarusaru::test {
using sarusaru::umd::kSectorSize;

inline void both_endian(std::vector<std::uint8_t> &image, std::size_t offset, std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        image[offset + i] = static_cast<std::uint8_t>(value >> (8u * i));
        image[offset + 2u * width - 1u - i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

inline std::size_t record(std::vector<std::uint8_t> &image, std::size_t offset, std::string_view name,
                   std::uint32_t sector, std::uint32_t size, bool directory) {
    const auto length = static_cast<std::uint8_t>((33u + name.size() + 1u) & ~1u);
    image[offset] = length;
    both_endian(image, offset + 2u, sector, 4u);
    both_endian(image, offset + 10u, size, 4u);
    image[offset + 25u] = directory ? 2u : 0u;
    both_endian(image, offset + 28u, 1u, 2u);
    image[offset + 32u] = static_cast<std::uint8_t>(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) image[offset + 33u + i] = static_cast<std::uint8_t>(name[i]);
    return offset + length;
}

// Invented 24-sector image: /UMD_DATA.BIN and /PSP_GAME/DATA.BIN (spans two sectors).
inline std::vector<std::uint8_t> synthetic_image() {
    constexpr std::uint32_t sectors = 24u;
    std::vector<std::uint8_t> image(sectors * kSectorSize, 0u);
    const std::size_t pvd = 16u * kSectorSize;
    image[pvd] = 1u;
    for (std::size_t i = 0; i < 5u; ++i) image[pvd + 1u + i] = static_cast<std::uint8_t>("CD001"[i]);
    image[pvd + 6u] = 1u;
    both_endian(image, pvd + 80u, sectors, 4u);
    both_endian(image, pvd + 128u, kSectorSize, 2u);
    record(image, pvd + 156u, std::string_view("\0", 1), 18u, kSectorSize, true);

    auto offset = record(image, 18u * kSectorSize, std::string_view("\0", 1), 18u, kSectorSize, true);
    offset = record(image, offset, std::string_view("\1", 1), 18u, kSectorSize, true);
    offset = record(image, offset, "PSP_GAME", 19u, kSectorSize, true);
    record(image, offset, "UMD_DATA.BIN;1", 20u, 11u, false);
    offset = record(image, 19u * kSectorSize, std::string_view("\0", 1), 19u, kSectorSize, true);
    offset = record(image, offset, std::string_view("\1", 1), 18u, kSectorSize, true);
    record(image, offset, "DATA.BIN;1", 21u, 3000u, false);

    for (std::size_t i = 0; i < 11u; ++i) image[20u * kSectorSize + i] = static_cast<std::uint8_t>("TEST-00001|"[i]);
    for (std::size_t i = 0; i < 3000u; ++i) image[21u * kSectorSize + i] = static_cast<std::uint8_t>(i * 7u);
    return image;
}

inline std::filesystem::path write_image(const std::vector<std::uint8_t> &image, const char *name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(image.data()),
                                                static_cast<std::streamsize>(image.size()));
    return path;
}

} // namespace sarusaru::test
