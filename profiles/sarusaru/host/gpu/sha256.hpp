#pragma once

// SHA-256 for the texture hashes the renderer takes on its own thread
// (gpu::identify_texture, translation and button-prompt matches): over data
// given in pieces, with the CPU's SHA-256 instructions where there are any.
// The same digests as psprecomp::sha256, whose header the game module's ABI
// covers, so it stays as it is.
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace sarusaru::gpu {

class Sha256 {
public:
    void update(std::span<const std::uint8_t> bytes);
    // The digest of everything given so far; the hasher is spent afterwards.
    [[nodiscard]] std::array<std::uint8_t, 32> digest();
    // The digest as 64 lowercase hex digits.
    [[nodiscard]] std::string hex();

private:
    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{};
    std::uint64_t length_{};
};

// The portable rounds only, for tests to compare against.
[[nodiscard]] std::string sha256_portable(std::span<const std::uint8_t> bytes);

} // namespace sarusaru::gpu
