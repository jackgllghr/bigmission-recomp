#pragma once
#include <cstdint>

namespace sarusaru::shell {
// Selects presentations on the guest clock without changing simulation, audio,
// input sampling or GE submissions. Deadlines remain anchored to avoid drift.
class PresentationClock {
public:
    explicit PresentationClock(std::uint32_t fps) : fps_(fps) {}
    [[nodiscard]] bool due(std::uint64_t time_us) noexcept {
        if (fps_ == 0) return true;
        if (started_ && time_us * fps_ < next_tick_) return false;
        started_ = true;
        next_tick_ = (time_us * fps_ / 1'000'000u + 1u) * 1'000'000u;
        return true;
    }
private:
    std::uint32_t fps_;
    std::uint64_t next_tick_{};
    bool started_{};
};
} // namespace sarusaru::shell
