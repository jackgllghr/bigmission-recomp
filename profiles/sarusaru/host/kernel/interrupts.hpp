#pragma once

#include "kernel/early_kernel.hpp"

#include <cstdint>
#include <map>
#include <utility>

namespace sarusaru::kernel {

inline constexpr std::uint32_t kIllegalInterruptCode = 0x80020065u;
inline constexpr std::uint32_t kHandlerAlreadyRegistered = 0x80020067u;
inline constexpr std::uint32_t kHandlerNotFound = 0x80020068u;
inline constexpr std::uint32_t kInterruptGe = 25u;
inline constexpr std::uint32_t kInterruptVblank = 30u;
inline constexpr std::uint32_t kSubInterruptCount = 16u;

// Sub-interrupt handler registration. Enabled vblank handlers run in
// interrupt context at every vblank, in sub-interrupt order, called as
// handler(subno, arg). Other interrupt sources are registered here and raised
// by the module that owns them (GE).
class InterruptManager {
public:
    explicit InterruptManager(EarlyKernel &kernel);

    [[nodiscard]] std::int32_t register_handler(std::uint32_t interrupt, std::uint32_t sub, std::uint32_t handler,
                                                std::uint32_t argument, std::uint32_t gp);
    [[nodiscard]] std::int32_t release_handler(std::uint32_t interrupt, std::uint32_t sub);
    [[nodiscard]] std::int32_t enable(std::uint32_t interrupt, std::uint32_t sub);
    [[nodiscard]] std::int32_t disable(std::uint32_t interrupt, std::uint32_t sub);
    // Queues every enabled handler for `interrupt`; returns how many.
    std::size_t raise(std::uint32_t interrupt);

private:
    friend class sarusaru::save_state::KernelSnapshot;
    struct Handler {
        std::uint32_t address{};
        std::uint32_t argument{};
        std::uint32_t gp{};
        bool enabled{};
    };
    EarlyKernel *kernel_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, Handler> handlers_;

    void update_vblank_activity() noexcept;
};

void register_interrupts(InterruptManager &interrupts, psprecomp::Runtime &runtime);

} // namespace sarusaru::kernel
