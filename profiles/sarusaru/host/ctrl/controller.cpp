#include "ctrl/controller.hpp"

#include "kernel/guest_clock.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace sarusaru::ctrl {

std::int32_t Controller::set_sampling_mode(std::uint32_t mode) noexcept {
    if (mode > 1u) return static_cast<std::int32_t>(kInvalidMode);
    const auto previous = mode_;
    mode_ = mode;
    return static_cast<std::int32_t>(previous);
}

std::int32_t Controller::set_sampling_cycle(std::uint32_t cycle) noexcept {
    // 0 samples on vblank; otherwise the period is in microseconds.
    if (cycle != 0u && (cycle < 5555u || cycle > 20000u)) return static_cast<std::int32_t>(kInvalidValue);
    const auto previous = cycle_;
    cycle_ = cycle;
    return static_cast<std::int32_t>(previous);
}

std::int32_t Controller::set_idle_cancel_threshold(std::int32_t idle_reset, std::int32_t idle_back) noexcept {
    if (idle_reset < -1 || idle_reset > 128 || idle_back < -1 || idle_back > 128)
        return static_cast<std::int32_t>(kInvalidValue);
    idle_reset_ = idle_reset;
    idle_back_ = idle_back;
    return 0;
}

std::uint64_t Controller::sample_index(std::uint64_t time_us) const noexcept {
    return cycle_ == 0u ? kernel::vblanks_by(time_us) : time_us / cycle_;
}

std::uint64_t Controller::sample_time_us(std::uint64_t index) const noexcept {
    return cycle_ == 0u ? kernel::vblank_time_us(index) : index * cycle_;
}

Controller::Pad Controller::pad_at(std::uint64_t time_us) const noexcept {
    Pad pad = pad_;
    for (const auto &event : script_) {
        if (event.time_us > time_us) break;
        pad = event.pad;
    }
    return pad;
}

std::optional<std::vector<Controller::ScriptEvent>> parse_input_script(std::string_view text) {
    static constexpr std::pair<std::string_view, std::uint32_t> kNames[] = {
        {"SELECT", kButtonSelect}, {"START", kButtonStart}, {"UP", kButtonUp}, {"RIGHT", kButtonRight},
        {"DOWN", kButtonDown}, {"LEFT", kButtonLeft}, {"L", kButtonL}, {"R", kButtonR},
        {"TRIANGLE", kButtonTriangle}, {"CIRCLE", kButtonCircle}, {"CROSS", kButtonCross}, {"SQUARE", kButtonSquare},
    };
    std::vector<Controller::ScriptEvent> events;
    while (!text.empty()) {
        const auto end = text.find(';');
        const std::string_view entry = text.substr(0u, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1u);
        const auto colon = entry.find(':');
        if (colon == std::string_view::npos) return std::nullopt;
        double seconds = 0.0;
        try {
            std::size_t used = 0u;
            const std::string time_text(entry.substr(0u, colon));
            seconds = std::stod(time_text, &used);
            if (used != time_text.size() || !(seconds >= 0.0)) return std::nullopt;
        } catch (const std::exception &) {
            return std::nullopt;
        }
        Controller::ScriptEvent event{static_cast<std::uint64_t>(std::llround(seconds * 1e6)), {}};
        std::string_view buttons = entry.substr(colon + 1u);
        if (buttons != "-") {
            while (!buttons.empty()) {
                const auto plus = buttons.find('+');
                const std::string_view name = buttons.substr(0u, plus);
                buttons = plus == std::string_view::npos ? std::string_view{} : buttons.substr(plus + 1u);
                const auto found = std::ranges::find_if(kNames, [name](const auto &known) { return known.first == name; });
                if (found == std::end(kNames)) return std::nullopt;
                event.pad.buttons |= found->second;
            }
        }
        if (!events.empty() && event.time_us <= events.back().time_us) return std::nullopt;
        events.push_back(event);
    }
    return events;
}

std::uint32_t Controller::write_samples(psprecomp::GuestMemory &memory, std::uint32_t address,
                                        std::uint32_t count, std::uint64_t now_us) {
    const std::uint64_t latest = sample_index(now_us);
    const auto available = std::min<std::uint64_t>({latest - std::min(latest, last_read_sample_), count,
                                                     kMaxBufferedSamples});
    const auto written = static_cast<std::uint32_t>(available);
    for (std::uint32_t i = 0; i < written; ++i) {
        const std::uint64_t index = latest - written + 1u + i;
        const std::uint32_t sample = address + i * kSampleSize;
        memory.store32(sample, static_cast<std::uint32_t>(sample_time_us(index)));
        const Pad pad = pad_at(sample_time_us(index));
        memory.store32(sample + 4u, pad.buttons);
        // Digital mode reports the analog stick centred.
        memory.store8(sample + 8u, mode_ == 1u ? pad.lx : 0x80u);
        memory.store8(sample + 9u, mode_ == 1u ? pad.ly : 0x80u);
        for (std::uint32_t reserved = 10u; reserved < kSampleSize; ++reserved) memory.store8(sample + reserved, 0u);
    }
    last_read_sample_ = latest;
    return written;
}

void register_ctrl(Controller &controller, kernel::EarlyKernel &kernel, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceCtrl", 0x1F803938u,  // sceCtrlReadBufferPositive
        [&controller, &kernel](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const std::uint32_t address = ctx.gpr[4];
            const std::uint32_t count = ctx.gpr[5];
            if (count > kMaxBufferedSamples) { ctx.set_gpr(2u, kInvalidSize); return; }
            if (count == 0u) { ctx.set_gpr(2u, 0u); return; }
            if (!rt.memory().contains(address, count * kSampleSize)) { ctx.set_gpr(2u, kInvalidPointer); return; }
            const std::uint32_t written = controller.write_samples(rt.memory(), address, count, kernel.now_us());
            if (written != 0u) { ctx.set_gpr(2u, written); return; }
            const std::uint64_t next = controller.sample_time_us(controller.sample_index(kernel.now_us()) + 1u);
            kernel.wait_current_until(ctx, next, [&controller, &kernel, &rt, address, count] {
                return controller.write_samples(rt.memory(), address, count, kernel.now_us());
            });
        });
    runtime.register_hle("sceCtrl", 0x1F4011E6u,  // sceCtrlSetSamplingMode
        [&controller](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(controller.set_sampling_mode(ctx.gpr[4])));
        });
    runtime.register_hle("sceCtrl", 0x6A2774F3u,  // sceCtrlSetSamplingCycle
        [&controller](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(controller.set_sampling_cycle(ctx.gpr[4])));
        });
    runtime.register_hle("sceCtrl", 0xA7144800u,  // sceCtrlSetIdleCancelThreshold
        [&controller](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(controller.set_idle_cancel_threshold(
                static_cast<std::int32_t>(ctx.gpr[4]), static_cast<std::int32_t>(ctx.gpr[5]))));
        });
}

} // namespace sarusaru::ctrl
