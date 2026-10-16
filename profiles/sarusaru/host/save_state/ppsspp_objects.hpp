#pragma once

#include "save_state/ppst.hpp"

#include <map>
#include <array>

namespace sarusaru::save_state {
enum class PspObjectType : std::uint32_t {
    Thread = 1, Semaphore = 2, EventFlag = 3, Callback = 8,
    Module = 0x100001, PartitionMemory = 0x100002, File = 0x100003, Directory = 0x100004,
};
struct PspObjectRecord {
    PspObjectType type{};
    std::string name;
    // Complete versioned object section, excluding its type and implicit UID.
    std::vector<std::uint8_t> encoded;
};
struct PspKernelObjects {
    std::vector<std::uint8_t> hle_record;
    bool running{};
    std::uint32_t next_pool_index{}, exit_callback{};
    std::map<std::uint32_t, PspObjectRecord> objects;
    // Starts with Kernel Modules and retains the enclosing SaveStart cookie.
    std::vector<std::uint8_t> remainder;
};

// PPSSPP 1.20.4 HLE + Kernel/KernelObjectPool records. Unknown object/action
// schemas fail at their structural position; no marker scanning is used.
[[nodiscard]] PspKernelObjects decode_psp_objects(std::span<const std::uint8_t> tail);
[[nodiscard]] std::vector<std::uint8_t> encode_psp_objects(const PspKernelObjects &objects);

struct PspSemaphore {
    std::uint32_t native_size{56};
    std::array<std::uint8_t, 32> name{};
    std::uint32_t attributes{}, initial_count{}, count{}, maximum{}, waiter_count{};
    std::vector<std::uint32_t> waiters;
    std::map<std::uint32_t, std::uint64_t> paused_waits;
};
struct PspEventWaiter {
    std::uint32_t thread{}, bits{}, mode{}, output_address{};
    std::uint64_t paused_timeout{};
    bool operator==(const PspEventWaiter &) const = default;
};
struct PspEventFlag {
    std::uint32_t native_size{52};
    std::array<std::uint8_t, 32> name{};
    std::uint32_t attributes{}, initial_pattern{}, pattern{}, waiter_count{};
    std::vector<PspEventWaiter> waiters;
    std::map<std::uint32_t, PspEventWaiter> paused_waits;
};
struct PspCallback {
    std::uint32_t native_size{56};
    std::array<std::uint8_t, 32> name{};
    std::uint32_t owner{}, entry{}, argument{}, notify_count{}, notify_argument{};
    std::array<std::uint32_t, 5> legacy_registers{};
};
// Named-object fixed fields retain all bytes, including bytes after a name's
// NUL terminator. Counts in the guest query structs are preserved separately
// from host waiter lists; adapters validate their relationships before use.
[[nodiscard]] PspSemaphore decode_psp_semaphore(std::span<const std::uint8_t> section);
[[nodiscard]] PspEventFlag decode_psp_event_flag(std::span<const std::uint8_t> section);
[[nodiscard]] PspCallback decode_psp_callback(std::span<const std::uint8_t> section);
[[nodiscard]] std::vector<std::uint8_t> encode_psp_semaphore(const PspSemaphore &semaphore);
[[nodiscard]] std::vector<std::uint8_t> encode_psp_event_flag(const PspEventFlag &event);
[[nodiscard]] std::vector<std::uint8_t> encode_psp_callback(const PspCallback &callback);
} // namespace sarusaru::save_state
