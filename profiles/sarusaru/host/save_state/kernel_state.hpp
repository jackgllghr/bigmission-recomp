#pragma once

#include "kernel/early_kernel.hpp"
#include "save_state/ppst.hpp"

namespace sarusaru::save_state {
// Internal, pointer-free kernel representation for the state adapters. This
// section is not a PPSSPP kernel section and is not itself a .ppst save state.
[[nodiscard]] std::vector<std::uint8_t> encode_kernel_state(const kernel::EarlyKernel::State &state);
[[nodiscard]] kernel::EarlyKernel::State decode_kernel_state(std::span<const std::uint8_t> bytes);
} // namespace sarusaru::save_state
