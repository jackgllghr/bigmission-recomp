#include "movie/subtitles.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace sarusaru::movie {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// HH:MM:SS,mmm (a '.' also separates the milliseconds, as WebVTT writes it).
std::optional<std::uint32_t> timestamp(std::string_view text) {
    text = trim(text);
    unsigned fields[4]{};
    std::size_t field = 0, digits = 0;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            fields[field] = fields[field] * 10u + static_cast<unsigned>(c - '0');
            if (++digits > 3) return std::nullopt;
        } else if ((c == ':' && field < 2) || ((c == ',' || c == '.') && field == 2)) {
            if (digits == 0) return std::nullopt;
            ++field;
            digits = 0;
        } else {
            return std::nullopt;
        }
    }
    if (field != 3 || digits != 3 || fields[1] > 59 || fields[2] > 59) return std::nullopt;
    return ((fields[0] * 60u + fields[1]) * 60u + fields[2]) * 1000u + fields[3];
}

// Removes <i>, </b>, <font ...> and the like, which the overlay does not draw.
std::string strip_tags(std::string_view line) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '<') {
            const auto close = line.find('>', i);
            const bool tag = close != std::string_view::npos && close > i + 1 &&
                (std::isalpha(static_cast<unsigned char>(line[i + 1])) || line[i + 1] == '/');
            if (tag) {
                i = close;
                continue;
            }
        }
        out += line[i];
    }
    return out;
}

} // namespace

std::vector<Cue> parse_srt(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::vector<std::string_view> lines;
    for (std::size_t at = 0; at <= text.size();) {
        const auto end = std::min(text.find('\n', at), text.size());
        lines.push_back(trim(text.substr(at, end - at)));
        at = end + 1;
    }
    const auto fail = [](std::size_t line, const std::string &reason) {
        throw std::runtime_error("line " + std::to_string(line + 1) + ": " + reason);
    };
    std::vector<Cue> cues;
    std::size_t i = 0;
    while (i < lines.size()) {
        if (lines[i].empty()) { ++i; continue; }
        // An optional cue number, then the timing line.
        if (lines[i].find("-->") == std::string_view::npos) ++i;
        if (i >= lines.size() || lines[i].find("-->") == std::string_view::npos) fail(i, "expected a timing line (00:00:01,000 --> 00:00:02,500)");
        const auto arrow = lines[i].find("-->");
        auto after = lines[i].substr(arrow + 3);
        // WebVTT-style settings after the end time are ignored.
        after = trim(after);
        if (const auto space = after.find(' '); space != std::string_view::npos) after = after.substr(0, space);
        const auto start = timestamp(lines[i].substr(0, arrow));
        const auto end = timestamp(after);
        if (!start || !end) fail(i, "malformed time; write HH:MM:SS,mmm");
        if (*end <= *start) fail(i, "the cue ends before it starts");
        const auto timing_line = i++;
        Cue cue{*start, *end, {}};
        for (; i < lines.size() && !lines[i].empty(); ++i) {
            if (!cue.text.empty()) cue.text += '\n';
            cue.text += strip_tags(lines[i]);
        }
        if (trim(cue.text).empty()) fail(timing_line, "the cue has no text");
        cues.push_back(std::move(cue));
    }
    std::stable_sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) { return a.start_ms < b.start_ms; });
    return cues;
}

std::string subtitle_key(std::string_view movie_path) {
    if (const auto slash = movie_path.find_last_of("/\\:"); slash != std::string_view::npos)
        movie_path.remove_prefix(slash + 1);
    if (const auto dot = movie_path.rfind('.'); dot != std::string_view::npos && dot != 0) movie_path = movie_path.substr(0, dot);
    std::string key(movie_path);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

void SubtitleLibrary::load_directory(const std::filesystem::path &directory, std::string_view language) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return;
    const std::string suffix = "." + std::string(language) + ".srt";
    for (const auto &item : std::filesystem::directory_iterator(directory, error)) {
        const auto name = item.path().filename().string();
        if (!item.is_regular_file(error) || name.size() <= suffix.size() || !name.ends_with(suffix)) continue;
        std::ifstream in(item.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        try {
            add(subtitle_key(name.substr(0, name.size() - suffix.size())), parse_srt(text.str()));
        } catch (const std::exception &failure) {
            std::cerr << "sarusaru: subtitles " << item.path().string() << ": " << failure.what() << '\n';
        }
    }
}

const Cue *SubtitleLibrary::at(std::string_view movie_path, std::uint64_t position_ms) const {
    const auto found = movies_.find(subtitle_key(movie_path));
    if (found == movies_.end()) return nullptr;
    const auto &cues = found->second;
    // The latest cue to have started that has not ended: an earlier, longer
    // one still shows once a short one after it is over.
    auto next = std::upper_bound(cues.begin(), cues.end(), position_ms,
                                 [](std::uint64_t position, const Cue &cue) { return position < cue.start_ms; });
    while (next != cues.begin()) {
        --next;
        if (position_ms < next->end_ms) return &*next;
    }
    return nullptr;
}

} // namespace sarusaru::movie
