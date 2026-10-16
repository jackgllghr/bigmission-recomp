#include "sas/sas.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace sarusaru::sas;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

// One VAG block: header (filter << 4 | shift), flags, 14 bytes of nibbles.
void vag_block(psprecomp::GuestMemory &memory, std::uint32_t address, std::uint8_t header, std::uint8_t flags,
               std::uint8_t data) {
    memory.store8(address, header);
    memory.store8(address + 1u, flags);
    for (std::uint32_t i = 0; i < 14u; ++i) memory.store8(address + 2u + i, data);
}

std::int16_t sample(const psprecomp::GuestMemory &memory, std::uint32_t address, std::uint32_t index) {
    return static_cast<std::int16_t>(memory.load16(address + index * 4u));
}

constexpr std::uint32_t kAdsr1 = 0x0007u;  // Linear attack rate 0, fastest decay, sustain level 7.
constexpr std::uint32_t kAdsr2 = 0x1FC0u;  // Sustain held, linear release rate 0.
}

int main() {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();

    // Decoding: shift 0 turns nibble 1 into 4096; filter 1 predicts 60/64 of the last sample.
    vag_block(memory, 0x08811000u, 0x00u, 0x04u, 0x11u);
    vag_block(memory, 0x08811010u, 0x10u, 0x01u, 0x00u);
    VagDecoder decoder;
    decoder.start(0x08811000u, 32u, false);
    for (int i = 0; i < 28; ++i) require(decoder.next(memory) == 4096, "shift-0 block decoded incorrectly");
    require(decoder.next(memory) == 3840 && decoder.next(memory) == 3600, "filter 1 prediction incorrect");
    for (int i = 2; i < 28; ++i) (void)decoder.next(memory);
    require(!decoder.next(memory) && decoder.ended(), "end flag did not stop the decoder");
    decoder.start(0x08811000u, 32u, true);
    for (int i = 0; i < 56; ++i) (void)decoder.next(memory);
    require(!decoder.next(memory) && decoder.ended(), "a plain end flag looped a looping voice");
    vag_block(memory, 0x08811010u, 0x10u, 0x03u, 0x00u);
    decoder.start(0x08811000u, 32u, true);
    for (int i = 0; i < 56; ++i) (void)decoder.next(memory);
    require(decoder.next(memory).has_value() && !decoder.ended(), "repeat flag did not return to the loop start");
    decoder.start(0x08811000u, 32u, false);
    for (int i = 0; i < 56; ++i) (void)decoder.next(memory);
    require(!decoder.next(memory) && decoder.ended(), "repeat flag looped a voice without looping");

    // Envelope: three attack steps to the maximum, decay to the sustain level, linear release.
    Envelope envelope;
    envelope.set_simple(kAdsr1, kAdsr2);
    envelope.key_on();
    envelope.step();
    require(envelope.height() == 0x1C000000, "attack step");
    envelope.step();
    envelope.step();
    require(envelope.height() == kEnvelopeMax && envelope.phase() == Envelope::Phase::Decay, "attack to decay");
    envelope.step();
    require(envelope.height() == 0x20000000 && envelope.phase() == Envelope::Phase::Sustain, "decay to sustain");
    envelope.step();
    require(envelope.height() == 0x20000000, "sustain held");
    envelope.key_off();
    envelope.step();
    envelope.step();
    require(envelope.height() == 0 && envelope.phase() == Envelope::Phase::Off, "release to off");

    SasCore sas;
    require(static_cast<std::uint32_t>(sas.init(0x08810010u, 64u, 32u, 0u, 44100u)) == kBadAddress &&
                static_cast<std::uint32_t>(sas.init(0x08810000u, 48u, 32u, 0u, 44100u)) == kInvalidGrain &&
                static_cast<std::uint32_t>(sas.init(0x08810000u, 64u, 33u, 0u, 44100u)) == kInvalidMaxVoices &&
                static_cast<std::uint32_t>(sas.init(0x08810000u, 64u, 32u, 0u, 48000u)) == kInvalidSampleRate,
            "init validation");
    register_sas(sas, runtime);
    auto &cpu = runtime.cpu();
    cpu.set_gpr(4u, 0x08810000u); cpu.set_gpr(5u, 0x08812000u);
    runtime.invoke_import("sceSasCore", 0xA3589D81u, cpu);
    require(cpu.gpr[2] == kNotInitialized, "render before init");
    cpu.set_gpr(4u, 0x08810000u); cpu.set_gpr(5u, 64u); cpu.set_gpr(6u, 32u); cpu.set_gpr(7u, 0u); cpu.set_gpr(8u, 44100u);
    runtime.invoke_import("sceSasCore", 0x42778A9Fu, cpu);
    require(cpu.gpr[2] == 0u && sas.grain() == 64u, "HLE init");

    // 84 samples of constant 4096 through the envelope above.
    for (std::uint32_t block = 0; block < 3u; ++block)
        vag_block(memory, 0x08811100u + block * 16u, 0x00u, block == 2u ? 0x01u : 0x00u, 0x11u);
    require(static_cast<std::uint32_t>(sas.set_voice(0, 0x08811100u, 40, 0u, memory)) == kInvalidParameter &&
                static_cast<std::uint32_t>(sas.set_pitch(0, 0x4001u)) == kInvalidPitch &&
                static_cast<std::uint32_t>(sas.set_volume(0, 0x1001, 0, 0, 0)) == kInvalidVolume,
            "voice validation");
    require(sas.set_voice(0, 0x08811100u, 48, 0u, memory) == 0 && sas.set_volume(0, 0x1000, 0x800, 0, 0) == 0 &&
                sas.set_simple_adsr(0, kAdsr1, kAdsr2) == 0 && sas.key_on(0) == 0 && (sas.end_flags() & 1u) == 0u,
            "voice setup");
    constexpr std::uint32_t out = 0x08812000u;
    require(sas.render(memory, out, false, 0, 0) == 0, "first grain");
    require(sample(memory, out, 0u) == 1792 && sample(memory, out, 1u) == 3584 && sample(memory, out, 2u) == 4096 &&
                sample(memory, out, 3u) == 2048 && sample(memory, out, 63u) == 2048 &&
                static_cast<std::int16_t>(memory.load16(out + 2u)) == 896,
            "envelope-shaped output incorrect");
    require(sas.render(memory, out, false, 0, 0) == 0, "second grain");
    require(sample(memory, out, 18u) == 2048 && sample(memory, out, 19u) == 0 && (sas.end_flags() & 1u) != 0u,
            "voice did not end with its data");

    // Mixing scales the existing samples first.
    for (std::uint32_t i = 0; i < 64u; ++i) { memory.store16(out + i * 4u, 1000u); memory.store16(out + i * 4u + 2u, 1000u); }
    require(sas.render(memory, out, true, 0x800, 0x1000) == 0 && sample(memory, out, 0u) == 500 &&
                static_cast<std::int16_t>(memory.load16(out + 2u)) == 1000,
            "mix volumes incorrect");

    require(sas.set_pause(1u, true) == 0 && sas.pause_flags() == 1u &&
                static_cast<std::uint32_t>(sas.key_on(0)) == kVoicePaused,
            "pause");

    // Reverb: echo and delay derive their taps from the delay parameter; at
    // delay 255 that is the psx-spx Chaos Echo table, and feedback 127 its vWALL.
    Reverb reverb;
    reverb.set_type(Reverb::kEcho);
    reverb.set_parameters(255, 127);
    require(reverb.registers().mLSAME == 0x1FFF && reverb.registers().mRSAME == 0x0FFF &&
                reverb.registers().mLCOMB1 == 0x1005 && reverb.registers().dLSAME == 0x1005 &&
                reverb.registers().mLAPF1 == 0x1004 && reverb.registers().mRAPF1 == 0x1002 &&
                reverb.registers().vWALL == 0x8100,
            "echo registers at delay 255");

    // Delay without feedback: one impulse comes back once, (16 * (delay + 1) - 4)
    // * 4 steps later at 22050 Hz, so 96 output samples later at delay 0.
    reverb.set_type(Reverb::kDelay);
    reverb.set_parameters(0, 0);
    std::vector<std::int64_t> left(256, 0), right(256, 0);
    left[0] = left[1] = 0x4000;
    reverb.process(left, right, 0x1000, 0x1000);
    bool silent_before = true;
    for (std::size_t i = 0; i < 95u; ++i) silent_before = silent_before && left[i] == 0 && right[i] == 0;
    require(silent_before, "delay echoed early");
    require(left[97] > 0x3F00 && left[97] <= 0x4000 && right[97] == 0, "delay echo missing or wrong channel");
    bool silent_after = true;
    for (std::size_t i = 99u; i < 256u; ++i) silent_after = silent_after && left[i] == 0;
    require(silent_after, "delay without feedback repeated");
    reverb.set_type(Reverb::kOff);
    left.assign(64, 1000);
    right.assign(64, 1000);
    reverb.process(left, right, 0x1000, 0x1000);
    require(left[0] == 0 && right[63] == 0, "reverb off produced output");

    // Through SasCore: type and parameter validation, then a voice sending
    // only to the effect is silent until the wet output is switched on.
    require(static_cast<std::uint32_t>(sas.set_reverb_type(9)) == kInvalidReverbType &&
                static_cast<std::uint32_t>(sas.set_reverb_type(-2)) == kInvalidReverbType &&
                sas.set_reverb_type(8) == 0 && sas.reverb().type() == 8 &&
                static_cast<std::uint32_t>(sas.set_reverb_parameters(128, 0)) == kInvalidReverbDelay &&
                static_cast<std::uint32_t>(sas.set_reverb_parameters(0, 128)) == kInvalidReverbFeedback &&
                static_cast<std::uint32_t>(sas.set_reverb_volume(0x1001, 0)) == kInvalidReverbVolume,
            "reverb validation");
    require(sas.set_pause(1u, false) == 0 && sas.set_reverb_type(Reverb::kDelay) == 0 &&
                sas.set_reverb_parameters(0, 0) == 0 && sas.set_reverb_volume(0x1000, 0x1000) == 0 &&
                sas.set_voice(0, 0x08811100u, 48, 0u, memory) == 0 && sas.set_volume(0, 0, 0, 0x1000, 0x1000) == 0 &&
                sas.key_on(0) == 0,
            "reverb voice setup");
    const auto grain_peak = [&] {
        std::int32_t peak = 0;
        for (std::uint32_t i = 0; i < 64u; ++i) peak = std::max<std::int32_t>(peak, std::abs(sample(memory, out, i)));
        return peak;
    };
    std::int32_t dry_peak = 0;
    for (int grain = 0; grain < 4; ++grain) {
        require(sas.render(memory, out, false, 0, 0) == 0, "dry-only grain");
        dry_peak = std::max(dry_peak, grain_peak());
    }
    require(dry_peak == 0, "effect send leaked into the dry output");
    require(sas.key_on(0) == 0 && sas.set_reverb_voice_on(0, 1) == 0, "wet on");
    std::int32_t wet_peak = 0;
    for (int grain = 0; grain < 4; ++grain) {
        require(sas.render(memory, out, false, 0, 0) == 0, "wet grain");
        wet_peak = std::max(wet_peak, grain_peak());
    }
    require(wet_peak > 1000, "wet output missing");
    return 0;
}
