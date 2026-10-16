#pragma once
#include "save_state/session_access.hpp"
#include <vector>

// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sarusaru::audio {

enum class AtracCodec {
    Atrac3,     // baseline ATRAC3: 1024 samples per channel per frame
    Atrac3Plus, // ATRAC3plus: 2048 samples per channel per frame
};

[[nodiscard]] constexpr std::size_t atrac_frame_samples(AtracCodec codec) noexcept {
    return codec == AtracCodec::Atrac3 ? 1024u : 2048u;
}

// One ATRAC stream, decoded frame by frame with FFmpeg's libavcodec. Without
// FFmpeg the class still exists but open() fails, so callers fall back to
// silence.
class AtracDecoder {
    friend struct sarusaru::save_state::SessionCodec;
public:
    AtracDecoder();
    ~AtracDecoder();
    AtracDecoder(const AtracDecoder &) = delete;
    AtracDecoder &operator=(const AtracDecoder &) = delete;

    // True when the build links a decoder.
    [[nodiscard]] static bool available() noexcept;

    // For ATRAC3, `extradata` is the 14 codec-specific bytes that follow
    // cbSize in the WAVE `fmt ` chunk (coding mode and frame factor);
    // ATRAC3plus needs none.
    bool open(AtracCodec codec, unsigned channels, unsigned block_align, std::span<const std::uint8_t> extradata);
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] AtracCodec codec() const noexcept;

    // Decodes one frame of `block_align` bytes into interleaved 16-bit stereo;
    // `out` must hold atrac_frame_samples(codec) * 2 samples. Mono streams come
    // out on both sides. Returns the samples per channel written, 0 on error.
    std::size_t decode(std::span<const std::uint8_t> frame, std::int16_t *out);

    // Drops the overlap state, for decoding from another position.
    void reset();

private:
    std::vector<std::vector<std::uint8_t>> history_;
    std::size_t history_bytes_{};
    bool history_complete_{true};
    void clear_history() { history_.clear(); history_bytes_=0; history_complete_=true; }
    void remember(std::span<const std::uint8_t> bytes) {
        if (!history_complete_) return;
        if (bytes.size() > 32u*1024u*1024u-history_bytes_ || history_.size() >= 65536) {
            history_.clear(); history_bytes_=0; history_complete_=false; return;
        }
        history_bytes_+=bytes.size(); history_.emplace_back(bytes.begin(),bytes.end());
    }
    AtracCodec saved_codec_{};
    unsigned saved_channels_{}, saved_align_{};
    std::vector<std::uint8_t> saved_extra_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sarusaru::audio
