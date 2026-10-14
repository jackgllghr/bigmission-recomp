#include "sas/sas.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace sarusaru::sas {
namespace {
constexpr std::int32_t kVagFilters[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

std::int16_t clamp16(std::int64_t value) noexcept {
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(value, -32768, 32767));
}

std::int64_t simple_rate(std::uint32_t n) noexcept {
    n &= 0x7Fu;
    if (n == 0x7Fu) return 0;
    const std::int64_t rate = (static_cast<std::int64_t>(7u - (n & 3u)) << 26u) >> (n >> 2u);
    return rate == 0 ? 1 : rate;
}

std::int64_t exponent_rate(std::uint32_t n) noexcept {
    n &= 0x7Fu;
    if (n == 0x7Fu) return 0;
    const std::int64_t rate = (static_cast<std::int64_t>(7u - (n & 3u)) << 24u) >> (n >> 2u);
    return rate == 0 ? 1 : rate;
}
}

void VagDecoder::start(std::uint32_t address, std::uint32_t size, bool loop) noexcept {
    start_ = address;
    size_ = size;
    loop_ = loop;
    block_ = 0u;
    loop_block_ = 0u;
    has_loop_start_ = false;
    ended_ = false;
    end_after_block_ = false;
    index_ = 28u;
    history1_ = history2_ = 0;
}

bool VagDecoder::decode_block(const psprecomp::GuestMemory &memory) {
    const std::uint64_t offset = static_cast<std::uint64_t>(block_) * 16u;
    const std::uint32_t address = start_ + static_cast<std::uint32_t>(offset);
    if (offset + 16u > size_ || !memory.contains(address, 16u)) {
        ended_ = true;
        return false;
    }
    const std::uint8_t header = memory.load8(address);
    const std::uint8_t flags = memory.load8(address + 1u);
    if (flags == 7u) {
        ended_ = true;
        return false;
    }
    if ((flags & 4u) != 0u) {
        loop_block_ = block_;
        has_loop_start_ = true;
    }
    const std::uint32_t shift = header & 0x0Fu;
    const std::uint32_t filter = std::min<std::uint32_t>(header >> 4u, 4u);
    for (std::uint32_t i = 0; i < 14u; ++i) {
        const std::uint8_t byte = memory.load8(address + 2u + i);
        for (std::uint32_t nibble = 0; nibble < 2u; ++nibble) {
            const auto raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(((byte >> (nibble * 4u)) & 0x0Fu) << 12u));
            const std::int32_t sample = (static_cast<std::int32_t>(raw) >> shift) +
                ((history1_ * kVagFilters[filter][0] + history2_ * kVagFilters[filter][1]) >> 6);
            history2_ = history1_;
            history1_ = clamp16(sample);
            samples_[i * 2u + nibble] = static_cast<std::int16_t>(history1_);
        }
    }
    index_ = 0u;
    if ((flags & 1u) != 0u) {
        // Last block: loop back (to the marked start, else the beginning) or end after it.
        if (loop_) block_ = has_loop_start_ ? loop_block_ : 0u;
        else end_after_block_ = true;
    } else {
        ++block_;
    }
    return true;
}

std::optional<std::int16_t> VagDecoder::next(const psprecomp::GuestMemory &memory) {
    if (ended_) return std::nullopt;
    if (index_ >= samples_.size()) {
        if (end_after_block_) {
            ended_ = true;
            return std::nullopt;
        }
        if (!decode_block(memory)) return std::nullopt;
    }
    return samples_[index_++];
}

