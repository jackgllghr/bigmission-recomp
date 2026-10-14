#include "shell/audio_output.hpp"

#include <algorithm>

namespace sarusaru::shell {
namespace {

constexpr int kSampleRate = 44100;
// More than this queued on a channel means the guest ran ahead of the device
// (after a stall, say); the excess is dropped to keep latency bounded.
constexpr int kMaxQueuedBytes = kSampleRate / 4 * 2 * static_cast<int>(sizeof(std::int16_t));

} // namespace

AudioOutput::~AudioOutput() {
    for (auto *stream : streams_)
        if (stream != nullptr) SDL_DestroyAudioStream(stream);
    if (device_ != 0u) SDL_CloseAudioDevice(device_);
}

bool AudioOutput::open(std::string &error) {
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kSampleRate};
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (device_ == 0u) {
        error = std::string("cannot open the audio device: ") + SDL_GetError();
        return false;
    }
    for (auto &stream : streams_) {
        stream = SDL_CreateAudioStream(&spec, nullptr);
        if (stream == nullptr || !SDL_BindAudioStream(device_, stream)) {
            error = std::string("cannot create an audio stream: ") + SDL_GetError();
            return false;
        }
    }
    return true;
}

void AudioOutput::play(std::int32_t channel, std::span<const std::int16_t> samples, bool mono,
                       std::uint32_t left_volume, std::uint32_t right_volume) {
    if (channel < 0 || static_cast<std::size_t>(channel) >= kStreams || samples.empty()) return;
    SDL_AudioStream *stream = streams_[static_cast<std::size_t>(channel)];
    if (stream == nullptr) return;
    const std::size_t frames = mono ? samples.size() : samples.size() / 2u;
    scratch_.resize(frames * 2u);
    const auto scale = [](std::int16_t sample, std::uint32_t volume) {
        const std::int32_t scaled = static_cast<std::int32_t>(sample) * static_cast<std::int32_t>(std::min(volume, 0xFFFFu)) / 0x8000;
        return static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767));
    };
    for (std::size_t i = 0; i < frames; ++i) {
        const std::int16_t left = mono ? samples[i] : samples[2u * i];
        const std::int16_t right = mono ? samples[i] : samples[2u * i + 1u];
        scratch_[2u * i] = scale(left, left_volume);
        scratch_[2u * i + 1u] = scale(right, right_volume);
    }
    if (SDL_GetAudioStreamQueued(stream) > kMaxQueuedBytes) SDL_ClearAudioStream(stream);
    SDL_PutAudioStreamData(stream, scratch_.data(), static_cast<int>(scratch_.size() * sizeof(std::int16_t)));
}

void AudioOutput::clear() {
    for (auto *stream : streams_)
        if (stream != nullptr) SDL_ClearAudioStream(stream);
}

} // namespace sarusaru::shell
