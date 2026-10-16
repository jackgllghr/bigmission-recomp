#pragma once
#include "save_state/session_access.hpp"

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>

namespace sarusaru::rtc {

inline constexpr std::uint32_t kInvalidPointer = 0x80000103u;

// ScePspDateTime fields.
struct DateTime {
    std::uint16_t year{}, month{}, day{}, hour{}, minute{}, second{};
    std::uint32_t microsecond{};
};

[[nodiscard]] DateTime civil_from_unix_us(std::int64_t unix_us) noexcept;

// Guest wall clock: the boot time plus the kernel's virtual elapsed time, so
// it advances exactly with guest time.
class Rtc {
    friend struct sarusaru::save_state::SessionCodec;
public:
    // `boot_local_unix_us` is local wall time at boot, expressed as Unix microseconds.
    Rtc(kernel::EarlyKernel &kernel, std::int64_t boot_local_unix_us) noexcept
        : kernel_(&kernel), boot_local_unix_us_(boot_local_unix_us) {}
    // Local time now, or SARUSARU_RTC_EPOCH (Unix seconds, read as local time) when set.
    [[nodiscard]] static std::int64_t host_boot_local_unix_us();

    [[nodiscard]] DateTime local_time() const noexcept {
        return civil_from_unix_us(boot_local_unix_us_ + static_cast<std::int64_t>(kernel_->now_us()));
    }

private:
    kernel::EarlyKernel *kernel_;
    std::int64_t boot_local_unix_us_;
};

void register_rtc(Rtc &rtc, psprecomp::Runtime &runtime);

} // namespace sarusaru::rtc
