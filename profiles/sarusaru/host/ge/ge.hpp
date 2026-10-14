#pragma once

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sarusaru::ge {

inline constexpr std::uint32_t kEdramAddress = 0x04000000u;
inline constexpr std::uint32_t kEdramSize = 0x00200000u;
inline constexpr std::uint32_t kInvalidId = 0x80000100u;
inline constexpr std::uint32_t kInvalidPointer = 0x80000103u;
inline constexpr std::uint32_t kInvalidMode = 0x80000107u;
inline constexpr std::uint32_t kOutOfMemory = 0x80000022u;
// Queue IDs are opaque handles: slot index XOR this value.
inline constexpr std::uint32_t kListIdMagic = 0x35000000u;
inline constexpr std::uint32_t kMaxLists = 64u;
inline constexpr std::uint32_t kMaxCallbacks = 16u;
inline constexpr std::uint32_t kCallStackDepth = 32u;

// sceGeListSync / sceGeDrawSync peek results (pspge.h).
enum class ListState : std::uint32_t { Completed = 0, Queued = 1, Drawing = 2, Stalling = 3, Paused = 4 };

// Headless graphics engine. Display lists run in zero guest time when queued
// or unstalled: control flow is followed exactly, finish and signal callbacks
// run as GE interrupts, and every other command's latest value is kept as GE
// state for a future renderer. Nothing is rasterised. Bounding-box tests are
// treated as visible, so BJUMP never branches.
class Ge {
public:
    struct Stats {
        std::uint64_t commands{};
        std::uint64_t primitives{};
        std::uint64_t lists_completed{};
    };

    explicit Ge(kernel::EarlyKernel &kernel) noexcept : kernel_(&kernel) {}

    [[nodiscard]] std::int32_t set_callback(std::uint32_t data_address, std::uint32_t gp);
    [[nodiscard]] std::int32_t unset_callback(std::uint32_t id);
    // Queues a list and runs what it can. The caller then completes the
    // import with kernel.return_with_preemption so GE interrupts run at once.
    [[nodiscard]] std::int32_t enqueue(std::uint32_t list, std::uint32_t stall, std::int32_t callback,
                                       std::uint32_t argument, bool head);
    [[nodiscard]] std::int32_t update_stall(std::uint32_t id, std::uint32_t stall);
    // Mode 0 waits for completion; mode 1 returns the ListState.
    void list_sync(psprecomp::AllegrexContext &context, std::uint32_t id, std::uint32_t mode);
    void draw_sync(psprecomp::AllegrexContext &context, std::uint32_t mode);

    [[nodiscard]] std::optional<ListState> list_state(std::uint32_t id) const noexcept;
    [[nodiscard]] ListState queue_state() const noexcept;
    [[nodiscard]] std::uint32_t state(std::uint8_t command) const noexcept { return registers_[command]; }
    [[nodiscard]] const Stats &stats() const noexcept { return stats_; }

private:
    struct Callback {
        std::uint32_t signal_function{}, signal_argument{}, finish_function{}, finish_argument{}, gp{};
    };
    struct CallFrame { std::uint32_t return_address{}, offset{}; };
    struct List {
        bool used{};
        bool in_use{};
        ListState state{ListState::Queued};
        std::uint32_t pc{}, stall{}, offset{};
        std::int32_t callback{-1};
        std::vector<CallFrame> stack;
        std::vector<kernel::ThreadUid> waiters;
    };

    kernel::EarlyKernel *kernel_;
    std::array<std::optional<Callback>, kMaxCallbacks> callbacks_{};
    std::array<List, kMaxLists> lists_{};
    std::deque<std::uint32_t> queue_;  // Slot indices, front is the active list.
    std::vector<kernel::ThreadUid> draw_waiters_;
    std::array<std::uint32_t, 256> registers_{};
    std::uint32_t base_{};
    Stats stats_;

    [[nodiscard]] std::optional<std::uint32_t> slot(std::uint32_t id) const noexcept;
    void run();
    // Executes the active list until it stalls, pauses or completes; false if it cannot continue.
    bool execute(std::uint32_t index);
    void complete(std::uint32_t index);
    void raise(std::uint32_t function, std::uint32_t argument, std::uint32_t gp, std::uint32_t value,
               std::uint32_t pc, std::string label, std::function<void(std::uint32_t)> on_return = nullptr);
    [[nodiscard]] std::uint32_t target(std::uint32_t data, std::uint32_t offset) const noexcept;
};

void register_ge(Ge &ge, kernel::EarlyKernel &kernel, psprecomp::Runtime &runtime);

} // namespace sarusaru::ge
