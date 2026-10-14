#include "audio/audio.hpp"

#include <algorithm>

namespace sarusaru::audio {
namespace {
bool valid_sample_count(std::uint32_t samples) noexcept {
    return samples >= 64u && samples <= 65472u && samples % 64u == 0u;
}
}

Audio::Channel *Audio::reserved_channel(std::int32_t channel) noexcept {
    if (channel < 0 || channel > kOutput2 || !channels_[static_cast<std::size_t>(channel)].reserved) return nullptr;
    return &channels_[static_cast<std::size_t>(channel)];
}

std::uint64_t Audio::block_duration_us(const Channel &channel) const noexcept {
    return static_cast<std::uint64_t>(channel.sample_count) * 1'000'000u / kSampleRate;
}

std::int32_t Audio::reserve(std::int32_t channel, std::uint32_t samples, std::uint32_t format) {
    if (channel >= static_cast<std::int32_t>(kChannelCount) || channel == -2) return static_cast<std::int32_t>(kInvalidChannel);
    if (channel < 0) {
        // Free channels are handed out from the highest number down.
        channel = -1;
        for (std::int32_t index = static_cast<std::int32_t>(kChannelCount) - 1; index >= 0; --index) {
            if (!channels_[static_cast<std::size_t>(index)].reserved) { channel = index; break; }
        }
        if (channel < 0) return static_cast<std::int32_t>(kNoChannelsAvailable);
    }
    Channel &target = channels_[static_cast<std::size_t>(channel)];
    if (target.reserved) return static_cast<std::int32_t>(kChannelAlreadyReserved);
    if (!valid_sample_count(samples)) return static_cast<std::int32_t>(kSampleSizeNotAligned);
    if (format != kFormatStereo && format != kFormatMono) return static_cast<std::int32_t>(kInvalidFormat);
    target = Channel{true, samples, format};
    return channel;
}

std::int32_t Audio::reserve_output2(std::uint32_t samples) {
    Channel &target = channels_[static_cast<std::size_t>(kOutput2)];
    if (target.reserved) return static_cast<std::int32_t>(kChannelAlreadyReserved);
    if (!valid_sample_count(samples)) return static_cast<std::int32_t>(kSampleSizeNotAligned);
    target = Channel{true, samples, kFormatStereo};
    return 0;
}

std::int32_t Audio::release(std::int32_t channel) {
    if (channel < 0 || channel > kOutput2) return static_cast<std::int32_t>(kInvalidChannel);
    Channel *target = reserved_channel(channel);
    if (target == nullptr) return static_cast<std::int32_t>(kChannelNotReserved);
    target->reserved = false;
    return 0;
}

std::int32_t Audio::set_data_length(std::int32_t channel, std::uint32_t samples) {
    if (channel < 0 || channel >= static_cast<std::int32_t>(kChannelCount)) return static_cast<std::int32_t>(kInvalidChannel);
    Channel *target = reserved_channel(channel);
    if (target == nullptr) return static_cast<std::int32_t>(kChannelNotInitialized);
    if (!valid_sample_count(samples)) return static_cast<std::int32_t>(kSampleSizeNotAligned);
    target->sample_count = samples;
    return 0;
}

std::int32_t Audio::change_config(std::int32_t channel, std::uint32_t format) {
    if (channel < 0 || channel >= static_cast<std::int32_t>(kChannelCount)) return static_cast<std::int32_t>(kInvalidChannel);
    Channel *target = reserved_channel(channel);
    if (target == nullptr) return static_cast<std::int32_t>(kChannelNotInitialized);
    if (format != kFormatStereo && format != kFormatMono) return static_cast<std::int32_t>(kInvalidFormat);
    target->format = format;
    return 0;
}

std::int32_t Audio::change_volume(std::int32_t channel, std::uint32_t left, std::uint32_t right) {
    if (channel < 0 || channel >= static_cast<std::int32_t>(kChannelCount)) return static_cast<std::int32_t>(kInvalidChannel);
    if (left > kMaxVolume || right > kMaxVolume) return static_cast<std::int32_t>(kInvalidVolume);
    Channel *target = reserved_channel(channel);
    if (target == nullptr) return static_cast<std::int32_t>(kChannelNotInitialized);
    target->left_volume = left;
    target->right_volume = right;
    return 0;
}

std::int32_t Audio::rest_length(std::int32_t channel) const {
    if (channel < 0 || channel >= static_cast<std::int32_t>(kChannelCount)) return static_cast<std::int32_t>(kInvalidChannel);
    const Channel &target = channels_[static_cast<std::size_t>(channel)];
    if (!target.reserved) return static_cast<std::int32_t>(kChannelNotInitialized);
    const std::uint64_t now = kernel_->now_us();
    if (target.queued_until_us <= now) return 0;
    return static_cast<std::int32_t>(((target.queued_until_us - now) * kSampleRate + 999'999u) / 1'000'000u);
}

std::uint32_t Audio::enqueue(Channel &channel) {
    const std::uint64_t start = std::max(channel.queued_until_us, kernel_->now_us());
    channel.queued_until_us = start + block_duration_us(channel);
    channel.samples_output += channel.sample_count;
    return channel.sample_count;
}

void Audio::output(psprecomp::AllegrexContext &context, std::int32_t channel, std::uint32_t left,
                   std::uint32_t right, std::uint32_t buffer, bool blocking) {
    (void)buffer;  // Not mixed yet; see the class comment.
    if (channel < 0 || channel > kOutput2) { context.set_gpr(2u, kInvalidChannel); return; }
    if (left > kMaxVolume || right > kMaxVolume) { context.set_gpr(2u, kInvalidVolume); return; }
    Channel *target = reserved_channel(channel);
    if (target == nullptr) { context.set_gpr(2u, kChannelNotInitialized); return; }
    target->left_volume = left;
    target->right_volume = right;
    // Room once no more than kBlocksInFlight - 1 blocks remain ahead of now.
    const std::uint64_t room_at = target->queued_until_us > (kBlocksInFlight - 1u) * block_duration_us(*target)
        ? target->queued_until_us - (kBlocksInFlight - 1u) * block_duration_us(*target) : 0u;
    if (room_at <= kernel_->now_us()) {
        context.set_gpr(2u, enqueue(*target));
        return;
    }
    if (!blocking) { context.set_gpr(2u, kChannelBusy); return; }
    kernel_->block_current(context, {"audio output", room_at, 0u, [this, target] { return enqueue(*target); }, 0, false});
}

void register_audio(Audio &audio, psprecomp::Runtime &runtime) {
    // Guest channel numbers never reach the internal Output2 slot.
    const auto ch = [](std::uint32_t value) {
        const auto channel = static_cast<std::int32_t>(value);
        return channel >= static_cast<std::int32_t>(kChannelCount) ? std::int32_t{-2} : channel;
    };
    runtime.register_hle("sceAudio", 0x5EC81C55u,  // sceAudioChReserve
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.reserve(ch(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("sceAudio", 0x6FC46853u,  // sceAudioChRelease
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.release(ch(ctx.gpr[4]))));
        });
    runtime.register_hle("sceAudio", 0xCB2E439Eu,  // sceAudioSetChannelDataLen
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.set_data_length(ch(ctx.gpr[4]), ctx.gpr[5])));
        });
    runtime.register_hle("sceAudio", 0x95FD0C2Du,  // sceAudioChangeChannelConfig
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.change_config(ch(ctx.gpr[4]), ctx.gpr[5])));
        });
    runtime.register_hle("sceAudio", 0xB7E1D8E7u,  // sceAudioChangeChannelVolume
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.change_volume(ch(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("sceAudio", 0xB011922Fu,  // sceAudioGetChannelRestLength
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.rest_length(ch(ctx.gpr[4]))));
        });
    runtime.register_hle("sceAudio", 0x136CAF51u,  // sceAudioOutputBlocking(channel, volume, buffer)
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            audio.output(ctx, ch(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[5], ctx.gpr[6], true);
        });
    runtime.register_hle("sceAudio", 0x13F592BCu,  // sceAudioOutputPannedBlocking(channel, left, right, buffer)
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            audio.output(ctx, ch(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], true);
        });
    runtime.register_hle("sceAudio", 0xE2D56B2Du,  // sceAudioOutputPanned
        [&audio, ch](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            audio.output(ctx, ch(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], false);
        });
    runtime.register_hle("sceAudio", 0x01562BA3u,  // sceAudioOutput2Reserve(samples)
        [&audio](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.reserve_output2(ctx.gpr[4])));
        });
    runtime.register_hle("sceAudio", 0x2D53F36Eu,  // sceAudioOutput2OutputBlocking(volume, buffer)
        [&audio](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            audio.output(ctx, Audio::kOutput2, ctx.gpr[4], ctx.gpr[4], ctx.gpr[5], true);
        });
    runtime.register_hle("sceAudio", 0x43196845u,  // sceAudioOutput2Release
        [&audio](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(audio.release(Audio::kOutput2)));
        });
}

} // namespace sarusaru::audio
