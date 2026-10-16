#pragma once

#include "save_state/ppst.hpp"

#include <array>
#include <map>
#include <utility>

namespace sarusaru::save_state {
struct PspAllocation {
    std::uint32_t address{}, size{};
    bool allocated{};
    std::array<std::uint8_t, 32> tag{};
};
struct PspAllocator {
    std::vector<PspAllocation> blocks;
    std::uint32_t address{}, size{}, grain{};
};
struct PspReadyQueue {
    std::uint32_t capacity{};
    std::vector<std::uint32_t> threads;
};
// PPSSPP 1.20.4's Kernel Modules records. All CPU words retain
// their raw bits; event/action IDs refer to the corresponding PPSSPP tables.
struct PspScheduler {
    std::array<std::uint32_t, 214> interrupt_context{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pending_interrupts;
    std::uint32_t interrupts_enabled{}, interrupted_thread{};
    bool in_interrupt{};
    std::array<PspAllocator, 3> allocators; // kernel, user, volatile
    std::uint32_t vpl_timer{}, fpl_timer{}, memory_flags{}, sdk_version{}, compiler_version{};
    std::array<bool, 16> tls_indexes{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> tls_thread_checks;
    // Debug maps are structurally validated but do not affect guest execution.
    std::vector<std::uint8_t> memory_debug_record;
    std::uint32_t callback_depth{}, callback_thread{}, ready_callbacks{};
    std::array<std::uint32_t, 7> return_stubs{}; // idle, thread, callback, interrupt, stack, module, HLE
    std::uint32_t current_thread{};
    std::vector<std::uint32_t> threads;
    std::array<std::uint32_t, 2> idle_threads{};
    bool dispatch_enabled{};
    std::array<PspReadyQueue, 128> ready_queues;
    std::uint32_t wakeup_event{}, thread_end_event{}, after_mips_action{}, after_callback_action{};
    std::map<std::uint32_t, std::uint64_t> paused_delays;
    std::map<std::uint32_t, std::vector<std::uint32_t>> thread_event_handlers;
    std::vector<std::uint32_t> pending_delete_threads;
    std::uint32_t alarm_timer{}, triggered_alarm{}, vtimer_timer{}, running_vtimer{};
    std::vector<std::uint32_t> vtimers;
    std::uint32_t event_flag_timer{}, mailbox_timer{}, after_module_action{};
    std::vector<std::uint32_t> loaded_modules;
    std::uint32_t message_pipe_timer{}, mutex_timer{}, lightweight_mutex_timer{}, semaphore_timer{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> held_mutexes;
    std::uint64_t start_time{};
    // Starts at HLE Modules, after the closed Kernel Modules section.
    std::vector<std::uint8_t> remainder;
};
[[nodiscard]] PspScheduler decode_psp_scheduler(std::span<const std::uint8_t> kernel_modules);
[[nodiscard]] std::vector<std::uint8_t> encode_psp_scheduler(const PspScheduler &scheduler);
} // namespace sarusaru::save_state
