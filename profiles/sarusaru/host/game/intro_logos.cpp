#include "game/intro_logos.hpp"

namespace sarusaru::game {

void IntroLogos::on_call(std::uint32_t pc, psprecomp::GuestMemory &memory,
                         const psprecomp::AllegrexContext &ctx, ctrl::Controller &controller, std::uint64_t now_us) {
    if (finished_ || pc != kUpdate) return;
    const auto object = ctx.gpr[4];
    // stLogo's vtable and state layout, verified against BOOT.elf. Leave an
    // unexpected object alone rather than treating another task as a logo.
    if ((object & 3u) != 0 || !memory.contains(object, 32u) || memory.load32(object + 8u) != 0x08A88D98u) return;
    const auto state = memory.load32(object + 12u);
    if (state > 5u) return;
    if (state == 5u) { finished_ = true; return; }
    if ((controller.pad_at(now_us).buttons & ctrl::kButtonStart) != 0) {
        requested_ = true;
        controller.consume_buttons(ctrl::kButtonStart, now_us);
    }
    // States 0/1 initialise the three logo sprites and their resource handles.
    // Once ready, state 5 clears the background and removes the task through
    // its normal destructor. No jumps, clocks, or title/movie state changes.
    if (requested_ && state >= 2u) {
        memory.store32(object + 12u, 5u);
        finished_ = true;
    }
}

} // namespace sarusaru::game
