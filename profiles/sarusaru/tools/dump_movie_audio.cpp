// Writes the soundtrack of every movie on the player's disc as a WAV file,
// for transcribing the cutscenes into subtitles (docs/movie-subtitles.md).
// The movies' ATRAC3plus audio goes through the same demuxer and decoder as
// in the game, so the WAV lines up with what the player hears.
//
//   sarusaru_dump_movie_audio <disc.iso> <output directory>
//
// Each movie becomes <stem>.wav (44.1 kHz stereo 16-bit), its stem being the
// one its subtitle file is named after. A line per movie gives its length
// and how much of it is above a quiet level, to tell silent ones apart.
#include "movie/psmf_demuxer.hpp"
#include "movie/psmf_player.hpp"
#include "movie/subtitles.hpp"
#include "audio/atrac_decoder.hpp"
#include "umd/disc_image.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {
using namespace sarusaru;

constexpr std::uint32_t kSampleRate = 44'100u;
constexpr std::uint32_t kMaxMovieBytes = 512u * 1024u * 1024u;

void le(std::ofstream &out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
}

void write_wav(const std::filesystem::path &path, const std::vector<std::int16_t> &samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    const auto data = static_cast<std::uint32_t>(samples.size() * 2u);
    out.write("RIFF", 4); le(out, 36u + data, 4); out.write("WAVE", 4);
    out.write("fmt ", 4); le(out, 16u, 4); le(out, 1u, 2); le(out, 2u, 2);
    le(out, kSampleRate, 4); le(out, kSampleRate * 4u, 4); le(out, 4u, 2); le(out, 16u, 2);
    out.write("data", 4); le(out, data, 4);
    for (const auto sample : samples) le(out, static_cast<std::uint16_t>(sample), 2);
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// The movie's audio, decoded from start to end.
std::vector<std::int16_t> decode_audio(const std::vector<std::uint8_t> &file, const movie::PsmfInfo &info) {
    movie::PsmfDemuxer demuxer;
    audio::AtracDecoder decoder;
    bool failed = false;
    std::vector<std::int16_t> samples;
    std::vector<std::int16_t> frame(movie::kAtracFrameSamples * 2u);
    const auto drain = [&] {
        while (auto unit = demuxer.pop_audio()) {
            if (!decoder.is_open() && !failed) {
                failed = !decoder.open(audio::AtracCodec::Atrac3Plus, std::max(1u, demuxer.audio_channels()),
                                       static_cast<unsigned>(unit->data.size()), {});
            }
            if (failed) continue;
            const auto count = decoder.decode(unit->data, frame.data());
            samples.insert(samples.end(), frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(count * 2u));
        }
    };
    const std::size_t end = std::min<std::size_t>(file.size(), static_cast<std::size_t>(info.stream_offset) + info.stream_size);
    for (std::size_t offset = info.stream_offset; offset + movie::kPackSize <= end; offset += movie::kPackSize) {
        (void)demuxer.push_pack(std::span(file).subspan(offset, movie::kPackSize));
        drain();
    }
    demuxer.end_of_stream();
    drain();
    if (failed) throw std::runtime_error("the ATRAC3plus decoder would not open");
    return samples;
}

// The share of 100 ms windows louder than -40 dBFS: near 0 for a silent movie.
double active_share(const std::vector<std::int16_t> &samples) {
    constexpr std::size_t kWindow = kSampleRate / 10u * 2u;
    const double threshold = 32768.0 * std::pow(10.0, -40.0 / 20.0);
    std::size_t windows = 0u, active = 0u;
    for (std::size_t at = 0; at + kWindow <= samples.size(); at += kWindow, ++windows) {
        double sum = 0.0;
        for (std::size_t i = at; i < at + kWindow; ++i) sum += static_cast<double>(samples[i]) * samples[i];
        if (std::sqrt(sum / kWindow) > threshold) ++active;
    }
    return windows == 0u ? 0.0 : static_cast<double>(active) / static_cast<double>(windows);
}

void find_movies(umd::DiscImage &disc, const umd::DiscImage::Entry &directory, const std::string &path,
                 std::vector<std::pair<std::string, umd::DiscImage::Entry>> &movies) {
    for (const auto &entry : disc.list(directory)) {
        const std::string child = path.empty() ? entry.name : path + "/" + entry.name;
        if (entry.directory) {
            find_movies(disc, entry, child, movies);
            continue;
        }
        std::string extension = std::filesystem::path(entry.name).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".pmf") movies.emplace_back(child, entry);
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3 || std::string_view(argv[1]) == "--help") {
        std::cerr << "usage: sarusaru_dump_movie_audio <disc.iso> <output directory>\n";
        return argc == 2 ? 0 : 2;
    }
    try {
        if (!audio::AtracDecoder::available()) throw std::runtime_error("this build has no FFmpeg to decode ATRAC3plus");
        umd::DiscImage disc{std::filesystem::path(argv[1])};
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        std::vector<std::pair<std::string, umd::DiscImage::Entry>> movies;
        find_movies(disc, disc.root(), "", movies);
        std::sort(movies.begin(), movies.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::map<std::string, std::string> stems;
        int status = 0;
        std::printf("%-40s %-24s %9s %7s\n", "movie", "stem", "seconds", "sound");
        for (const auto &[path, entry] : movies) {
            const auto stem = movie::subtitle_key(path);
            if (const auto [other, fresh] = stems.emplace(stem, path); !fresh) {
                std::cerr << path << ": same stem as " << other->second << "; subtitles cannot tell them apart\n";
                status = 1;
            }
            const auto file = disc.read_file(entry, kMaxMovieBytes);
            const auto info = movie::parse_psmf(file);
            if (!info || info->audio_streams == 0u) {
                std::printf("%-40s %-24s %9s %7s\n", path.c_str(), stem.c_str(), "-", info ? "none" : "bad");
                continue;
            }
            try {
                const auto samples = decode_audio(file, *info);
                write_wav(output / (stem + ".wav"), samples);
                std::printf("%-40s %-24s %9.2f %6.0f%%\n", path.c_str(), stem.c_str(),
                            static_cast<double>(samples.size() / 2u) / kSampleRate, active_share(samples) * 100.0);
            } catch (const std::exception &error) {
                std::cerr << path << ": " << error.what() << '\n';
                status = 1;
            }
        }
        return status;
    } catch (const std::exception &error) {
        std::cerr << "sarusaru_dump_movie_audio: " << error.what() << '\n';
        return 1;
    }
}
