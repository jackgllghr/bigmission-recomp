#pragma once

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sarusaru::save_state { class KernelSnapshot; }

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
inline constexpr std::uint32_t kGuestCallReturnStub = 0x08800008u;
inline constexpr std::uint32_t kPowerInvalidCallback = 0x80000100u;
inline constexpr std::uint32_t kPowerInvalidSlot = 0x80000102u;
inline constexpr std::uint32_t kPowerSlotsFull = 0x80000022u;
inline constexpr std::uint32_t kUnknownThread = 0x80020198u;
inline constexpr std::uint32_t kUnknownCallback = 0x800201A1u;
inline constexpr std::uint32_t kDormant = 0x800201A2u;
inline constexpr std::uint32_t kIllegalAttribute = 0x80020191u;
inline constexpr std::uint32_t kIllegalThread = 0x80020197u;
inline constexpr std::uint32_t kNotDormant = 0x800201A4u;
inline constexpr std::uint32_t kCanNotWait = 0x800201A7u;
inline constexpr std::uint32_t kInterruptsDisabled = 0x80020064u;
inline constexpr std::uint32_t kWaitTimeout = 0x800201A8u;
inline constexpr std::uint32_t kThreadTerminated = 0x800201ACu;
inline constexpr std::uint32_t kWaitDelete = 0x800201B5u;
inline constexpr std::uint32_t kThreadAttrVfpu = 0x00004000u;
inline constexpr std::uint64_t kNoDeadline = ~std::uint64_t{0};

// Thread construction, stack ownership and cooperative scheduling: priority
// preemption on start/wakeup, sleep/wakeup, timed waits and exit.
//
// Guest time is virtual and deterministic. Guest code runs in zero time; the
// clock only advances when no thread is ready, jumping to the earliest timed
// wait. A thread that never blocks therefore never sees time pass.
// Synchronization objects are deliberately not modeled yet.
class EarlyKernel {
public:
    enum class ThreadState { Dormant, Ready, Running, Sleeping, Waiting };
    // Guest continuations carry values, not captured host pointers. Modules
    // install handlers once; snapshots retain only these stable action records.
    enum class ActionKind : std::uint32_t {
        None, ReturnValue, ControllerRead, AudioOutput, GeResumeList, SemaphoreTimeout, EventFlagTimeout,
    };
    struct Action {
        ActionKind kind{ActionKind::None};
        std::array<std::uint32_t, 4> arguments{};
        bool operator==(const Action &) const = default;
    };
    using ActionHandler = std::function<std::uint32_t(const Action &)>;
    void set_action_handler(ActionKind kind, ActionHandler handler);
    void install(psprecomp::Runtime &runtime, std::uint32_t user_memory_start, std::uint32_t user_memory_top);
    void begin_main_thread(psprecomp::AllegrexContext &context, std::string name,
                           std::uint32_t priority);

    [[nodiscard]] ThreadUid create_thread(const std::string &name, std::uint32_t entry,
                                          std::uint32_t priority, std::uint32_t stack_size,
                                          std::uint32_t attributes, std::uint32_t gp);
    [[nodiscard]] std::int32_t start_thread(psprecomp::AllegrexContext &context, ThreadUid uid,
                                            std::uint32_t argument_size, std::uint32_t argument_address);
    // Ends the current thread with `status` (it becomes dormant) and runs the next one.
    void exit_current_thread(psprecomp::AllegrexContext &context, std::uint32_t status, bool delete_thread = false);
    [[nodiscard]] std::int32_t delete_thread(ThreadUid uid);
    [[nodiscard]] std::int32_t terminate_thread(psprecomp::AllegrexContext &context, ThreadUid uid);
    [[nodiscard]] std::int32_t thread_exit_status(ThreadUid uid) const noexcept;
    void wait_thread_end(psprecomp::AllegrexContext &context, ThreadUid uid, std::uint32_t timeout_address);
    [[nodiscard]] std::int32_t change_current_thread_attributes(std::uint32_t clear, std::uint32_t set) noexcept;

    // Called from inside an import: a blocked caller resumes at its return address.
    void sleep_current_thread(psprecomp::AllegrexContext &context, bool callbacks = false);
    [[nodiscard]] std::int32_t wakeup_thread(psprecomp::AllegrexContext &context, ThreadUid uid);
    // Values match PSP wait identities; diagnostic labels never determine
    // which object or subsystem a restored thread is waiting for.
    enum class WaitType : std::uint32_t {
        None = 0, Sleep = 1, Delay = 2, Semaphore = 3, EventFlag = 4, ThreadEnd = 9,
        Audio = 10, Umd = 11, Vblank = 12, Controller = 15, GeDraw = 17, GeList = 18,
        Custom = 0xFFFFFFFFu,
    };
    struct WaitIdentity {
        WaitType type{WaitType::None};
        std::uint32_t object{}, value{};
        bool operator==(const WaitIdentity &) const = default;
    };
    void wait_current_until(psprecomp::AllegrexContext &context, std::uint64_t deadline_us, Action on_wake,
                            WaitIdentity identity);
    void wait_current_until(psprecomp::AllegrexContext &context, std::uint64_t deadline_us, Action on_wake) {
        wait_current_until(context, deadline_us, on_wake, {WaitType::Custom});
    }
    void delay_current_thread(psprecomp::AllegrexContext &context, std::uint32_t micros, bool callbacks = false);
    [[nodiscard]] std::uint64_t now_us() const noexcept { return now_us_; }

