#include "movie/subtitles.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace sarusaru::movie;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

bool rejects(std::string_view text) {
    try {
        (void)parse_srt(text);
        return false;
    } catch (const std::runtime_error &) {
        return true;
    }
}
}

int main() {
    // A BOM, CRLF line endings, tags, a two-line cue, cues out of order and no final blank line.
    const auto cues = parse_srt("\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Hello</i>, Spike!\r\n\r\n"
                                "3\n00:01:02.250 --> 00:01:04,000 align:start\nSecond\nline\n\n"
                                "2\n00:00:03,000 --> 00:00:04,000\nMiddle");
    require(cues.size() == 3u, "three cues");
    require(cues[0].start_ms == 1000u && cues[0].end_ms == 2500u && cues[0].text == "Hello, Spike!", "first cue");
    require(cues[1].text == "Middle", "cues sorted by start");
    require(cues[2].start_ms == 62250u && cues[2].end_ms == 64000u && cues[2].text == "Second\nline", "multi-line cue");
    require(parse_srt("").empty() && parse_srt("\n\n").empty(), "empty file");
    require(parse_srt("00:00:00,000 --> 00:00:00,500\n3 < 4\n")[0].text == "3 < 4", "a lone < is text");

    require(rejects("1\n00:00:01 --> 00:00:02,000\nNo milliseconds\n"), "missing milliseconds");
    require(rejects("1\n00:00:02,000 --> 00:00:01,000\nBackwards\n"), "end before start");
    require(rejects("1\n00:00:01,000 --> 00:00:02,000\n\n"), "no text");
    require(rejects("1\nHello\n"), "no timing line");
    require(rejects("1\n00:61:00,000 --> 00:62:00,000\nMinutes\n"), "minutes out of range");

    require(subtitle_key("disc0:/PSP_GAME/USRDIR/movie/OPENING.PMF") == "opening", "key from a guest path");
    require(subtitle_key("umd0:Ending.pmf") == "ending" && subtitle_key("intro") == "intro", "key without directories");

    SubtitleLibrary library;
    library.add("opening", parse_srt("00:00:01,000 --> 00:00:05,000\nLong\n\n00:00:02,000 --> 00:00:03,000\nShort\n"));
    require(library.at("disc0:/x/OPENING.PMF", 500u) == nullptr, "nothing before the first cue");
    require(library.at("disc0:/x/OPENING.PMF", 1000u)->text == "Long", "a cue from its start");
    require(library.at("disc0:/x/OPENING.PMF", 2500u)->text == "Short", "overlap shows the later cue");
    require(library.at("disc0:/x/OPENING.PMF", 3000u)->text == "Long", "the longer cue returns");
    require(library.at("disc0:/x/OPENING.PMF", 5000u) == nullptr, "nothing at the end time");
    require(library.at("disc0:/x/OTHER.PMF", 2000u) == nullptr, "a movie without subtitles");

    // Files named <movie>.<language>.srt; a broken one is left out.
    const auto directory = std::filesystem::temp_directory_path() / "sarusaru_subtitle_tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "Opening.en.srt") << "1\n00:00:00,000 --> 00:00:01,000\nHi\n";
    std::ofstream(directory / "ending.ja.srt") << "1\n00:00:00,000 --> 00:00:01,000\nやあ\n";
    std::ofstream(directory / "broken.en.srt") << "nonsense\n";
    SubtitleLibrary english;
    english.load_directory(directory, "en");
    require(english.size() == 1u && english.at("OPENING.PMF", 0u) != nullptr && english.at("ending.pmf", 0u) == nullptr,
            "only this language's parseable files");
    SubtitleLibrary missing;
    missing.load_directory(directory / "absent", "en");
    require(missing.size() == 0u, "a missing directory gives none");
    std::filesystem::remove_all(directory);
    return 0;
}
