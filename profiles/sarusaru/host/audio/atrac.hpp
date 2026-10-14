#pragma once

// sceAtrac3plus: ATRAC3/ATRAC3plus WAVE tracks decoded one frame per call into
// 16-bit stereo PCM, which the game hands to sceAudio itself.
//
// A track is either wholly in its guest buffer ("all data on memory") or
// streamed. When streamed, the guest buffer is a staging area: the game asks
// where to write (always the buffer's start here; the PSP uses varying
// positions inside it) and which file offset to read, writes the bytes and
// reports them with AddStreamData, which moves them into a host-side frame
// queue. Supply wraps from the loop-end frame to the loop-start frame while
// the track loops. After a loop jump the decoder restarts without warm-up.
//
// Sample positions follow the library's convention: position 0 is the first
// sample the encoder was given; the decoded stream starts earlier by the
// encoder delay in the `fact` chunk plus the decoder's own delay.
#include "audio/atrac_decoder.hpp"
#include "psprecomp/runtime.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace sarusaru::audio {

namespace atrac_error {
inline constexpr std::uint32_t kParamFail = 0x80630001u;
inline constexpr std::uint32_t kNoAtracId = 0x80630003u;
inline constexpr std::uint32_t kBadCodecType = 0x80630004u;
inline constexpr std::uint32_t kBadAtracId = 0x80630005u;
inline constexpr std::uint32_t kUnknownFormat = 0x80630006u;
inline constexpr std::uint32_t kBadCodecParam = 0x80630008u;
inline constexpr std::uint32_t kNoData = 0x80630010u;
inline constexpr std::uint32_t kSizeTooSmall = 0x80630011u;
inline constexpr std::uint32_t kNoLoopInformation = 0x80630021u;
inline constexpr std::uint32_t kBufferIsEmpty = 0x80630023u;
inline constexpr std::uint32_t kAllDataDecoded = 0x80630024u;
} // namespace atrac_error

inline constexpr std::uint32_t kCodecAtrac3Plus = 0x1000u;
inline constexpr std::uint32_t kCodecAtrac3 = 0x1001u;
// GetRemainFrame results that are not frame counts.
inline constexpr std::int32_t kRemainAllDataOnMemory = -1;
inline constexpr std::int32_t kRemainStreamDataOnMemory = -2;

// Decodes single ATRAC frames; FFmpeg in the game, a stand-in in tests.
class FrameDecoder {
public:
    virtual ~FrameDecoder() = default;
    virtual bool open(AtracCodec codec, unsigned channels, unsigned block_align,
                      std::span<const std::uint8_t> extradata) = 0;
    // Writes atrac_frame_samples(codec) stereo samples; returns samples per channel, 0 on error.
    virtual std::size_t decode(std::span<const std::uint8_t> frame, std::int16_t *out) = 0;
    virtual void reset() = 0;
};
using DecoderFactory = std::function<std::unique_ptr<FrameDecoder>()>;
[[nodiscard]] std::unique_ptr<FrameDecoder> make_ffmpeg_decoder();

// What SetData learns from the WAVE header.
struct TrackInfo {
    AtracCodec codec{AtracCodec::Atrac3};
    std::uint32_t channels{};
    std::uint32_t block_align{};
    std::vector<std::uint8_t> extradata;
    std::uint32_t file_size{};
    std::uint32_t data_offset{};
    std::uint32_t data_size{};
    std::int32_t end_sample{};   // last playable position
    std::int32_t loop_start{-1}; // positions, -1 without a loop
    std::int32_t loop_end{-1};
    std::uint32_t skip{}; // decoded samples before position 0
};

class AtracLibrary {
public:
    static constexpr std::size_t kMaxIds = 6u;

    explicit AtracLibrary(DecoderFactory factory) : factory_(std::move(factory)) {}

    [[nodiscard]] std::int32_t get_id(std::uint32_t codec_type);
    [[nodiscard]] std::int32_t release(std::uint32_t id);
    void reinit();
    [[nodiscard]] std::int32_t set_data(std::uint32_t id, const psprecomp::GuestMemory &memory, std::uint32_t buffer,
                                        std::uint32_t buffer_size);
    [[nodiscard]] std::int32_t set_loop_num(std::uint32_t id, std::int32_t loops);

    struct StreamInfo { std::uint32_t write_address{}, writable{}, read_offset{}; };
    [[nodiscard]] std::int32_t stream_info(std::uint32_t id, StreamInfo &info) const;
    [[nodiscard]] std::int32_t add_stream_data(std::uint32_t id, const psprecomp::GuestMemory &memory, std::uint32_t bytes);

    struct Decoded { std::int32_t samples{}; bool end{}; std::int32_t remain{}; };
    [[nodiscard]] std::int32_t decode(std::uint32_t id, psprecomp::GuestMemory &memory, std::uint32_t output,
                                      Decoded &decoded);
    [[nodiscard]] std::int32_t remain_frames(std::uint32_t id, std::int32_t &remain) const;
    [[nodiscard]] std::int32_t next_sample(std::uint32_t id, std::int32_t &samples) const;
    [[nodiscard]] const TrackInfo *track(std::uint32_t id) const noexcept;

private:
    struct Context {
        AtracCodec requested{};
        bool has_data{};
        TrackInfo track;
        std::uint32_t buffer{}, buffer_size{};
        bool all_on_memory{};
        std::int32_t loop_num{};
        std::int32_t position{};
        std::unique_ptr<FrameDecoder> decoder;
        std::vector<std::int16_t> pcm;
        std::int64_t pcm_frame{-1};
        std::int64_t next_decoder_frame{};
        // Streaming: frames supplied but not yet decoded, a partial frame, and
        // the frame the game should supply next.
        std::deque<std::pair<std::int64_t, std::vector<std::uint8_t>>> queue;
        std::vector<std::uint8_t> pending;
        std::int64_t next_supply_frame{};
        bool supply_finished{};
    };

    DecoderFactory factory_;
    std::array<std::unique_ptr<Context>, kMaxIds> contexts_{};

    [[nodiscard]] Context *context(std::uint32_t id) const noexcept;
    [[nodiscard]] std::uint32_t missing(std::uint32_t id) const noexcept;
    static bool looping(const Context &context) noexcept;
    static std::int64_t frame_of(const Context &context, std::int64_t position) noexcept;
    static std::int64_t supply_end_frame(const Context &context) noexcept;
    static std::uint32_t writable_bytes(const Context &context) noexcept;
    static std::int32_t remain(const Context &context) noexcept;
    static std::int32_t samples_at(const Context &context) noexcept;
    void feed(Context &context, std::span<const std::uint8_t> bytes);
    bool load_frame(const psprecomp::GuestMemory &memory, Context &context, std::int64_t frame);
};

void register_atrac(AtracLibrary &library, psprecomp::Runtime &runtime);

} // namespace sarusaru::audio
