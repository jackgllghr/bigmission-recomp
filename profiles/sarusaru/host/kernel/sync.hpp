#pragma once

#include "kernel/early_kernel.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace sarusaru::kernel {

inline constexpr std::uint32_t kIllegalMode = 0x80020195u;
inline constexpr std::uint32_t kUnknownSemaphore = 0x80020199u;
inline constexpr std::uint32_t kUnknownEventFlag = 0x8002019Au;
inline constexpr std::uint32_t kSemaphoreOverflow = 0x800201AEu;
inline constexpr std::uint32_t kEventFlagMultipleWaiters = 0x800201B0u;
inline constexpr std::uint32_t kEventFlagIllegalPattern = 0x800201B1u;
inline constexpr std::uint32_t kIllegalCount = 0x800201BDu;

inline constexpr std::uint32_t kAttrPriorityQueue = 0x100u;
inline constexpr std::uint32_t kEventFlagWaitMultiple = 0x200u;
inline constexpr std::uint32_t kEventFlagWaitOr = 0x01u;
inline constexpr std::uint32_t kEventFlagClearAll = 0x10u;
inline constexpr std::uint32_t kEventFlagClearPattern = 0x20u;

// Semaphores and event flags on top of EarlyKernel's object waits. Waiters
// queue in arrival order, or by thread priority with kAttrPriorityQueue.
class SyncObjects {
public:
    explicit SyncObjects(EarlyKernel &kernel) noexcept : kernel_(&kernel) {}

    [[nodiscard]] std::int32_t create_semaphore(std::string name, std::uint32_t attributes, std::int32_t initial,
                                                std::int32_t maximum);
    void delete_semaphore(psprecomp::AllegrexContext &context, std::int32_t uid);
    void signal_semaphore(psprecomp::AllegrexContext &context, std::int32_t uid, std::int32_t count);
    void wait_semaphore(psprecomp::AllegrexContext &context, std::int32_t uid, std::int32_t count,
                        std::uint32_t timeout_address);
    [[nodiscard]] std::int32_t semaphore_count(std::int32_t uid) const noexcept;

    [[nodiscard]] std::int32_t create_event_flag(std::string name, std::uint32_t attributes, std::uint32_t pattern);
    void delete_event_flag(psprecomp::AllegrexContext &context, std::int32_t uid);
    void set_event_flag(psprecomp::AllegrexContext &context, std::int32_t uid, std::uint32_t bits);
    [[nodiscard]] std::int32_t clear_event_flag(std::int32_t uid, std::uint32_t bits) noexcept;
    void wait_event_flag(psprecomp::AllegrexContext &context, std::int32_t uid, std::uint32_t bits,
                         std::uint32_t mode, std::uint32_t out_address, std::uint32_t timeout_address);
    [[nodiscard]] std::uint32_t event_flag_pattern(std::int32_t uid) const noexcept;

private:
    struct Waiter {
        ThreadUid thread{};
        std::uint32_t value{};  // Semaphore count needed, or event-flag bits.
        std::uint32_t mode{};
        std::uint32_t out_address{};
    };
    struct Semaphore {
        std::string name;
        std::uint32_t attributes{};
        std::int32_t count{};
        std::int32_t maximum{};
        std::deque<Waiter> waiters;
    };
    struct EventFlag {
        std::string name;
        std::uint32_t attributes{};
        std::uint32_t pattern{};
        std::deque<Waiter> waiters;
    };

    EarlyKernel *kernel_;
    std::map<std::int32_t, Semaphore> semaphores_;
    std::map<std::int32_t, EventFlag> event_flags_;

    // Drops waiters whose thread stopped waiting (terminated) and orders the rest.
    void order_waiters(std::deque<Waiter> &waiters, std::uint32_t attributes) const;
    static bool satisfied(std::uint32_t pattern, std::uint32_t bits, std::uint32_t mode) noexcept;
    void finish_event_wait(EventFlag &flag, const Waiter &waiter);
};

void register_sync(SyncObjects &sync, psprecomp::Runtime &runtime);

} // namespace sarusaru::kernel
