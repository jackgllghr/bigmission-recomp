#pragma once
#include "save_state/session_access.hpp"

#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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

// The SAS effect: the PlayStation SPU reverb (nocash's psx-spx formula and
// preset table), run at 22050 Hz on the voices' effect sends. PSP types are
// -1 off, 0 room, 1-3 studio small/medium/large, 4 hall, 5 space, 6 echo,
// 7 delay and 8 pipe. Echo and delay derive their length and feedback from
// sceSasRevParam; the other presets ignore it.
class Reverb {
    friend struct sarusaru::save_state::SessionCodec;
public:
    // Registers in psx-spx order. m/d registers count 8-byte units of the
    // work buffer relative to the moving buffer position; v registers are
    // signed 1.15 volumes.
    struct Registers {
        std::uint16_t dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL;
        std::uint16_t vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2;
        std::uint16_t dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4;
        std::uint16_t dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN;
    };

    static constexpr std::int32_t kOff = -1, kEcho = 6, kDelay = 7, kMaxType = 8;

    // Selecting a type (even the same one) clears the work buffer.
    void set_type(std::int32_t type);
    void set_parameters(std::int32_t delay, std::int32_t feedback);
    [[nodiscard]] std::int32_t type() const noexcept { return type_; }
    [[nodiscard]] const Registers &registers() const noexcept { return registers_; }
    // One grain of 44.1 kHz sends (an even count), replaced in place by the
    // wet signal scaled by the effect volumes (0x1000 = unity).
    void process(std::span<std::int64_t> left, std::span<std::int64_t> right, std::int32_t volume_left,
                 std::int32_t volume_right);

private:
    std::int32_t type_{kOff}, delay_{}, feedback_{};
    Registers registers_{};
    std::vector<std::int16_t> buffer_;
    std::size_t position_{};
    std::int32_t previous_left_{}, previous_right_{};

    void load_registers();
    [[nodiscard]] std::int16_t &at(std::int32_t halfwords) noexcept;
    void step(std::int32_t in_left, std::int32_t in_right, std::int32_t &out_left, std::int32_t &out_right) noexcept;
};

// sceSasCore: up to 32 voices from VAG or noise sources, pitch-resampled
// with linear interpolation, shaped by their envelope and mixed per grain
// into stereo 16-bit output. Effect sends feed the reverb, whose wet output
// is mixed in when sceSasRevVON turns it on (the dry voices likewise).
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
    [[nodiscard]] const Reverb &reverb() const noexcept { return reverb_; }

private:
    std::uint32_t core_{};
    std::uint32_t grain_{};
    std::uint32_t max_voices_{};
    std::uint32_t output_mode_{};
    std::array<Voice, kMaxVoices> voices_{};
    std::uint32_t noise_state_{1u};
    Reverb reverb_;
    std::int32_t reverb_left_{}, reverb_right_{};
    std::int32_t dry_on_{1}, wet_on_{};

    [[nodiscard]] Voice *voice_at(std::int32_t index) noexcept;
    [[nodiscard]] std::int16_t next_source_sample(Voice &voice, const psprecomp::GuestMemory &memory);
};

void register_sas(SasCore &sas, psprecomp::Runtime &runtime);

} // namespace sarusaru::sas
