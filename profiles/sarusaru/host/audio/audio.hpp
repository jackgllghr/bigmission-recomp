#pragma once

#include "kernel/early_kernel.hpp"
#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>

namespace sarusaru::audio {

inline constexpr std::uint32_t kChannelNotInitialized = 0x80260001u;
inline constexpr std::uint32_t kChannelBusy = 0x80260002u;
inline constexpr std::uint32_t kInvalidChannel = 0x80260003u;
inline constexpr std::uint32_t kNoChannelsAvailable = 0x80260005u;
inline constexpr std::uint32_t kSampleSizeNotAligned = 0x80260006u;
inline constexpr std::uint32_t kInvalidFormat = 0x80260007u;
inline constexpr std::uint32_t kChannelNotReserved = 0x80260008u;
inline constexpr std::uint32_t kInvalidVolume = 0x8026000Bu;
inline constexpr std::uint32_t kChannelAlreadyReserved = 0x80268002u;

inline constexpr std::uint32_t kChannelCount = 8u;
inline constexpr std::uint32_t kSampleRate = 44100u;
inline constexpr std::uint32_t kFormatStereo = 0x00u;
inline constexpr std::uint32_t kFormatMono = 0x10u;
inline constexpr std::uint32_t kMaxVolume = 0xFFFFu;
// A channel accepts a block while at most this many are still playing.
inline constexpr std::uint32_t kBlocksInFlight = 2u;

// sceAudio output channels on the kernel's virtual clock. Each block plays
// for sample_count / 44100 s; blocking output waits until a channel has room,
// which paces the guest audio thread. Samples are counted, not played: host
// output arrives with the SDL shell.
class Audio {
public:
    struct Channel {
        bool reserved{};
        std::uint32_t sample_count{};
        std::uint32_t format{};
        std::uint32_t left_volume{kMaxVolume};
        std::uint32_t right_volume{kMaxVolume};
        std::uint64_t queued_until_us{};
        std::uint64_t samples_output{};
    };

    explicit Audio(kernel::EarlyKernel &kernel) noexcept : kernel_(&kernel) {}

    [[nodiscard]] std::int32_t reserve(std::int32_t channel, std::uint32_t samples, std::uint32_t format);
    [[nodiscard]] std::int32_t reserve_output2(std::uint32_t samples);
    // Channels 0-7, or kOutput2 for the Output2 channel.
    [[nodiscard]] std::int32_t release(std::int32_t channel);
    [[nodiscard]] std::int32_t set_data_length(std::int32_t channel, std::uint32_t samples);
    [[nodiscard]] std::int32_t change_config(std::int32_t channel, std::uint32_t format);
    [[nodiscard]] std::int32_t change_volume(std::int32_t channel, std::uint32_t left, std::uint32_t right);
    [[nodiscard]] std::int32_t rest_length(std::int32_t channel) const;
    // Queues one block on channel 0-7 or kOutput2; blocking waits for room,
    // non-blocking fails with kChannelBusy.
    void output(psprecomp::AllegrexContext &context, std::int32_t channel, std::uint32_t left,
                std::uint32_t right, std::uint32_t buffer, bool blocking);

    [[nodiscard]] const Channel &channel(std::uint32_t index) const noexcept { return channels_[index]; }
    // The Output2 (sample-rate converter) channel, kept after the eight normal ones.
    static constexpr std::int32_t kOutput2 = static_cast<std::int32_t>(kChannelCount);

private:
    kernel::EarlyKernel *kernel_;
    std::array<Channel, kChannelCount + 1u> channels_{};

    [[nodiscard]] Channel *reserved_channel(std::int32_t channel) noexcept;
    [[nodiscard]] std::uint64_t block_duration_us(const Channel &channel) const noexcept;
    std::uint32_t enqueue(Channel &channel);
};

void register_audio(Audio &audio, psprecomp::Runtime &runtime);

} // namespace sarusaru::audio
