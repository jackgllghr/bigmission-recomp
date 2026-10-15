#include "movie/psmf_player.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sarusaru::movie;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void be32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
}

// An invented PSMF header: one video and one audio stream, 2 s long, no packs.
std::vector<std::uint8_t> make_psmf() {
    std::vector<std::uint8_t> file(0x800u, 0u);
    file[0] = 'P'; file[1] = 'S'; file[2] = 'M'; file[3] = 'F';
    be32(file, 8, 0x800u);
    be32(file, 12, 0u);
    be32(file, 0x54 + 2, 90000u);
    be32(file, 0x5A + 2, 90000u + 180000u);
    file[0x81] = 2u;
    file[0x82] = 0xE0u;
    file[0x82 + 16] = 0xBDu;
    return file;
}

// One MPEG-PS pack holding a video PES (with a PTS) and an audio PES.
std::vector<std::uint8_t> make_pack() {
    std::vector<std::uint8_t> pack(kPackSize, 0xFFu);
    const std::uint8_t header[] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 0, 0, 3, 0xF8};
    std::copy(std::begin(header), std::end(header), pack.begin());
    std::size_t at = 14u;
    // Video: two access-unit delimiters, so the first picture is complete.
    const std::uint8_t video[] = {0, 0, 1, 0xE0, 0, 22, 0x81, 0x80, 5, 0x21, 0x00, 0x05, 0xBF, 0x21,
                                  0, 0, 0, 1, 9, 0xF0, 0xAA, 0, 0, 0, 1, 9, 0xF0, 0xBB};
    std::copy(std::begin(video), std::end(video), pack.begin() + static_cast<std::ptrdiff_t>(at));
    at += sizeof video;
    // Audio: private stream 1, sub-stream header, one 16-byte ATRAC3plus frame (8 + 8).
    const std::uint8_t audio[] = {0, 0, 1, 0xBD, 0, 23, 0x81, 0x00, 0, 0, 0, 0, 0,
                                  0x0F, 0xD0, 0x08, 0x00, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::copy(std::begin(audio), std::end(audio), pack.begin() + static_cast<std::ptrdiff_t>(at));
    return pack;
}
}

int main() {
    const auto file = make_psmf();
    const auto info = parse_psmf(file);
    require(info && info->stream_offset == 0x800u && info->start_pts == 90000u && info->end_pts == 270000u &&
                info->video_streams == 1u && info->audio_streams == 1u,
            "PSMF header");
    require(!parse_psmf(std::vector<std::uint8_t>(0x100u, 0u)), "non-PSMF rejected");

    // Demuxer: pictures split at delimiters with the PES timestamp; ATRAC frames split at their headers.
    PsmfDemuxer demuxer;
    require(demuxer.push_pack(make_pack()), "pack accepted");
    const auto picture = demuxer.pop_video();
    require(picture && picture->pts == 90000 && picture->data.size() == 7u && picture->data[5] == 0xF0u, "first picture");
    require(!demuxer.video_ready(), "second picture waits for the next delimiter");
    demuxer.end_of_stream();
    require(demuxer.pop_video().has_value(), "last picture completes at end of stream");
    const auto frame = demuxer.pop_audio();
    require(frame && frame->data.size() == 8u && frame->data[0] == 1u && demuxer.audio_frame_size() == 16u &&
                demuxer.audio_channels() == 2u,
            "audio frame");

    // Pixel conversion: limited-range grey, white and pure red.
    Picture grey{2u, 2u, false, {16, 235, 81, 81}, {128}, {128}};
    std::uint8_t rgba[16]{};
    convert_picture(grey, PixelType::Rgba8888, 2u, rgba);
    require(rgba[0] == 0 && rgba[3] == 0xFF && rgba[4] == 255 && rgba[5] == 255 && rgba[6] == 255, "luma range");
    Picture red{1u, 1u, false, {81}, {90}, {240}};
    std::uint8_t rgb565[2]{};
    convert_picture(red, PixelType::Rgb565, 1u, rgb565);
    const std::uint16_t packed = static_cast<std::uint16_t>(rgb565[0] | (rgb565[1] << 8u));
    require((packed & 0x1Fu) == 0x1Fu && (packed >> 11u) == 0u, "red in RGB565");

    // Player status machine on the virtual clock.
    psprecomp::Runtime runtime;
    sarusaru::kernel::EarlyKernel kernel;
    kernel.install(runtime, 0x08804000u, 0x0A000000u);
    auto &cpu = runtime.cpu();
    cpu.pc = 0x08804000u;
    kernel.begin_main_thread(cpu, "main", 0x20u);
    sarusaru::kernel::register_early_threadman(kernel, runtime);
    PsmfPlayer player(kernel, [&file](const std::string &path) -> std::optional<std::vector<std::uint8_t>> {
        if (path == "disc0:/movie.pmf") return file;
        return std::nullopt;
    });
    constexpr std::uint32_t handle = 0x08807000u;
    require(player.create(handle) == 0 && player.status(handle) == static_cast<std::int32_t>(psmf_status::kInit), "create");
    require(static_cast<std::uint32_t>(player.set_psmf(handle, "disc0:/missing.pmf")) == psmf_error::kInvalidParam, "missing movie");
    require(player.set_psmf(handle, "disc0:/movie.pmf") == 0 && player.status(handle) == static_cast<std::int32_t>(psmf_status::kStandby),
            "SetPsmf");
    require(player.configure(handle, 1u, 0) == 0 && static_cast<std::uint32_t>(player.configure(handle, 9u, 0)) == psmf_error::kInvalidConfig,
            "configuration");
    require(player.start(runtime, handle, 0u) == 0 && player.update(handle) == 0 &&
                player.status(handle) == static_cast<std::int32_t>(psmf_status::kPlaying),
            "start");
    std::uint32_t pts = 0u;
    require(static_cast<std::uint32_t>(player.video(runtime.memory(), handle, 512u, 0x04000000u, pts)) == psmf_error::kNoMoreData,
            "no pictures in an empty stream");
    require(!player.playing(), "no movie position before a picture is shown");
    cpu.set_gpr(4u, 2'000'000u);
    cpu.pc = 0x08A00000u; cpu.set_gpr(31u, 0x08804100u);
    runtime.invoke_import("ThreadManForUser", 0xCEADEB47u, cpu);
    require(player.update(handle) == 0 && player.status(handle) == static_cast<std::int32_t>(psmf_status::kFinished),
            "finished after the stream's length");
    require(player.stop(handle) == 0 && player.release_psmf(handle) == 0 && player.remove(handle) == 0 &&
                static_cast<std::uint32_t>(player.status(handle)) == psmf_error::kInvalidStatus,
            "stop, release, delete");
    return 0;
}
