#pragma once

// Plays the guest's sceAudio channels through SDL: one SDL audio stream per
// guest channel, all bound to the default playback device, which mixes them.
// Blocks arrive from audio::Audio's sink on the game thread as the guest
// queues them; the guest clock is paced to real time, so the device drains
// them at the rate they come.
#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sarusaru::shell {

class AudioOutput {
public:
    ~AudioOutput();
    bool open(std::string &error);
    // One guest block: interleaved samples, mono or stereo, volumes 0x8000 = unity.
    void play(std::int32_t channel, std::span<const std::int16_t> samples, bool mono, std::uint32_t left_volume,
              std::uint32_t right_volume);
    // Drops what is queued: for when what was queued no longer belongs to
    // what plays next (a menu opened, a state loaded).
    void clear();

private:
    static constexpr std::size_t kStreams = 9u; // eight channels and Output2
    SDL_AudioDeviceID device_{};
    std::array<SDL_AudioStream *, kStreams> streams_{};
    std::vector<std::int16_t> scratch_;
};

} // namespace sarusaru::shell
