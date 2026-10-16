#pragma once
#include "save_state/session_access.hpp"
#include <vector>

// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sarusaru::movie {

// A decoded picture as planar 4:2:0: a full-size luma plane, then the two
// half-size chroma planes, all tightly packed.
struct Picture {
    std::uint32_t width{};
    std::uint32_t height{};
    bool full_range{};
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> cb;
    std::vector<std::uint8_t> cr;
};

// H.264 pictures decoded with FFmpeg's libavcodec. Without FFmpeg the class
// still exists but open() fails.
class AvcDecoder {
    friend struct sarusaru::save_state::SessionCodec;
public:
    AvcDecoder();
    ~AvcDecoder();
    AvcDecoder(const AvcDecoder &) = delete;
    AvcDecoder &operator=(const AvcDecoder &) = delete;

    [[nodiscard]] static bool available() noexcept;

    bool open();
    void close();
    [[nodiscard]] bool is_open() const noexcept;

    // Decodes one access unit (Annex B). Returns true and fills `picture`
    // when a picture comes out; the decoder may hold pictures back.
    bool decode(std::span<const std::uint8_t> unit, Picture &picture);
    // Returns a held-back picture at the end of the stream, if any is left.
    bool drain(Picture &picture);
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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sarusaru::movie