    // Waits on kernel objects. A non-null timeout address holds microseconds;
    // on timeout it is zeroed, `on_timeout` runs and the call fails with
    // kWaitTimeout. On release it receives the time left.
    struct WaitRequest {
        std::string label;
        std::uint64_t deadline_us{kNoDeadline};
        std::uint32_t timeout_address{};
        Action on_timeout;
        ThreadUid end_of{};
        // A *CB wait: notified callbacks run on the thread, then the wait resumes.
        bool callbacks{};
        WaitIdentity identity{WaitType::Custom};
    };
    [[nodiscard]] WaitRequest object_wait(std::string label, std::uint32_t timeout_address,
                                          Action on_timeout) const;
    [[nodiscard]] WaitRequest object_wait(std::string label, std::uint32_t timeout_address) const {
        return object_wait(std::move(label), timeout_address, Action{});
    }
    void block_current(psprecomp::AllegrexContext &context, WaitRequest request);
    // Ends `uid`'s object wait with `result`; false if it is no longer waiting.
    bool release_wait(ThreadUid uid, std::uint32_t result);
    // Completes the current call with `result`, first switching to a released
    // thread of higher priority if there is one.
    void return_with_preemption(psprecomp::AllegrexContext &context, std::uint32_t result);
    [[nodiscard]] std::int32_t allocate_uid() noexcept { return next_uid_++; }

    // Guest code run in interrupt context: a priority-0 pseudo-thread with its
    // own stack runs queued calls in order and returns through
    // kGuestCallReturnStub. It cannot block; waits fail with kCanNotWait.
    struct GuestCall {
        std::uint32_t address{};
        std::array<std::uint32_t, 4> arguments{};
        std::uint32_t gp{};
        std::string label;
        Action on_return;
    };
    // The caller must then let the interrupt run, e.g. via return_with_preemption.
    void queue_interrupt(GuestCall call);
    [[nodiscard]] bool in_interrupt() const noexcept { return current_uid_ != 0 && current_uid_ == interrupt_uid_; }
    // Called at every vblank the virtual clock crosses while `active`.
    void set_vblank_handler(std::function<void()> handler) { vblank_handler_ = std::move(handler); }
    void set_vblank_active(bool active) noexcept { vblank_active_ = active; }
    // Host hook called at every vblank the virtual clock crosses (presenting a
    // frame, polling input, pacing to real time). Unset, timing is unchanged.
    void set_frame_observer(std::function<void()> observer) { frame_observer_ = std::move(observer); }
    void guest_call_returned(psprecomp::AllegrexContext &context);
    [[nodiscard]] psprecomp::Runtime &runtime() noexcept { return *runtime_; }
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
        Action on_wake;
        // Callbacks run on this thread: the interrupted context, the
        // callbacks still to run, and what to do after the last one returns.
        std::optional<psprecomp::AllegrexContext> callback_saved;
        std::deque<std::int32_t> callback_queue;
        enum class CallbackResume : std::uint32_t { None, Check, Wait, Sleep };
        CallbackResume after_callbacks{CallbackResume::None};
        // The *CB wait (or sleep) to resume after a callback interrupts it.
        std::optional<WaitRequest> resume_wait;
        bool resume_sleep{};
        std::uint32_t exit_status{kDormant};
        std::int32_t stack_block{};
        std::uint32_t timeout_address{};
        ThreadUid end_of{};
        std::string wait_label;
        WaitIdentity wait_identity;
    };
    [[nodiscard]] const ThreadInfo *thread(ThreadUid uid) const noexcept;
    [[nodiscard]] ThreadUid current_uid() const noexcept { return current_uid_; }

    struct CallbackInfo {
        std::string name;
        std::uint32_t function{};
        std::uint32_t common{};
        ThreadUid owner{};
        bool registered_as_exit{};
        std::uint32_t notify_count{};
        std::uint32_t notify_argument{};
    };
    [[nodiscard]] std::int32_t create_callback(const std::string &name, std::uint32_t function, std::uint32_t common);
    [[nodiscard]] const CallbackInfo *callback(std::int32_t uid) const noexcept;
    [[nodiscard]] std::int32_t register_exit_callback(std::int32_t uid);
    [[nodiscard]] std::int32_t register_power_callback(std::int32_t slot, std::int32_t callback_uid);
    [[nodiscard]] std::int32_t register_umd_callback(std::int32_t callback_uid);
    [[nodiscard]] std::int32_t delete_callback(std::int32_t uid);
    // Records a notification. If the owner is in a *CB wait it becomes ready to
    // run the callback; the caller should then return_with_preemption.
    void notify_callback(std::int32_t uid, std::uint32_t argument);
    // sceKernelCheckCallback: runs the current thread's notified callbacks,
    // then returns 1 if any ran, else 0.
    void check_callbacks(psprecomp::AllegrexContext &context);
    [[nodiscard]] std::int32_t umd_callback() const noexcept { return umd_callback_uid_; }
    [[nodiscard]] bool interrupts_enabled() const noexcept { return interrupts_enabled_; }
    [[nodiscard]] std::uint32_t suspend_interrupts() noexcept;
    void resume_interrupts(bool enabled) noexcept { interrupts_enabled_ = enabled; }
    // While dispatch is suspended the current thread keeps the CPU: nothing
    // preempts it and its waits fail with kCanNotWait.
    [[nodiscard]] std::int32_t suspend_dispatch() noexcept;
    void resume_dispatch(psprecomp::AllegrexContext &context, std::uint32_t state);
    [[nodiscard]] bool dispatch_enabled() const noexcept { return dispatch_enabled_; }

    struct FreeRange { std::uint32_t address{}; std::uint32_t size{}; };
    struct MemoryBlock { std::string name; std::uint32_t address{}; std::uint32_t size{}; };
    struct State {
        std::uint32_t next_stack_top{};
        ThreadUid next_uid{}, current_uid{}, interrupt_uid{};
        std::uint64_t next_ready_sequence{}, now_us{};
        std::deque<GuestCall> interrupt_calls;
        std::map<ThreadUid, ThreadInfo> threads;
        std::vector<FreeRange> free_ranges;
        std::map<std::int32_t, MemoryBlock> blocks;
        std::map<std::int32_t, CallbackInfo> callbacks;
        std::int32_t exit_callback_uid{}, umd_callback_uid{};
        std::array<std::int32_t, 16> power_callback_slots{};
        bool interrupts_enabled{}, dispatch_enabled{}, vblank_active{};
    };
    // Called after all native guest frames have unwound. Host hooks and action
    // bindings remain installed on this kernel when a detached state is applied.
    [[nodiscard]] State snapshot() const;
    void validate_state(const State &state) const;
    void restore(State state);

