#pragma once

// Subtitles for the game's movies, from SubRip (.srt) files: one per movie,
// named after the movie's file on the disc and the language, such as
// opening.en.srt for PSP_GAME/USRDIR/movie/OPENING.PMF. The shell draws the
// line due at the playing movie's position (movie/psmf_player.hpp) over the
// picture. See docs/movie-subtitles.md for making them.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sarusaru::movie {

struct Cue {
    std::uint32_t start_ms{}, end_ms{};
    // UTF-8, lines separated by '\n'; SRT's <i>-style tags are removed.
    std::string text;
};

// Parses SubRip text (UTF-8, with or without a BOM, any line endings). Cues
// come back in start order. Throws std::runtime_error naming the line for a
// malformed timing line, an end before its start or a cue without text.
[[nodiscard]] std::vector<Cue> parse_srt(std::string_view text);

// The name a movie's subtitles go by: the file name's stem, lowercased.
// "disc0:/PSP_GAME/USRDIR/movie/OPENING.PMF" -> "opening".
[[nodiscard]] std::string subtitle_key(std::string_view movie_path);

class SubtitleLibrary {
public:
    // Every <key>.<language>.srt in `directory`. A file that does not parse
    // is reported on stderr and left out; a missing directory gives none.
    void load_directory(const std::filesystem::path &directory, std::string_view language);
    void add(std::string key, std::vector<Cue> cues) { movies_[std::move(key)] = std::move(cues); }

    // The cue showing `position_ms` into the movie at `movie_path`, or null.
    [[nodiscard]] const Cue *at(std::string_view movie_path, std::uint64_t position_ms) const;
    [[nodiscard]] std::size_t size() const noexcept { return movies_.size(); }

private:
    std::map<std::string, std::vector<Cue>, std::less<>> movies_;
};

} // namespace sarusaru::movie
