#pragma once

// scePsmfPlayer: Sony's movie player library (libpsmfplayer.prx), which this
// profile provides by HLE. The player reads a PSMF movie from the disc,
// demuxes it (movie/psmf_demuxer), decodes H.264 video (movie/avc_decoder)
// and ATRAC3plus audio (audio/atrac_decoder) with FFmpeg, and hands the game
// pictures in its configured pixel format and 2048-sample stereo blocks.
//
// Timing: playback time is the kernel's virtual time since Start, so the
// picture GetVideoData returns is the latest whose presentation time has been
// reached (pictures are assumed to come out in presentation order at the
// stream's 29.97 Hz). Each GetAudioData returns the next audio frame. Only
// start position 0 is modeled; other start points stop execution.
#include "kernel/early_kernel.hpp"
#include "movie/avc_decoder.hpp"
#include "movie/psmf_demuxer.hpp"
#include "audio/atrac_decoder.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sarusaru::movie {

namespace psmf_error {
inline constexpr std::uint32_t kInvalidStatus = 0x80616001u;
inline constexpr std::uint32_t kInvalidStream = 0x80616003u;
inline constexpr std::uint32_t kInvalidConfig = 0x80616006u;
inline constexpr std::uint32_t kInvalidParam = 0x80616008u;
inline constexpr std::uint32_t kNoMoreData = 0x8061600Cu;
} // namespace psmf_error

namespace psmf_status {
inline constexpr std::uint32_t kNone = 0x0u;
inline constexpr std::uint32_t kInit = 0x1u;
inline constexpr std::uint32_t kStandby = 0x2u;
inline constexpr std::uint32_t kPlaying = 0x4u;
inline constexpr std::uint32_t kFinished = 0x200u;
} // namespace psmf_status

enum class PixelType : std::uint32_t { Rgb565 = 0, Rgba5551 = 1, Rgba4444 = 2, Rgba8888 = 3 };

// The header fields the player needs from a PSMF file.
struct PsmfInfo {
    std::uint32_t stream_offset{}, stream_size{};
    std::uint32_t start_pts{}, end_pts{};
    std::uint32_t video_streams{}, audio_streams{};
};
[[nodiscard]] std::optional<PsmfInfo> parse_psmf(const std::vector<std::uint8_t> &file);

// Converts one 4:2:0 picture to `type`, writing `height` rows of `width`
// pixels with a stride of `stride` pixels.
void convert_picture(const Picture &picture, PixelType type, std::uint32_t stride, std::uint8_t *out);

class PsmfPlayer {
public:
    using FileReader = std::function<std::optional<std::vector<std::uint8_t>>(const std::string &)>;

    PsmfPlayer(kernel::EarlyKernel &kernel, FileReader reader) : kernel_(&kernel), reader_(std::move(reader)) {}

    [[nodiscard]] std::int32_t create(std::uint32_t handle);
    [[nodiscard]] std::int32_t remove(std::uint32_t handle);
    [[nodiscard]] std::int32_t set_psmf(std::uint32_t handle, const std::string &path);
    [[nodiscard]] std::int32_t release_psmf(std::uint32_t handle);
    [[nodiscard]] std::int32_t info(std::uint32_t handle, PsmfInfo &info) const;
    [[nodiscard]] std::int32_t configure(std::uint32_t handle, std::uint32_t mode, std::int32_t value);
    [[nodiscard]] std::int32_t start(psprecomp::Runtime &runtime, std::uint32_t handle, std::uint32_t initial_pts);
    [[nodiscard]] std::int32_t stop(std::uint32_t handle);
    [[nodiscard]] std::int32_t update(std::uint32_t handle);
    [[nodiscard]] std::int32_t status(std::uint32_t handle) const;
    // Writes the current picture into `buffer` (stride `frame_width` pixels).
    [[nodiscard]] std::int32_t video(psprecomp::GuestMemory &memory, std::uint32_t handle, std::uint32_t frame_width,
                                     std::uint32_t buffer, std::uint32_t &pts);
    // Writes 2048 stereo 16-bit samples.
    [[nodiscard]] std::int32_t audio(psprecomp::GuestMemory &memory, std::uint32_t handle, std::uint32_t buffer);

private:
    struct Player {
        std::uint32_t status{psmf_status::kInit};
        PixelType pixel_type{PixelType::Rgba8888};
        bool loop{};
        std::vector<std::uint8_t> file;
        PsmfInfo info;
        PsmfDemuxer demuxer;
        std::size_t next_pack{};
        std::unique_ptr<AvcDecoder> video;
        std::unique_ptr<audio::AtracDecoder> audio;
        std::optional<Picture> picture;
        std::uint32_t picture_pts{};
        std::uint64_t pictures_out{};
        bool video_ended{};
        std::uint64_t start_us{};
    };

    kernel::EarlyKernel *kernel_;
    FileReader reader_;
    std::map<std::uint32_t, Player> players_;

    [[nodiscard]] Player *find(std::uint32_t handle) noexcept;
    [[nodiscard]] const Player *find(std::uint32_t handle) const noexcept;
    void restart(Player &player);
    // Feeds packs until the demuxer has a unit of the wanted kind or the file ends.
    void fill(Player &player, bool want_video);
    // Decodes the next picture; false at the end of the video.
    bool next_picture(Player &player);
};

void register_psmf_player(PsmfPlayer &player, psprecomp::Runtime &runtime);

} // namespace sarusaru::movie
