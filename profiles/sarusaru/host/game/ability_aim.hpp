#pragma once

#include "psprecomp/guest_memory.hpp"

namespace sarusaru::game {

// UCJS10060's first-person ability camera. Observed in slot 2 when the
// laser monkey toggles aim: stage camera mode 5, with its aim-lock flag set.
// Ordinary following uses modes 0/1. Scripted cameras and transitions must
// keep their own input behavior. No host history is needed after state loads.
[[nodiscard]] inline bool ability_aiming(const psprecomp::GuestMemory &memory) {
    constexpr std::uint32_t system_pointer = 0x08A82B24u;
    const auto system = memory.load32(system_pointer);
    if ((system & 3u) != 0 || !memory.contains(system, 24u)) return false;
    const auto camera = memory.load32(system + 20u);
    return (camera & 3u) == 0 && memory.contains(camera, 0x2e0u) &&
        memory.load32(camera + 0xfcu) == 1u && memory.load32(camera + 0x228u) == 5u &&
        memory.load32(camera + 0x2dcu) == 0u && memory.load8(camera + 0x2abu) == 1u;
}

} // namespace sarusaru::game