void Envelope::set_simple(std::uint32_t adsr1, std::uint32_t adsr2) noexcept {
    attack.curve = (adsr1 & 0x8000u) != 0u ? Curve::LinearBent : Curve::LinearIncrease;
    attack.rate = simple_rate(adsr1 >> 8u);
    const std::uint32_t decay_shift = (adsr1 >> 4u) & 0x0Fu;
    decay = {Curve::ExponentDecrease, decay_shift == 0u ? 0x7FFFFFFF : static_cast<std::int64_t>(0x80000000u >> decay_shift)};
    sustain_level = static_cast<std::int64_t>((adsr1 & 0x0Fu) + 1u) << 26u;
    static constexpr Curve sustain_curves[4] = {Curve::LinearIncrease, Curve::LinearBent, Curve::LinearDecrease,
                                                Curve::ExponentDecrease};
    sustain.curve = sustain_curves[(adsr2 >> 14u) & 3u];
    sustain.rate = sustain.curve == Curve::ExponentDecrease ? exponent_rate(adsr2 >> 6u) : simple_rate(adsr2 >> 6u);
    const std::uint32_t release_shift = adsr2 & 0x1Fu;
    if ((adsr2 & 0x20u) != 0u) {
        release = {Curve::ExponentDecrease,
                   release_shift == 0u ? 0x7FFFFFFF : static_cast<std::int64_t>(0x80000000u >> release_shift)};
    } else {
        release.curve = Curve::LinearDecrease;
        release.rate = release_shift == 31u ? 0 : release_shift == 30u ? 0x40000000
                     : release_shift == 29u ? 1 : static_cast<std::int64_t>(0x10000000u >> release_shift);
    }
}

void Envelope::walk(const Stage &stage) noexcept {
    switch (stage.curve) {
    case Curve::LinearIncrease: height_ += stage.rate; break;
    case Curve::LinearDecrease: height_ -= stage.rate; break;
    case Curve::LinearBent: height_ += height_ <= kEnvelopeMax * 3 / 4 ? stage.rate : stage.rate / 4; break;
    case Curve::ExponentDecrease: height_ -= std::max<std::int64_t>(1, (height_ * stage.rate) >> 31); break;
    case Curve::ExponentIncrease: height_ += std::max<std::int64_t>(1, ((kEnvelopeMax - height_) * stage.rate) >> 31); break;
    case Curve::Direct: height_ = stage.rate; break;
    }
    height_ = std::clamp<std::int64_t>(height_, 0, kEnvelopeMax);
}

void Envelope::step() noexcept {
    switch (phase_) {
    case Phase::Off:
        return;
    case Phase::Attack:
        walk(attack);
        if (height_ >= kEnvelopeMax) phase_ = Phase::Decay;
        break;
    case Phase::Decay:
        walk(decay);
        if (height_ <= sustain_level) {
            height_ = sustain_level;
            phase_ = Phase::Sustain;
        }
        break;
    case Phase::Sustain:
        walk(sustain);
        break;
    case Phase::Release:
        walk(release);
        if (height_ == 0) phase_ = Phase::Off;
        break;
    }
}

SasCore::Voice *SasCore::voice_at(std::int32_t index) noexcept {
    if (index < 0 || static_cast<std::uint32_t>(index) >= kMaxVoices) return nullptr;
    return &voices_[static_cast<std::size_t>(index)];
}

std::int32_t SasCore::init(std::uint32_t core, std::uint32_t grain, std::uint32_t voices, std::uint32_t output_mode,
                           std::uint32_t sample_rate) {
    if ((core & 0x3Fu) != 0u || core == 0u) return static_cast<std::int32_t>(kBadAddress);
    if (grain < 0x40u || grain > 0x800u || (grain & 0x1Fu) != 0u) return static_cast<std::int32_t>(kInvalidGrain);
    if (voices == 0u || voices > kMaxVoices) return static_cast<std::int32_t>(kInvalidMaxVoices);
    if (output_mode > 1u) return static_cast<std::int32_t>(kInvalidOutputMode);
    if (sample_rate != 44100u) return static_cast<std::int32_t>(kInvalidSampleRate);
    core_ = core;
    grain_ = grain;
    max_voices_ = voices;
    output_mode_ = output_mode;
    voices_ = {};
    return 0;
}

