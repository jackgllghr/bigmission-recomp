#include "game/intro_logos.hpp"
#include "kernel/guest_clock.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using sarusaru::game::IntroLogos;
    using namespace sarusaru::ctrl;
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    auto &ctx = runtime.cpu();
    Controller controller;
    constexpr std::uint32_t object = 0x08900000u, samples = object + 256u;
    ctx.set_gpr(4u, object);
    memory.store32(object + 8u, 0x08A88D98u);
    const auto call = [&](IntroLogos &logos, std::uint32_t state) {
        memory.store32(object + 12u, state);
        logos.on_call(IntroLogos::kUpdate, memory, ctx, controller, 0u);
        return memory.load32(object + 12u);
    };
    IntroLogos native;
    require(call(native, 2u) == 2u && call(native, 3u) == 3u, "logos skipped without a request");
    require(call(native, 5u) == 5u && native.finished(), "normal completion not recognised");
    IntroLogos automatic(true);
    require(call(automatic, 0u) == 0u && call(automatic, 1u) == 1u, "initialisation skipped");
    automatic.on_call(0x088405F4u, memory, ctx, controller, 0u);
    require(memory.load32(object + 12u) == 1u, "title update changed logo state");
    memory.store32(object + 8u, 0u);
    require(call(automatic, 2u) == 2u, "unexpected task changed");
    ctx.set_gpr(4u, 0xFFFFFFFFu);
    automatic.on_call(IntroLogos::kUpdate, memory, ctx, controller, 0u);
    ctx.set_gpr(4u, object);
    memory.store32(object + 8u, 0x08A88D98u);
    require(call(automatic, 2u) == 5u && automatic.finished(), "automatic skip did not use completion state");
    require(call(automatic, 2u) == 2u, "completed startup skip affected a later task");

    // A short press during initialisation is retained; holding it through the
    // transition must not also press Start on the title screen.
    IntroLogos manual;
    controller.set_pad({kButtonStart | kButtonCross});
    require(call(manual, 0u) == 0u, "Start bypassed initialisation");
    const auto sample = [&](std::uint64_t index) {
        require(controller.write_samples(memory, samples, 1u, sarusaru::kernel::vblank_time_us(index)) == 1u,
                "controller sample missing");
        return memory.load32(samples + 4u);
    };
    require(sample(1u) == kButtonCross && sample(2u) == kButtonCross, "Start leaked or other button consumed");
    controller.set_pad({});
    require(call(manual, 2u) == 5u, "short Start press was lost");
    require(sample(3u) == 0u, "release not sampled");
    controller.set_pad({kButtonStart});
    require(sample(4u) == kButtonStart, "Start stayed consumed after release");
    IntroLogos during_fade;
    require(call(during_fade, 4u) == 5u, "Start during fade did not skip");
    Controller buffered;
    buffered.set_script(*parse_input_script("0.1:START;0.2:-"));
    buffered.consume_buttons(kButtonStart, 120000u);
    require(buffered.write_samples(memory, samples, 8u, 150000u) == 8u, "buffered samples missing");
    for (unsigned i = 0; i < 8u; ++i)
        require(memory.load32(samples + i * kSampleSize + 4u) == 0u, "historical release exposed consumed Start");
    require(buffered.write_samples(memory, samples, 1u, 220000u) == 1u, "buffered release missing");
    buffered.set_script({});
    buffered.set_pad({kButtonStart});
    require(buffered.write_samples(memory, samples, 1u, 240000u) == 1u && memory.load32(samples + 4u) == kButtonStart,
            "buffered shortcut did not release");
    std::cout << "intro logo tests passed\n";
}
