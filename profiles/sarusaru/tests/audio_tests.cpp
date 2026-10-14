#include "audio/audio.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sarusaru::audio;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void call(psprecomp::Runtime &runtime, std::uint32_t nid, std::uint32_t return_address,
          std::initializer_list<std::uint32_t> arguments) {
    auto &cpu = runtime.cpu();
    std::uint32_t reg = 4u;
    for (const auto argument : arguments) cpu.set_gpr(reg++, argument);
    cpu.pc = 0x08A00000u;
    cpu.set_gpr(31u, return_address);
    runtime.invoke_import("sceAudio", nid, cpu);
}

constexpr std::uint32_t kReserve = 0x5EC81C55u, kRelease = 0x6FC46853u, kOutputPannedBlocking = 0x13F592BCu,
                        kOutputPanned = 0xE2D56B2Du, kRestLength = 0xB011922Fu;
constexpr std::uint32_t kBuffer = 0x08807000u;
}

int main() {
    psprecomp::Runtime runtime;
    sarusaru::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    kernel.begin_main_thread(cpu, "main", 0x20u);
    Audio audio(kernel);
    register_audio(audio, runtime);

    // Free channels are allocated from 7 downwards.
    call(runtime, kReserve, 0x08804010u, {0xFFFFFFFFu, 448u, kFormatStereo});
    require(cpu.gpr[2] == 7u, "first automatic channel");
    call(runtime, kReserve, 0x08804020u, {0xFFFFFFFFu, 448u, kFormatStereo});
    require(cpu.gpr[2] == 6u, "second automatic channel");
    require(static_cast<std::uint32_t>(audio.reserve(7, 448u, kFormatStereo)) == kChannelAlreadyReserved &&
                static_cast<std::uint32_t>(audio.reserve(0, 100u, kFormatStereo)) == kSampleSizeNotAligned &&
                static_cast<std::uint32_t>(audio.reserve(0, 64u, 0x20u)) == kInvalidFormat,
            "reservation validation");
    call(runtime, kReserve, 0x08804030u, {8u, 448u, kFormatStereo});
    require(cpu.gpr[2] == kInvalidChannel, "guest reached the Output2 slot");

    // Two 448-sample blocks (10158 us each) are accepted at once; the third
    // waits until the first has played.
    call(runtime, kOutputPannedBlocking, 0x08804100u, {7u, 0x8000u, 0x8000u, kBuffer});
    require(cpu.gpr[2] == 448u && kernel.now_us() == 0u, "first block");
    call(runtime, kOutputPannedBlocking, 0x08804110u, {7u, 0x8000u, 0x8000u, kBuffer});
    require(cpu.gpr[2] == 448u && kernel.now_us() == 0u, "second block");
    call(runtime, kOutputPanned, 0x08804120u, {7u, 0x8000u, 0x8000u, kBuffer});
    require(cpu.gpr[2] == kChannelBusy, "non-blocking output to a full channel");
    call(runtime, kOutputPannedBlocking, 0x08804130u, {7u, 0x8000u, 0x8000u, kBuffer});
    require(kernel.now_us() == 10158u && cpu.pc == 0x08804130u && cpu.gpr[2] == 448u &&
                audio.channel(7u).samples_output == 3u * 448u,
            "blocking output did not wait for room");
    call(runtime, kRestLength, 0x08804140u, {7u});
    require(cpu.gpr[2] == 896u, "rest length");
    call(runtime, kOutputPannedBlocking, 0x08804150u, {7u, 0x10000u, 0u, kBuffer});
    require(cpu.gpr[2] == kInvalidVolume, "volume above maximum accepted");

    // Output2 is a separate channel.
    call(runtime, 0x01562BA3u, 0x08804200u, {1024u});
    require(cpu.gpr[2] == 0u && audio.channel(Audio::kOutput2).reserved, "Output2 reserve");
    call(runtime, 0x2D53F36Eu, 0x08804210u, {0x8000u, kBuffer});
    require(cpu.gpr[2] == 1024u, "Output2 output");
    call(runtime, 0x43196845u, 0x08804220u, {});
    require(cpu.gpr[2] == 0u && !audio.channel(Audio::kOutput2).reserved, "Output2 release");

    call(runtime, kRelease, 0x08804300u, {7u});
    require(cpu.gpr[2] == 0u, "release");
    call(runtime, kRelease, 0x08804310u, {7u});
    require(cpu.gpr[2] == kChannelNotReserved, "double release");
    return 0;
}
