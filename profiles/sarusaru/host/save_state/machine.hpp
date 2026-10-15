#pragma once

#include "save_state/ppst.hpp"
#include "psprecomp/allegrex_context.hpp"

#include <array>

namespace sarusaru::save_state {

// The machine prefix schema used by PPSSPP 1.20.4. No native object layouts or
// host pointers occur on the wire. The remaining kernel/HLE/filesystem records
// are retained verbatim here; decoding this prefix does not make them runnable.
struct PpstCpu {
    std::array<std::uint32_t, 32> gpr{}, fpr{};
    std::array<std::uint32_t, 128> vfpu{};
    std::array<std::uint32_t, 16> vfpu_control{};
    std::uint32_t pc{}, next_pc{}, downcount{}, hi{}, lo{}, fp_condition{}, fcr31{};
    bool in_delay_slot{};
    std::uint32_t load_linked{};
    std::uint32_t debug_count{};
    bool jit_start_default{}, jit_has_set_rounding{};

    [[nodiscard]] psprecomp::AllegrexContext context() const;
    void set_context(const psprecomp::AllegrexContext &context);
};

struct TimingEvent {
    std::uint64_t ticks{}, userdata{};
    std::uint32_t type{};
    bool operator==(const TimingEvent &) const = default;
};

struct MachinePrefix {
    std::uint32_t state_generation{}, savedata_generation{};
    std::string initial_creator;
    std::uint32_t psp_model{};
    std::vector<std::uint8_t> ram, vram, scratchpad;
    std::uint32_t event_type_count{};
    std::vector<TimingEvent> events;
    std::uint32_t cpu_hz{}, slice_length{};
    std::uint64_t global_ticks{}, idle_ticks{}, last_time_ticks{}, last_time_us{};
    std::uint32_t memory_stick_state{}, memory_stick_fat_state{};
    std::uint64_t memory_stick_initial_free{}, memory_stick_inserted_at{};
    bool memory_stick_needs_assign{};
    PpstCpu cpu;
    // Includes HLE, kernel, achievements, filesystems, and SaveStart's cookie.
    std::vector<std::uint8_t> remainder;
};

[[nodiscard]] MachinePrefix decode_machine_prefix(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_machine_prefix(const MachinePrefix &machine);

} // namespace sarusaru::save_state
