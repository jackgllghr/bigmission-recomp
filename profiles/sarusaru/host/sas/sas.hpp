#pragma once
#include "save_state/session_access.hpp"

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace sarusaru::sas {

inline constexpr std::uint32_t kInvalidGrain = 0x80420001u;
inline constexpr std::uint32_t kInvalidMaxVoices = 0x80420002u;
inline constexpr std::uint32_t kInvalidOutputMode = 0x80420003u;
inline constexpr std::uint32_t kInvalidSampleRate = 0x80420004u;
inline constexpr std::uint32_t kBadAddress = 0x80420005u;
inline constexpr std::uint32_t kInvalidVoice = 0x80420010u;
inline constexpr std::uint32_t kInvalidPitch = 0x80420011u;
inline constexpr std::uint32_t kInvalidCurveMode = 0x80420013u;
inline constexpr std::uint32_t kInvalidParameter = 0x80420014u;
inline constexpr std::uint32_t kInvalidLoop = 0x80420015u;
inline constexpr std::uint32_t kVoicePaused = 0x80420016u;
inline constexpr std::uint32_t kInvalidVolume = 0x80420018u;
inline constexpr std::uint32_t kInvalidRate = 0x80420019u;
inline constexpr std::uint32_t kInvalidReverbType = 0x80420020u;
inline constexpr std::uint32_t kInvalidReverbFeedback = 0x80420021u;
inline constexpr std::uint32_t kInvalidReverbDelay = 0x80420022u;
inline constexpr std::uint32_t kInvalidReverbVolume = 0x80420023u;
inline constexpr std::uint32_t kNotInitialized = 0x80420100u;

inline constexpr std::uint32_t kMaxVoices = 32u;
inline constexpr std::uint32_t kPitchUnity = 0x1000u;
inline constexpr std::uint32_t kPitchMax = 0x4000u;
inline constexpr std::int32_t kVolumeMax = 0x1000;
inline constexpr std::int64_t kEnvelopeMax = 0x40000000;

enum class Curve : std::uint32_t {
    LinearIncrease = 0, LinearDecrease = 1, LinearBent = 2, ExponentDecrease = 3, ExponentIncrease = 4, Direct = 5,
};

// VAG ADPCM: 16-byte blocks of 28 samples. Flags: bit 2 marks the loop
// start, bit 0 the end of the data, bit 1 with it (3) an end that jumps back
// to the loop start when the voice loops, and 7 an unconditional end.
class VagDecoder {
    friend struct sarusaru::save_state::SessionCodec;
public:
    void start(std::uint32_t address, std::uint32_t size, bool loop) noexcept;
    // Next sample, or nullopt once the data has ended.
    [[nodiscard]] std::optional<std::int16_t> next(const psprecomp::GuestMemory &memory);
    [[nodiscard]] bool ended() const noexcept { return ended_; }

private:
    std::uint32_t start_{}, size_{}, block_{}, loop_block_{};
    bool loop_{}, has_loop_start_{}, ended_{true}, end_after_block_{};
    std::array<std::int16_t, 28> samples_{};
    std::uint32_t index_{28u};
    std::int32_t history1_{}, history2_{};
    bool decode_block(const psprecomp::GuestMemory &memory);
};

// ADSR envelope with height 0..0x40000000, stepped once per output sample.
class Envelope {
    friend struct sarusaru::save_state::SessionCodec;
public:
    enum class Phase { Off, Attack, Decay, Sustain, Release };
    struct Stage { Curve curve{Curve::LinearIncrease}; std::int64_t rate{}; };

    void set_simple(std::uint32_t adsr1, std::uint32_t adsr2) noexcept;
    void key_on() noexcept { phase_ = Phase::Attack; height_ = 0; }
    void key_off() noexcept { if (phase_ != Phase::Off) phase_ = Phase::Release; }
    void stop() noexcept { phase_ = Phase::Off; height_ = 0; }
    void step() noexcept;

    Stage attack{Curve::LinearIncrease, 0}, decay{Curve::ExponentDecrease, 0}, sustain{Curve::LinearDecrease, 0},
        release{Curve::LinearDecrease, 0};
    std::int64_t sustain_level{kEnvelopeMax};

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] std::int64_t height() const noexcept { return height_; }

private:
    Phase phase_{Phase::Off};
    std::int64_t height_{};
    void walk(const Stage &stage) noexcept;
};

