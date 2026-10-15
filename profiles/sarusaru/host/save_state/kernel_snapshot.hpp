#pragma once

#include "kernel/interrupts.hpp"
#include "kernel/sync.hpp"

#include <memory>
#include <span>

namespace sarusaru::save_state {
// A detached scheduler, synchronization-object, and interrupt snapshot. The
// codec is an internal representation for adapters, not a PPSSPP save file.
// Capture/restore require the outer runtime dispatcher (or a stopped runtime).
// Destination module bindings and host observers survive restoration.
class KernelSnapshot {
public:
    static KernelSnapshot capture(const kernel::EarlyKernel &kernel, const kernel::SyncObjects &sync,
                                  const kernel::InterruptManager &interrupts);
    static KernelSnapshot decode(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    // Validates the entire detached state before changing any destination data.
    void restore(kernel::EarlyKernel &kernel, kernel::SyncObjects &sync, kernel::InterruptManager &interrupts) &&;

    ~KernelSnapshot();
    KernelSnapshot(KernelSnapshot &&) noexcept;
    KernelSnapshot &operator=(KernelSnapshot &&) noexcept;
    KernelSnapshot(const KernelSnapshot &) = delete;
    KernelSnapshot &operator=(const KernelSnapshot &) = delete;

private:
    struct Data;
    explicit KernelSnapshot(std::unique_ptr<Data> data);
    void validate(const kernel::EarlyKernel &kernel) const;
    std::unique_ptr<Data> data_;
};
} // namespace sarusaru::save_state
