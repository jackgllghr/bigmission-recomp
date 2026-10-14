#include "display/display.hpp"

#include "kernel/guest_clock.hpp"

namespace sarusaru::display {

std::int32_t Display::set_mode(std::uint32_t mode, std::uint32_t width, std::uint32_t height) noexcept {
    if (mode != 0u) return static_cast<std::int32_t>(kInvalidMode);
    if (width != 480u || height != 272u) return static_cast<std::int32_t>(kInvalidSize);
    mode_ = mode;
    width_ = width;
    height_ = height;
    return 0;
}

std::int32_t Display::set_framebuffer(const Framebuffer &framebuffer, std::uint32_t sync, std::uint64_t now_us,
                                      const psprecomp::GuestMemory &memory) {
    if (sync > 1u) return static_cast<std::int32_t>(kInvalidMode);
    // A null address turns the display off and skips the other checks.
    if (framebuffer.address != 0u) {
        if (framebuffer.pixel_format > 3u) return static_cast<std::int32_t>(kInvalidFormat);
        if (framebuffer.buffer_width < 512u || (framebuffer.buffer_width & (framebuffer.buffer_width - 1u)) != 0u)
            return static_cast<std::int32_t>(kInvalidSize);
        const std::uint32_t bytes_per_pixel = framebuffer.pixel_format == 3u ? 4u : 2u;
        if ((framebuffer.address & 0xFu) != 0u ||
            !memory.contains(framebuffer.address, framebuffer.buffer_width * height_ * bytes_per_pixel))
            return static_cast<std::int32_t>(kInvalidPointer);
    }
    apply_pending(now_us);
    requested_ = framebuffer;
    if (sync == 0u) {
        latched_ = framebuffer;
        latch_at_vblank_.reset();
    } else {
        latch_at_vblank_ = kernel::vblanks_by(now_us) + 1u;
    }
    return 0;
}

void Display::apply_pending(std::uint64_t now_us) noexcept {
    if (latch_at_vblank_ && kernel::vblanks_by(now_us) >= *latch_at_vblank_) {
        latched_ = requested_;
        latch_at_vblank_.reset();
    }
}

Display::Framebuffer Display::framebuffer(std::uint32_t sync, std::uint64_t now_us) {
    apply_pending(now_us);
    return sync == 1u ? latched_ : requested_;
}

std::uint32_t Display::vcount(std::uint64_t now_us) noexcept {
    return static_cast<std::uint32_t>(kernel::vblanks_by(now_us));
}

bool Display::in_vblank(std::uint64_t now_us) noexcept {
    const auto count = kernel::vblanks_by(now_us);
    return count != 0u && now_us - kernel::vblank_time_us(count) < kVblankDurationUs;
}

std::uint32_t Display::accumulated_hcount(std::uint64_t now_us) noexcept {
    return static_cast<std::uint32_t>(now_us * kLinesPerFrame * 60u / 1001000u);
}

std::uint64_t Display::next_vblank_start_us(std::uint64_t now_us) noexcept {
    return kernel::vblank_time_us(kernel::vblanks_by(now_us) + 1u);
}

void register_display(Display &display, kernel::EarlyKernel &kernel, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceDisplay", 0x0E20F177u,  // sceDisplaySetMode
        [&display](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(display.set_mode(ctx.gpr[4], ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("sceDisplay", 0x289D82FEu,  // sceDisplaySetFrameBuf
        [&display, &kernel](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(display.set_framebuffer(
                {ctx.gpr[4], ctx.gpr[5], ctx.gpr[6]}, ctx.gpr[7], kernel.now_us(), rt.memory())));
        });
    runtime.register_hle("sceDisplay", 0xEEDA2E54u,  // sceDisplayGetFrameBuf
        [&display, &kernel](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            if (ctx.gpr[7] > 1u) { ctx.set_gpr(2u, kInvalidMode); return; }
            const auto current = display.framebuffer(ctx.gpr[7], kernel.now_us());
            const std::uint32_t outputs[3] = {ctx.gpr[4], ctx.gpr[5], ctx.gpr[6]};
            const std::uint32_t values[3] = {current.address, current.buffer_width, current.pixel_format};
            for (int i = 0; i < 3; ++i) {
                if (outputs[i] != 0u && !rt.memory().contains(outputs[i], 4u)) { ctx.set_gpr(2u, kInvalidPointer); return; }
            }
            for (int i = 0; i < 3; ++i) if (outputs[i] != 0u) rt.memory().store32(outputs[i], values[i]);
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceDisplay", 0x9C6EAAD7u,  // sceDisplayGetVcount
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, Display::vcount(kernel.now_us()));
        });
    runtime.register_hle("sceDisplay", 0x4D4E10ECu,  // sceDisplayIsVblank
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, Display::in_vblank(kernel.now_us()) ? 1u : 0u);
        });
    runtime.register_hle("sceDisplay", 0x210EAB3Au,  // sceDisplayGetAccumulatedHcount
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, Display::accumulated_hcount(kernel.now_us()));
        });
    const auto wait_vblank_start = [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
        kernel.wait_current_until(ctx, Display::next_vblank_start_us(kernel.now_us()), nullptr);
    };
    runtime.register_hle("sceDisplay", 0x984C27E7u, wait_vblank_start);  // sceDisplayWaitVblankStart
    runtime.register_hle("sceDisplay", 0x46F186C3u,  // sceDisplayWaitVblankStartCB
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            kernel.block_current(ctx, {"vblank", Display::next_vblank_start_us(kernel.now_us()), 0u, nullptr, 0, true});
        });
    runtime.register_hle("sceDisplay", 0x36CDFADEu,  // sceDisplayWaitVblank: returns at once inside vblank
        [&kernel](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            if (Display::in_vblank(kernel.now_us())) { ctx.set_gpr(2u, 0u); return; }
            kernel.wait_current_until(ctx, Display::next_vblank_start_us(kernel.now_us()), nullptr);
        });
}

} // namespace sarusaru::display
