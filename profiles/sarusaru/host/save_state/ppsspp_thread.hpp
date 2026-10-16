#pragma once

#include "save_state/machine.hpp"

#include <map>
#include <utility>

namespace sarusaru::save_state {
// The blocked-thread context differs from MIPSState: it has no delay-slot
// flag/next PC/LL bit, and includes a sixth, reserved union word.
struct PspThreadContext {
    std::array<std::uint32_t, 32> gpr{}, fpr{};
    std::array<std::uint32_t, 128> vfpu{};
    std::array<std::uint32_t, 16> vfpu_control{};
    std::uint32_t pc{}, lo{}, hi{}, fcr31{}, fp_condition{}, reserved{};
    [[nodiscard]] psprecomp::AllegrexContext context() const;
    // A delayed branch cannot be represented by this PPSSPP record. The
    // running thread's authoritative CPU belongs in the machine prefix.
    void set_context(const psprecomp::AllegrexContext &context);
};
struct PspThread {
    std::uint32_t native_size{104};
    std::array<std::uint8_t, 32> name{};
    std::uint32_t attributes{}, status{}, entry{}, initial_stack{}, stack_size{}, gp{};
    std::uint32_t initial_priority{}, priority{}, wait_type{}, wait_id{}, wakeup_count{}, exit_status{};
    std::uint64_t run_clocks{};
    std::uint32_t interrupt_preempts{}, thread_preempts{}, releases{};
    std::uint32_t wait_value{}, timeout_address{}, module_uid{};
    bool processing_callbacks{};
    std::uint32_t mips_call_id{}, callback_id{};
    PspThreadContext cpu;
    std::vector<std::uint32_t> callbacks, pending_calls;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pushed_stacks;
    std::pair<std::uint32_t, std::uint32_t> current_stack{};
    std::vector<std::uint32_t> end_waiters;
    std::map<std::uint32_t, std::uint64_t> paused_waits;
};
[[nodiscard]] PspThread decode_psp_thread(std::span<const std::uint8_t> section);
[[nodiscard]] std::vector<std::uint8_t> encode_psp_thread(const PspThread &thread);
} // namespace sarusaru::save_state