private:
    friend class sarusaru::save_state::KernelSnapshot;
    void apply_state(State &state) noexcept;
    psprecomp::Runtime *runtime_{};
    std::uint32_t next_stack_top_{};
    ThreadUid next_uid_{0x100};
    ThreadUid current_uid_{};
    std::uint64_t next_ready_sequence_{1u};
    std::uint64_t now_us_{};
    ThreadUid interrupt_uid_{};
    std::deque<GuestCall> interrupt_calls_;
    std::function<void()> vblank_handler_;
    std::function<void()> frame_observer_;
    bool vblank_active_{};
    std::map<ThreadUid, ThreadInfo> threads_;

    std::vector<FreeRange> free_ranges_;
    std::map<std::int32_t, MemoryBlock> blocks_;
    std::map<std::int32_t, CallbackInfo> callbacks_;
    std::int32_t exit_callback_uid_{};
    std::array<std::int32_t, 16> power_callback_slots_{};
    std::int32_t umd_callback_uid_{};
    bool interrupts_enabled_{true};
    bool dispatch_enabled_{true};
    std::map<ActionKind, ActionHandler> action_handlers_;
    std::uint32_t perform_action(const Action &action);

    void make_ready(ThreadInfo &thread) noexcept;
    [[nodiscard]] ThreadInfo *best_ready_thread() noexcept;
    void switch_to(psprecomp::AllegrexContext &context, ThreadUid uid, ThreadInfo &thread);
    void save_blocked_context(ThreadInfo &thread, const psprecomp::AllegrexContext &context,
                              std::uint32_t result) noexcept;
    void run_next_ready(psprecomp::AllegrexContext &context, const char *idle_reason);
    void wake_due_threads();
    void start_interrupt_call(ThreadInfo &thread);
    [[nodiscard]] bool has_pending_callbacks(ThreadUid owner) const noexcept;
    // Runs the owner's notified callbacks starting from `saved` (a resumable
    // context), then calls `after` with the restored context.
    void start_callbacks(ThreadInfo &thread, const psprecomp::AllegrexContext &saved,
                         ThreadInfo::CallbackResume after);
    void finish_callbacks(ThreadInfo &thread, psprecomp::AllegrexContext &context);
    void start_next_callback(ThreadInfo &thread);
    [[nodiscard]] bool refuse_wait_in_interrupt(psprecomp::AllegrexContext &context);
    void end_thread(ThreadInfo &thread, std::uint32_t status);
    [[nodiscard]] std::string describe_blocked() const;
    void prepare_thread_stack(ThreadInfo &thread);
    [[nodiscard]] std::int32_t allocate_block(const std::string &name, std::uint32_t type,
                                              std::uint32_t size, std::uint32_t address_or_alignment);
};

void register_early_threadman(EarlyKernel &kernel, psprecomp::Runtime &runtime);

} // namespace sarusaru::kernel
