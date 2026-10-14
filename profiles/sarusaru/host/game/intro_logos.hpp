#pragma once

#include "ctrl/controller.hpp"

namespace sarusaru::game {

// The opening logo task in the verified UCJS10060 executable. Its update is
// dispatched virtually, so both chained and outer runtime calls observe it.
class IntroLogos {
public:
    static constexpr std::uint32_t kUpdate = 0x08905FC8u;
    explicit IntroLogos(bool automatic = false) : requested_(automatic) {}
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    void on_call(std::uint32_t pc, psprecomp::GuestMemory &memory,
                 const psprecomp::AllegrexContext &ctx, ctrl::Controller &controller, std::uint64_t now_us);

private:
    bool requested_{};
    bool finished_{};
};

} // namespace sarusaru::game
