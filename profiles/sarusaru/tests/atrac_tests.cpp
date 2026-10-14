#include "audio/atrac.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace sarusaru::audio;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

// Each 8-byte frame's first byte names it; the stand-in decoder writes that
// tag on the left and the sample's index within the frame on the right.
class TagDecoder final : public FrameDecoder {
public:
    bool open(AtracCodec codec, unsigned, unsigned, std::span<const std::uint8_t>) override { return codec == AtracCodec::Atrac3Plus; }
    std::size_t decode(std::span<const std::uint8_t> frame, std::int16_t *out) override {
        for (std::size_t i = 0; i < 2048u; ++i) {
            out[i * 2u] = static_cast<std::int16_t>(frame[0]);
            out[i * 2u + 1u] = static_cast<std::int16_t>(i);
        }
        return 2048u;
    }
    void reset() override {}
};

constexpr std::uint32_t kFrames = 10u, kAlign = 8u, kDataOffset = 128u;

void put32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
void put16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

// An invented ATRAC3plus WAVE: 10 frames, 20000 samples, loop 4096..12287.
std::vector<std::uint8_t> make_track() {
    std::vector<std::uint8_t> file(kDataOffset + kFrames * kAlign, 0u);
    put32(file, 0, 0x46464952u); put32(file, 4, static_cast<std::uint32_t>(file.size() - 8u)); put32(file, 8, 0x45564157u);
    put32(file, 12, 0x20746D66u); put32(file, 16, 16u);                         // fmt
    put16(file, 20, 0xFFFEu); put16(file, 22, 2u); put16(file, 32, kAlign);
    put32(file, 36, 0x74636166u); put32(file, 40, 8u); put32(file, 44, 20000u); // fact
    put32(file, 52, 0x6C706D73u); put32(file, 56, 60u);                         // smpl, one loop
    put32(file, 60 + 28, 1u); put32(file, 60 + 36 + 8, 4096u); put32(file, 60 + 36 + 12, 12287u);
    put32(file, 120, 0x61746164u); put32(file, 124, kFrames * kAlign);         // data
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) file[kDataOffset + frame * kAlign] = static_cast<std::uint8_t>(10u + frame);
    return file;
}

AtracLibrary make_library() {
    return AtracLibrary([] { return std::make_unique<TagDecoder>(); });
}

constexpr std::uint32_t kBuffer = 0x08810000u, kOut = 0x08820000u;

std::int16_t left(const psprecomp::GuestMemory &memory, std::uint32_t sample) {
    return static_cast<std::int16_t>(memory.load16(kOut + sample * 4u));
}
std::int16_t right(const psprecomp::GuestMemory &memory, std::uint32_t sample) {
    return static_cast<std::int16_t>(memory.load16(kOut + sample * 4u + 2u));
}
}

int main() {
    const auto track = make_track();
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    memory.copy_in(kBuffer, track);

    // All data on memory: position 0 is decoded sample 368 (decoder delay).
    {
        auto library = make_library();
        require(static_cast<std::uint32_t>(library.get_id(0x1002u)) == atrac_error::kBadCodecType, "codec type check");
        const auto id = static_cast<std::uint32_t>(library.get_id(kCodecAtrac3Plus));
        require(library.set_data(id, memory, kBuffer, static_cast<std::uint32_t>(track.size())) == 0, "SetData");
        const TrackInfo *info = library.track(id);
        require(info->end_sample == 19999 && info->loop_start == 4096 && info->loop_end == 12287 && info->skip == 368,
                "header parsing");
        AtracLibrary::Decoded decoded;
        require(library.decode(id, memory, kOut, decoded) == 0 && decoded.samples == 1680 && decoded.remain == -1 &&
                    left(memory, 0) == 10 && right(memory, 0) == 368,
                "first decode skips the delay");
        std::int32_t total = decoded.samples;
        while (!decoded.end) {
            require(library.decode(id, memory, kOut, decoded) == 0, "decode");
            total += decoded.samples;
        }
        require(total == 20000, "all samples decoded once without looping");
        require(static_cast<std::uint32_t>(library.decode(id, memory, kOut, decoded)) == atrac_error::kAllDataDecoded,
                "decode past the end");
        require(library.release(id) == 0 && static_cast<std::uint32_t>(library.release(id)) == atrac_error::kNoData,
                "release");
    }

    // Streamed and looping: the buffer holds the header and three frames.
    memory.copy_in(kBuffer, track);
    {
        auto library = make_library();
        const auto id = static_cast<std::uint32_t>(library.get_id(kCodecAtrac3Plus));
        const std::uint32_t buffer_size = kDataOffset + 3u * kAlign;
        require(library.set_data(id, memory, kBuffer, buffer_size) == 0 && library.set_loop_num(id, -1) == 0, "stream setup");
        std::int32_t remain = 0;
        require(library.remain_frames(id, remain) == 0 && remain == 3, "three frames buffered");
        // The supply runs to the loop-end frame (6), then from the loop-start frame (2).
        const auto supply = [&](std::uint32_t expected_offset, std::uint32_t expected_bytes) {
            AtracLibrary::StreamInfo info;
            require(library.stream_info(id, info) == 0 && info.write_address == kBuffer &&
                        info.read_offset == expected_offset && info.writable == expected_bytes,
                    "stream info");
            memory.copy_in(kBuffer, std::span(track).subspan(info.read_offset, info.writable));
            require(library.add_stream_data(id, memory, info.writable) == 0, "AddStreamData");
        };
        supply(kDataOffset + 3u * kAlign, 4u * kAlign);
        supply(kDataOffset + 2u * kAlign, 5u * kAlign);
        AtracLibrary::Decoded decoded;
        std::int32_t position = 0;
        while (position <= 12287) {
            require(library.decode(id, memory, kOut, decoded) == 0, "decode before the loop end");
            position += decoded.samples;
        }
        require(position == 12288, "playback reached the loop end exactly");
        require(library.decode(id, memory, kOut, decoded) == 0 && left(memory, 0) == 12 && right(memory, 0) == 368,
                "after the loop, playback continues in the loop-start frame");
        AtracLibrary::StreamInfo info;
        require(library.add_stream_data(id, memory, 1000u) == static_cast<std::int32_t>(atrac_error::kParamFail) &&
                    library.stream_info(id, info) == 0,
                "oversized add refused");
    }

    // Running out of streamed data is reported, not invented.
    memory.copy_in(kBuffer, track);
    {
        auto library = make_library();
        const auto id = static_cast<std::uint32_t>(library.get_id(kCodecAtrac3Plus));
        require(library.set_data(id, memory, kBuffer, kDataOffset + kAlign) == 0, "one-frame stream");
        AtracLibrary::Decoded decoded;
        require(library.decode(id, memory, kOut, decoded) == 0 && decoded.samples == 1680, "first frame");
        require(library.decode(id, memory, kOut, decoded) == static_cast<std::int32_t>(atrac_error::kBufferIsEmpty),
                "underflow reported");
    }

    // The real decoder (FFmpeg) opens ATRAC3plus when the build links it.
    if (AtracDecoder::available()) {
        auto decoder = make_ffmpeg_decoder();
        require(decoder->open(AtracCodec::Atrac3Plus, 2u, 0x230u, {}), "FFmpeg opens ATRAC3plus");
    }
    return 0;
}
