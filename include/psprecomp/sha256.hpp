#pragma once

#include <filesystem>
#include <cstdint>
#include <span>
#include <string>

namespace psprecomp {

[[nodiscard]] std::string sha256(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string sha256_file(const std::filesystem::path &path);

} // namespace psprecomp