std::int32_t SasCore::set_voice(std::int32_t index, std::uint32_t address, std::int32_t size, std::uint32_t loop,
                                const psprecomp::GuestMemory &memory) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    if (size <= 0 || (size & 0x0F) != 0) return static_cast<std::int32_t>(kInvalidParameter);
    if (loop > 1u) return static_cast<std::int32_t>(kInvalidLoop);
    if (!memory.contains(address, static_cast<std::size_t>(size))) return static_cast<std::int32_t>(kBadAddress);
    voice->configured = true;
    voice->noise = false;
    voice->vag_address = address;
    voice->vag_size = static_cast<std::uint32_t>(size);
    voice->loop = loop != 0u;
    return 0;
}

std::int32_t SasCore::set_noise(std::int32_t index, std::uint32_t frequency) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    if (frequency > 0x3Fu) return static_cast<std::int32_t>(kInvalidParameter);
    voice->configured = true;
    voice->noise = true;
    voice->noise_frequency = frequency;
    return 0;
}

std::int32_t SasCore::set_pitch(std::int32_t index, std::uint32_t pitch) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    if (pitch > kPitchMax) return static_cast<std::int32_t>(kInvalidPitch);
    voice->pitch = pitch;
    return 0;
}

std::int32_t SasCore::set_volume(std::int32_t index, std::int32_t left, std::int32_t right, std::int32_t effect_left,
                                 std::int32_t effect_right) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    for (const auto volume : {left, right, effect_left, effect_right}) {
        if (std::abs(volume) > kVolumeMax) return static_cast<std::int32_t>(kInvalidVolume);
    }
    voice->left = left;
    voice->right = right;
    voice->effect_left = effect_left;
    voice->effect_right = effect_right;
    return 0;
}

std::int32_t SasCore::key_on(std::int32_t index) {
    Voice *voice = voice_at(index);
    if (voice == nullptr || static_cast<std::uint32_t>(index) >= max_voices_) return static_cast<std::int32_t>(kInvalidVoice);
    if (voice->paused) return static_cast<std::int32_t>(kVoicePaused);
    if (!voice->configured) return static_cast<std::int32_t>(kInvalidParameter);
    if (!voice->noise) voice->vag.start(voice->vag_address, voice->vag_size, voice->loop);
    voice->envelope.key_on();
    voice->on = true;
    voice->primed = false;
    voice->position = 0u;
    return 0;
}

std::int32_t SasCore::key_off(std::int32_t index) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    if (voice->paused) return static_cast<std::int32_t>(kVoicePaused);
    voice->envelope.key_off();
    return 0;
}

std::int32_t SasCore::set_pause(std::uint32_t voice_bits, bool pause) {
    for (std::uint32_t index = 0; index < kMaxVoices; ++index) {
        if ((voice_bits & (1u << index)) != 0u) voices_[index].paused = pause;
    }
    return 0;
}

std::int32_t SasCore::set_simple_adsr(std::int32_t index, std::uint32_t adsr1, std::uint32_t adsr2) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    if (((adsr1 | adsr2) & 0xFFFF0000u) != 0u) return static_cast<std::int32_t>(kInvalidParameter);
    voice->envelope.set_simple(adsr1, adsr2);
    return 0;
}

std::int32_t SasCore::set_adsr(std::int32_t index, std::uint32_t flags, const std::array<std::int32_t, 4> &rates) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    Envelope::Stage *stages[4] = {&voice->envelope.attack, &voice->envelope.decay, &voice->envelope.sustain,
                                  &voice->envelope.release};
    for (std::uint32_t i = 0; i < 4u; ++i) {
        if ((flags & (1u << i)) != 0u && rates[i] < 0) return static_cast<std::int32_t>(kInvalidRate);
    }
    for (std::uint32_t i = 0; i < 4u; ++i) {
        if ((flags & (1u << i)) != 0u) stages[i]->rate = rates[i];
    }
    return 0;
}

