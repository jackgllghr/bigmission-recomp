#include "game/frame_rate.hpp"
#include "display/display.hpp"
#include "kernel/guest_clock.hpp"
#include "psprecomp/common.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
constexpr std::uint32_t object = 0x08807000u;
constexpr std::uint32_t object_pointer = 0x08A82B24u;
constexpr std::uint32_t wait_return = 0x088FEB1Cu;
void prepare(psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
    rt.memory().store32(object_pointer, object);
    rt.memory().store32(object + 48u, 123u);
    rt.memory().store32(object + 52u, 1u);
    ctx.gpr[16] = object;
    ctx.gpr[31] = wait_return;
}
}
int main() {
    using sarusaru::game::FrameRateOverride;
    for (const auto fps : {0u, 15u, 20u, 30u, 60u}) {
        psprecomp::Runtime rt;
        auto &ctx = rt.cpu();
        prepare(rt, ctx);
        ctx.gpr[2] = 321u;
        const auto registers = ctx.gpr;
        FrameRateOverride override(fps);
        override.on_vcount(rt, ctx);
        require(rt.memory().load32(object + 52u) == (fps == 0 ? 1u : 60u / fps - 1u), "wrong game frame interval");
        require(ctx.gpr == registers && rt.memory().load32(object + 48u) == 123u, "override changed registers or saved vcount");
        require(override.overridden_frames() == (fps == 0 ? 0u : 1u), "wrong override counter");
        require(override.interval_vblanks() == (fps == 0 ? 0u : 60u / fps), "wrong reported interval");
        override.on_vcount(rt, ctx);
        require(!rt.stopped(), "repeated override rejected its own interval");
    }
    for (const auto fps : {1u, 24u, 45u, 59u, 120u, 0xFFFFFFFFu}) {
        try { FrameRateOverride invalid(fps); require(false, "unsupported rate accepted"); }
        catch (const psprecomp::Error &) {}
    }
    {
        psprecomp::Runtime rt;
        auto &ctx = rt.cpu(); prepare(rt, ctx);
        FrameRateOverride override(60);
        ctx.gpr[31] = wait_return + 4;
        override.on_vcount(rt, ctx);
        require(rt.memory().load32(object + 52u) == 1 && !rt.stopped(), "unrelated GetVcount patched");
        ctx.gpr[31] = wait_return;
        rt.memory().store32(object_pointer, object + 4);
        override.on_vcount(rt, ctx);
        require(rt.stopped() && rt.memory().load32(object + 52u) == 1, "unexpected frame object was patched");
    }
    for (const auto pointer : {0u, 0xFFFFFFFFu, object + 1u}) {
        psprecomp::Runtime rt; auto &ctx = rt.cpu(); prepare(rt, ctx);
        ctx.gpr[16] = pointer;
        FrameRateOverride override(60); override.on_vcount(rt, ctx);
        require(rt.stopped(), "invalid object pointer accepted");
    }
    {
        psprecomp::Runtime rt; auto &ctx = rt.cpu(); prepare(rt, ctx);
        rt.memory().store32(object + 52u, 100u);
        FrameRateOverride override(60); override.on_vcount(rt, ctx);
        require(rt.stopped() && rt.memory().load32(object + 52u) == 100u, "unexpected interval overwritten");
    }
    // Exercise the actual import wiring, preserving the PSP vcount result and
    // the kernel clock rather than faking either to increase game cadence.
    {
        psprecomp::Runtime rt;
        sarusaru::kernel::EarlyKernel kernel;
        kernel.install(rt, 0x08804000u, 0x0A000000u);
        sarusaru::display::Display display;
        FrameRateOverride override(60);
        sarusaru::display::register_display(display, kernel, rt,
            [&](psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) { override.on_vcount(runtime, ctx); });
        auto &ctx = rt.cpu(); prepare(rt, ctx);
        rt.invoke_import("sceDisplay", 0x9C6EAAD7u, ctx);
        require(ctx.gpr[2] == 0 && kernel.now_us() == 0 && rt.memory().load32(object + 52u) == 0,
                "GetVcount hook altered PSP time or failed to install");
        ctx.gpr[31] = 0x08804444u;
        rt.memory().store32(object + 52u, 1u);
        rt.invoke_import("sceDisplay", 0x9C6EAAD7u, ctx);
        require(ctx.gpr[2] == 0 && rt.memory().load32(object + 52u) == 1, "import hook leaked to another caller");
    }
    // Lower rates must block for three/four real PSP vblanks, even though the
    // guest frame routine contains only two wait calls. Neither wait doubles
    // the requested deadline; unrelated callers still wait one native vblank.
    for (const auto fps : {15u, 20u, 30u, 60u}) {
        psprecomp::Runtime rt;
        sarusaru::kernel::EarlyKernel kernel;
        kernel.install(rt, 0x08804000u, 0x0A000000u);
        auto &ctx = rt.cpu();
        ctx.pc = 0x08804444u;
        kernel.begin_main_thread(ctx, "frame test", 0x20u);
        prepare(rt, ctx);
        rt.memory().store32(object + 48u, 0xFFFFFFFFu);
        sarusaru::display::Display display;
        FrameRateOverride override(fps);
        sarusaru::display::register_display(display, kernel, rt,
            [&](psprecomp::Runtime &runtime, psprecomp::AllegrexContext &cpu) { override.on_vcount(runtime, cpu); },
            [&](psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &cpu, std::uint64_t now) {
                return override.wait_deadline(runtime, cpu, now);
            });
        unsigned vblanks = 0;
        kernel.set_frame_observer([&] { ++vblanks; });
        rt.invoke_import("sceDisplay", 0x9C6EAAD7u, ctx);
        ctx.gpr[31] = 0x088FEB40u;
        rt.invoke_import("sceDisplay", 0x46F186C3u, ctx);
        const auto interval = 60u / fps;
        require(!rt.stopped() && vblanks == interval && kernel.now_us() == sarusaru::kernel::vblank_time_us(interval),
                "override did not preserve every vblank while waiting to its deadline");
        ctx.gpr[31] = 0x088FEB80u;
        rt.invoke_import("sceDisplay", 0x46F186C3u, ctx);
        require(vblanks == interval, "second wait added another frame interval");
        ctx.gpr[31] = 0x08804444u;
        rt.invoke_import("sceDisplay", 0x46F186C3u, ctx);
        require(vblanks == interval + 1, "override altered an unrelated vblank wait");
        // Steady state bases the deadline on the previous frame, not the time
        // at which the frame happens to reach its wait routine.
        rt.memory().store32(object + 48u, interval);
        ctx.gpr[31] = wait_return;
        rt.invoke_import("sceDisplay", 0x9C6EAAD7u, ctx);
        ctx.gpr[31] = 0x088FEB40u;
        rt.invoke_import("sceDisplay", 0x46F186C3u, ctx);
        require(vblanks == interval * 2 && kernel.now_us() == sarusaru::kernel::vblank_time_us(interval * 2),
                "steady-state deadline ignored already elapsed vblanks");
    }
    std::cout << "Game frame-rate override tests passed\n";
}
