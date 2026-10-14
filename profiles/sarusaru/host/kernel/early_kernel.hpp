#pragma once

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sarusaru::kernel {

using ThreadUid = std::int32_t;

inline constexpr std::uint32_t kIllegalEntry = 0x80020192u;
inline constexpr std::uint32_t kIllegalPriority = 0x80020193u;
inline constexpr std::uint32_t kIllegalStackSize = 0x80020194u;
inline constexpr std::uint32_t kNoMemory = 0x80020190u;
inline constexpr std::uint32_t kIllegalArgument = 0x800200D2u;
inline constexpr std::uint32_t kIllegalMemblockType = 0x800200D8u;
inline constexpr std::uint32_t kIllegalMemblock = 0x800201B6u;
inline constexpr std::uint32_t kThreadExitStub = 0x08800000u;
inline constexpr std::uint32_t kPowerInvalidCallback = 0x80000100u;
inline constexpr std::uint32_t kPowerInvalidSlot = 0x80000102u;
inline constexpr std::uint32_t kPowerSlotsFull = 0x80000022u;
inline constexpr std::uint32_t kUnknownThread = 0x80020198u;
inline constexpr std::uint32_t kDormant = 0x800201A2u;

// Thread construction, stack ownership and cooperative scheduling: priority
// preemption on start/wakeup, sleep/wakeup, timed waits and exit.
//
// Guest time is virtual and deterministic. Guest code runs in zero time; the
// clock only advances when no thread is ready, jumping to the earliest timed
// wait. A thread that never blocks therefore never sees time pass.
// Synchronization objects are deliberately not modeled yet.
class EarlyKernel {
public:
    enum class ThreadState { Dormant, Ready, Running, Sleeping, Waiting, Exited };
    // Produces the blocked call's return value when a timed wait ends.
    using WakeHandler = std::function<std::uint32_t()>;
    void install(psprecomp::Runtime &runtime, std::uint32_t user_memory_start, std::uint32_t user_memory_top);
    void begin_main_thread(psprecomp::AllegrexContext &context, std::string name,
                           std::uint32_t priority);

    [[nodiscard]] ThreadUid create_thread(const std::string &name, std::uint32_t entry,
                                          std::uint32_t priority, std::uint32_t stack_size,
                                          std::uint32_t attributes, std::uint32_t gp);
    [[nodiscard]] std::int32_t start_thread(psprecomp::AllegrexContext &context, ThreadUid uid,
                                            std::uint32_t argument_size, std::uint32_t argument_address);
    void exit_current_thread(psprecomp::AllegrexContext &context);
    // Called from inside an import: a blocked caller resumes at its return address.
    void sleep_current_thread(psprecomp::AllegrexContext &context);
    [[nodiscard]] std::int32_t wakeup_thread(psprecomp::AllegrexContext &context, ThreadUid uid);
    void wait_current_until(psprecomp::AllegrexContext &context, std::uint64_t deadline_us, WakeHandler on_wake);
    void delay_current_thread(psprecomp::AllegrexContext &context, std::uint32_t micros);
    [[nodiscard]] std::uint64_t now_us() const noexcept { return now_us_; }
    [[nodiscard]] std::int32_t allocate_partition_memory(const std::string &name, std::uint32_t type,
                                                         std::uint32_t size, std::uint32_t address_or_alignment);
    [[nodiscard]] std::int32_t free_partition_memory(std::int32_t uid);
    [[nodiscard]] std::uint32_t block_head_address(std::int32_t uid) const noexcept;

    struct ThreadInfo {
        ThreadUid uid{};
        std::string name;
        std::uint32_t entry{};
        std::uint32_t priority{};
        std::uint32_t stack_bottom{};
        std::uint32_t stack_size{};
        std::uint32_t attributes{};
        std::uint32_t gp{};
        std::uint32_t control_block{};
        psprecomp::AllegrexContext context{};
        ThreadState state{ThreadState::Dormant};
        std::uint64_t ready_sequence{};
        std::uint32_t wakeup_count{};
        std::uint64_t wake_at_us{};
        std::uint64_t wait_sequence{};
        WakeHandler on_wake;
    };
    [[nodiscard]] const ThreadInfo *thread(ThreadUid uid) const noexcept;
    [[nodiscard]] ThreadUid current_uid() const noexcept { return current_uid_; }

    struct CallbackInfo {
        std::string name;
        std::uint32_t function{};
        std::uint32_t common{};
        ThreadUid owner{};
        bool registered_as_exit{};
    };
    [[nodiscard]] std::int32_t create_callback(const std::string &name, std::uint32_t function, std::uint32_t common);
    [[nodiscard]] const CallbackInfo *callback(std::int32_t uid) const noexcept;
    [[nodiscard]] std::int32_t register_exit_callback(std::int32_t uid);
    [[nodiscard]] std::int32_t register_power_callback(std::int32_t slot, std::int32_t callback_uid);
    [[nodiscard]] std::int32_t register_umd_callback(std::int32_t callback_uid);
    [[nodiscard]] std::int32_t umd_callback() const noexcept { return umd_callback_uid_; }
    [[nodiscard]] bool interrupts_enabled() const noexcept { return interrupts_enabled_; }
    [[nodiscard]] std::uint32_t suspend_interrupts() noexcept;
    void resume_interrupts(bool enabled) noexcept { interrupts_enabled_ = enabled; }

private:
    psprecomp::Runtime *runtime_{};
    std::uint32_t next_stack_top_{};
    ThreadUid next_uid_{0x100};
    ThreadUid current_uid_{};
    std::uint64_t next_ready_sequence_{1u};
    std::uint64_t now_us_{};
    std::map<ThreadUid, ThreadInfo> threads_;

    struct FreeRange { std::uint32_t address{}; std::uint32_t size{}; };
    struct MemoryBlock { std::string name; std::uint32_t address{}; std::uint32_t size{}; };
    std::vector<FreeRange> free_ranges_;
    std::map<std::int32_t, MemoryBlock> blocks_;
    std::map<std::int32_t, CallbackInfo> callbacks_;
    std::int32_t exit_callback_uid_{};
    std::array<std::int32_t, 16> power_callback_slots_{};
    std::int32_t umd_callback_uid_{};
    bool interrupts_enabled_{true};

    void make_ready(ThreadInfo &thread) noexcept;
    [[nodiscard]] ThreadInfo *best_ready_thread() noexcept;
    void switch_to(psprecomp::AllegrexContext &context, ThreadUid uid, ThreadInfo &thread);
    void save_blocked_context(ThreadInfo &thread, const psprecomp::AllegrexContext &context,
                              std::uint32_t result) noexcept;
    void run_next_ready(psprecomp::AllegrexContext &context, const char *idle_reason);
    void wake_due_threads();
    void prepare_thread_stack(ThreadInfo &thread);
    [[nodiscard]] std::int32_t allocate_block(const std::string &name, std::uint32_t type,
                                              std::uint32_t size, std::uint32_t address_or_alignment);
};

void register_early_threadman(EarlyKernel &kernel, psprecomp::Runtime &runtime);

} // namespace sarusaru::kernel