std::int32_t SasCore::set_adsr_mode(std::int32_t index, std::uint32_t flags, const std::array<std::uint32_t, 4> &modes) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    Envelope::Stage *stages[4] = {&voice->envelope.attack, &voice->envelope.decay, &voice->envelope.sustain,
                                  &voice->envelope.release};
    for (std::uint32_t i = 0; i < 4u; ++i) {
        if ((flags & (1u << i)) != 0u && modes[i] > static_cast<std::uint32_t>(Curve::Direct))
            return static_cast<std::int32_t>(kInvalidCurveMode);
    }
    for (std::uint32_t i = 0; i < 4u; ++i) {
        if ((flags & (1u << i)) != 0u) stages[i]->curve = static_cast<Curve>(modes[i]);
    }
    return 0;
}

std::int32_t SasCore::set_sustain_level(std::int32_t index, std::int32_t level) {
    Voice *voice = voice_at(index);
    if (voice == nullptr) return static_cast<std::int32_t>(kInvalidVoice);
    if (level < 0) return static_cast<std::int32_t>(kInvalidParameter);
    voice->envelope.sustain_level = level;
    return 0;
}

std::int32_t SasCore::envelope_height(std::int32_t index) const {
    if (index < 0 || static_cast<std::uint32_t>(index) >= kMaxVoices) return static_cast<std::int32_t>(kInvalidVoice);
    return static_cast<std::int32_t>(voices_[static_cast<std::size_t>(index)].envelope.height());
}

std::uint32_t SasCore::end_flags() const noexcept {
    std::uint32_t flags = 0u;
    for (std::uint32_t index = 0; index < kMaxVoices; ++index) {
        if (!voices_[index].on) flags |= 1u << index;
    }
    return flags;
}

std::uint32_t SasCore::pause_flags() const noexcept {
    std::uint32_t flags = 0u;
    for (std::uint32_t index = 0; index < kMaxVoices; ++index) {
        if (voices_[index].paused) flags |= 1u << index;
    }
    return flags;
}

std::int32_t SasCore::set_grain(std::uint32_t grain) {
    if (grain < 0x40u || grain > 0x800u || (grain & 0x1Fu) != 0u) return static_cast<std::int32_t>(kInvalidGrain);
    grain_ = grain;
    return 0;
}

std::int32_t SasCore::set_output_mode(std::uint32_t mode) {
    if (mode > 1u) return static_cast<std::int32_t>(kInvalidOutputMode);
    output_mode_ = mode;
    return 0;
}

std::int32_t SasCore::set_reverb_type(std::int32_t type) {
    if (type < -1 || type > 7) return static_cast<std::int32_t>(kInvalidReverbType);
    reverb_type_ = type;
    return 0;
}

std::int32_t SasCore::set_reverb_parameters(std::int32_t delay, std::int32_t feedback) {
    if (delay < 0 || delay > 127) return static_cast<std::int32_t>(kInvalidReverbDelay);
    if (feedback < 0 || feedback > 127) return static_cast<std::int32_t>(kInvalidReverbFeedback);
    reverb_delay_ = delay;
    reverb_feedback_ = feedback;
    return 0;
}

std::int32_t SasCore::set_reverb_volume(std::int32_t left, std::int32_t right) {
    if (std::abs(left) > kVolumeMax || std::abs(right) > kVolumeMax) return static_cast<std::int32_t>(kInvalidReverbVolume);
    reverb_left_ = left;
    reverb_right_ = right;
    return 0;
}

std::int32_t SasCore::set_reverb_voice_on(std::int32_t dry, std::int32_t wet) {
    if (dry < 0 || dry > 1 || wet < 0 || wet > 1) return static_cast<std::int32_t>(kInvalidParameter);
    dry_on_ = dry;
    wet_on_ = wet;
    return 0;
}