// sceSasCore: up to 32 voices from VAG or noise sources, pitch-resampled
// with linear interpolation, shaped by their envelope and mixed per grain
// into stereo 16-bit output. Reverb settings are recorded but not applied:
// effect sends are dropped.
class SasCore {
    friend struct sarusaru::save_state::SessionCodec;
public:
    struct Voice {
        bool configured{}, on{}, paused{}, noise{}, primed{};
        std::uint32_t vag_address{}, vag_size{};
        bool loop{};
        VagDecoder vag;
        std::uint32_t pitch{kPitchUnity};
        std::uint32_t noise_frequency{};
        std::int32_t left{}, right{}, effect_left{}, effect_right{};
        Envelope envelope;
        std::uint32_t position{};  // 12-bit fraction between the two current samples.
        std::int16_t current{}, following{};
    };

    [[nodiscard]] std::int32_t init(std::uint32_t core, std::uint32_t grain, std::uint32_t voices,
                                    std::uint32_t output_mode, std::uint32_t sample_rate);
    [[nodiscard]] bool initialized() const noexcept { return core_ != 0u; }
    [[nodiscard]] std::int32_t set_voice(std::int32_t voice, std::uint32_t address, std::int32_t size,
                                         std::uint32_t loop, const psprecomp::GuestMemory &memory);
    [[nodiscard]] std::int32_t set_noise(std::int32_t voice, std::uint32_t frequency);
    [[nodiscard]] std::int32_t set_pitch(std::int32_t voice, std::uint32_t pitch);
    [[nodiscard]] std::int32_t set_volume(std::int32_t voice, std::int32_t left, std::int32_t right,
                                          std::int32_t effect_left, std::int32_t effect_right);
    [[nodiscard]] std::int32_t key_on(std::int32_t voice);
    [[nodiscard]] std::int32_t key_off(std::int32_t voice);
    [[nodiscard]] std::int32_t set_pause(std::uint32_t voice_bits, bool pause);
    [[nodiscard]] std::int32_t set_simple_adsr(std::int32_t voice, std::uint32_t adsr1, std::uint32_t adsr2);
    [[nodiscard]] std::int32_t set_adsr(std::int32_t voice, std::uint32_t flags, const std::array<std::int32_t, 4> &rates);
    [[nodiscard]] std::int32_t set_adsr_mode(std::int32_t voice, std::uint32_t flags, const std::array<std::uint32_t, 4> &modes);
    [[nodiscard]] std::int32_t set_sustain_level(std::int32_t voice, std::int32_t level);
    [[nodiscard]] std::int32_t envelope_height(std::int32_t voice) const;
    [[nodiscard]] std::uint32_t end_flags() const noexcept;
    [[nodiscard]] std::uint32_t pause_flags() const noexcept;
    [[nodiscard]] std::int32_t set_grain(std::uint32_t grain);
    [[nodiscard]] std::uint32_t grain() const noexcept { return grain_; }
    [[nodiscard]] std::int32_t set_output_mode(std::uint32_t mode);
    [[nodiscard]] std::uint32_t output_mode() const noexcept { return output_mode_; }
    [[nodiscard]] std::int32_t set_reverb_type(std::int32_t type);
    [[nodiscard]] std::int32_t set_reverb_parameters(std::int32_t delay, std::int32_t feedback);
    [[nodiscard]] std::int32_t set_reverb_volume(std::int32_t left, std::int32_t right);
    [[nodiscard]] std::int32_t set_reverb_voice_on(std::int32_t dry, std::int32_t wet);
    // Mixes one grain. With `mix`, existing samples are scaled by the
    // left/right volumes (0x1000 = unity) and the voices added on top.
    [[nodiscard]] std::int32_t render(psprecomp::GuestMemory &memory, std::uint32_t output, bool mix,
                                      std::int32_t mix_left, std::int32_t mix_right);

    [[nodiscard]] const Voice &voice(std::uint32_t index) const noexcept { return voices_[index]; }

private:
    std::uint32_t core_{};
    std::uint32_t grain_{};
    std::uint32_t max_voices_{};
    std::uint32_t output_mode_{};
    std::array<Voice, kMaxVoices> voices_{};
    std::uint32_t noise_state_{1u};
    std::int32_t reverb_type_{}, reverb_delay_{}, reverb_feedback_{}, reverb_left_{}, reverb_right_{};
    std::int32_t dry_on_{1}, wet_on_{};

    [[nodiscard]] Voice *voice_at(std::int32_t index) noexcept;
    [[nodiscard]] std::int16_t next_source_sample(Voice &voice, const psprecomp::GuestMemory &memory);
};

void register_sas(SasCore &sas, psprecomp::Runtime &runtime);

} // namespace sarusaru::sas