std::int16_t SasCore::next_source_sample(Voice &voice, const psprecomp::GuestMemory &memory) {
    if (voice.noise) {
        noise_state_ ^= noise_state_ << 13u;
        noise_state_ ^= noise_state_ >> 17u;
        noise_state_ ^= noise_state_ << 5u;
        return static_cast<std::int16_t>(noise_state_ >> 16u);
    }
    const auto sample = voice.vag.next(memory);
    if (!sample) {
        voice.on = false;
        return 0;
    }
    return *sample;
}

std::int32_t SasCore::render(psprecomp::GuestMemory &memory, std::uint32_t output, bool mix, std::int32_t mix_left,
                             std::int32_t mix_right) {
    if (!initialized()) return static_cast<std::int32_t>(kNotInitialized);
    if (output_mode_ != 0u) return static_cast<std::int32_t>(kInvalidOutputMode);
    if ((output & 1u) != 0u || !memory.contains(output, grain_ * 4u)) return static_cast<std::int32_t>(kBadAddress);
    std::vector<std::int64_t> left(grain_, 0), right(grain_, 0);
    for (std::uint32_t index = 0; index < max_voices_; ++index) {
        Voice &voice = voices_[index];
        if (!voice.on || voice.paused) continue;
        if (!voice.primed) {
            voice.current = next_source_sample(voice, memory);
            voice.following = next_source_sample(voice, memory);
            voice.primed = true;
        }
        for (std::uint32_t i = 0; i < grain_ && voice.on; ++i) {
            const std::int64_t sample = voice.current +
                ((static_cast<std::int64_t>(voice.following - voice.current) * voice.position) >> 12);
            voice.position += voice.pitch;
            while (voice.position >= kPitchUnity && voice.on) {
                voice.position -= kPitchUnity;
                voice.current = voice.following;
                voice.following = next_source_sample(voice, memory);
            }
            voice.envelope.step();
            const std::int64_t shaped = (sample * voice.envelope.height()) >> 30;
            left[i] += (shaped * voice.left) >> 12;
            right[i] += (shaped * voice.right) >> 12;
            if (voice.envelope.phase() == Envelope::Phase::Off) voice.on = false;
        }
    }
    for (std::uint32_t i = 0; i < grain_; ++i) {
        const std::uint32_t address = output + i * 4u;
        std::int64_t base_left = 0, base_right = 0;
        if (mix) {
            base_left = (static_cast<std::int64_t>(static_cast<std::int16_t>(memory.load16(address))) * mix_left) >> 12;
            base_right = (static_cast<std::int64_t>(static_cast<std::int16_t>(memory.load16(address + 2u))) * mix_right) >> 12;
        }
        memory.store16(address, static_cast<std::uint16_t>(clamp16(base_left + left[i])));
        memory.store16(address + 2u, static_cast<std::uint16_t>(clamp16(base_right + right[i])));
    }
    return 0;
}

void register_sas(SasCore &sas, psprecomp::Runtime &runtime) {
    using Handler = std::function<std::int32_t(psprecomp::Runtime &, psprecomp::AllegrexContext &)>;
    // Each call's first argument names the SAS core; one core is modeled.
    const auto bind = [&sas, &runtime](std::uint32_t nid, Handler handler, bool needs_init = true) {
        runtime.register_hle("sceSasCore", nid, [&sas, handler, needs_init](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            if (needs_init && !sas.initialized()) { ctx.set_gpr(2u, kNotInitialized); return; }
            ctx.set_gpr(2u, static_cast<std::uint32_t>(handler(rt, ctx)));
        });
    };
    const auto i32 = [](std::uint32_t value) { return static_cast<std::int32_t>(value); };
    bind(0x42778A9Fu, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasInit
        return sas.init(c.gpr[4], c.gpr[5], c.gpr[6], c.gpr[7], c.gpr[8]); }, false);
    bind(0xA3589D81u, [&sas](psprecomp::Runtime &rt, psprecomp::AllegrexContext &c) {  // __sceSasCore
        return sas.render(rt.memory(), c.gpr[5], false, 0, 0); });
    bind(0x50A14DFCu, [&sas, i32](psprecomp::Runtime &rt, psprecomp::AllegrexContext &c) {  // __sceSasCoreWithMix
        return sas.render(rt.memory(), c.gpr[5], true, i32(c.gpr[6]), i32(c.gpr[7])); });
    bind(0x68A46B95u, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &) {  // __sceSasGetEndFlag
        return static_cast<std::int32_t>(sas.end_flags()); });
    bind(0x2C8E6AB3u, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &) {  // __sceSasGetPauseFlag
        return static_cast<std::int32_t>(sas.pause_flags()); });
    bind(0x787D04D5u, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetPause
        return sas.set_pause(c.gpr[5], c.gpr[6] != 0u); });
    bind(0x99944089u, [&sas, i32](psprecomp::Runtime &rt, psprecomp::AllegrexContext &c) {  // __sceSasSetVoice
        return sas.set_voice(i32(c.gpr[5]), c.gpr[6], i32(c.gpr[7]), c.gpr[8], rt.memory()); });
    bind(0xB7660A23u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetNoise
        return sas.set_noise(i32(c.gpr[5]), c.gpr[6]); });
    bind(0xAD84D37Fu, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetPitch
        return sas.set_pitch(i32(c.gpr[5]), c.gpr[6]); });
    bind(0x440CA7D8u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetVolume
        return sas.set_volume(i32(c.gpr[5]), i32(c.gpr[6]), i32(c.gpr[7]), i32(c.gpr[8]), i32(c.gpr[9])); });
    bind(0x76F01ACAu, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetKeyOn
        return sas.key_on(i32(c.gpr[5])); });
    bind(0xA0CF2FA4u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetKeyOff
        return sas.key_off(i32(c.gpr[5])); });
    bind(0xCBCD4F79u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetSimpleADSR
        return sas.set_simple_adsr(i32(c.gpr[5]), c.gpr[6], c.gpr[7]); });
    bind(0x019B25EBu, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetADSR
        return sas.set_adsr(i32(c.gpr[5]), c.gpr[6], {i32(c.gpr[7]), i32(c.gpr[8]), i32(c.gpr[9]), i32(c.gpr[10])}); });
    bind(0x9EC3676Au, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetADSRmode
        return sas.set_adsr_mode(i32(c.gpr[5]), c.gpr[6], {c.gpr[7], c.gpr[8], c.gpr[9], c.gpr[10]}); });
    bind(0x5F9529F6u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetSL
        return sas.set_sustain_level(i32(c.gpr[5]), i32(c.gpr[6])); });
    bind(0x74AE582Au, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasGetEnvelopeHeight
        return sas.envelope_height(i32(c.gpr[5])); });
    bind(0xBD11B7C2u, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &) {  // __sceSasGetGrain
        return static_cast<std::int32_t>(sas.grain()); });
    bind(0xD1E0A01Eu, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetGrain
        return sas.set_grain(c.gpr[5]); });
    bind(0xE175EF66u, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &) {  // __sceSasGetOutputmode
        return static_cast<std::int32_t>(sas.output_mode()); });
    bind(0xE855BF76u, [&sas](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasSetOutputmode
        return sas.set_output_mode(c.gpr[5]); });
    bind(0x33D4AB37u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasRevType
        return sas.set_reverb_type(i32(c.gpr[5])); });
    bind(0x267A6DD2u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasRevParam
        return sas.set_reverb_parameters(i32(c.gpr[5]), i32(c.gpr[6])); });
    bind(0xD5A229C9u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasRevEVOL
        return sas.set_reverb_volume(i32(c.gpr[5]), i32(c.gpr[6])); });
    bind(0xF983B186u, [&sas, i32](psprecomp::Runtime &, psprecomp::AllegrexContext &c) {  // __sceSasRevVON
        return sas.set_reverb_voice_on(i32(c.gpr[5]), i32(c.gpr[6])); });
}

} // namespace sarusaru::sas
