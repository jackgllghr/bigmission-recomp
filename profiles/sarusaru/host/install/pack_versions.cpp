#include "install/pack_versions.hpp"

#include "install/download.hpp"
#include "install/texture_build.hpp"
#include "platform/paths.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <system_error>
#include <variant>

namespace sarusaru::install {
namespace {

constexpr std::string_view kOwner = "jackgllghr";
constexpr std::string_view kEnglishRepository = "sarusaru-translations";
constexpr std::string_view kHdRepository = "sarusaru-hd-textures";
constexpr std::string_view kHdAsset = "sarusaru-hd-4x.zip";
// The API's answer for one release is a few kilobytes; its notes are bounded too.
constexpr std::size_t kMaxAnswer = 512u * 1024u;
constexpr std::uint64_t kMaxPackBytes = 4'000'000'000ull;

std::string_view repository(PackKind kind) { return kind == PackKind::English ? kEnglishRepository : kHdRepository; }

// Enough JSON for the API's answer: objects, arrays, strings, numbers and
// literals, nested at most 16 deep. Strings keep \u escapes outside ASCII
// as they are, which none of the fields read here contain.
struct Json {
    using Object = std::map<std::string, Json, std::less<>>;
    using Array = std::vector<Json>;
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> value;

    [[nodiscard]] const Json *at(std::string_view key) const {
        const auto *object = std::get_if<std::shared_ptr<Object>>(&value);
        if (object == nullptr) return nullptr;
        const auto found = (*object)->find(key);
        return found == (*object)->end() ? nullptr : &found->second;
    }
    [[nodiscard]] const std::string *text() const { return std::get_if<std::string>(&value); }
    [[nodiscard]] const double *number() const { return std::get_if<double>(&value); }
    [[nodiscard]] const Array *array() const {
        const auto *array = std::get_if<std::shared_ptr<Array>>(&value);
        return array == nullptr ? nullptr : array->get();
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}
    std::optional<Json> document() {
        auto value = parse(0);
        space();
        if (!value || at_ != text_.size()) return std::nullopt;
        return value;
    }

private:
    std::string_view text_;
    std::size_t at_{};

    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t'))
            ++at_;
    }
    bool literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return false;
        at_ += word.size();
        return true;
    }
    std::optional<std::string> string() {
        if (at_ >= text_.size() || text_[at_] != '"') return std::nullopt;
        ++at_;
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20u) return std::nullopt;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) return std::nullopt;
            switch (const char escaped = text_[at_++]) {
            case '"': case '\\': case '/': out += escaped; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (at_ + 4u > text_.size()) return std::nullopt;
                unsigned code = 0u;
                const auto parsed = std::from_chars(text_.data() + at_, text_.data() + at_ + 4u, code, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != text_.data() + at_ + 4u) return std::nullopt;
                at_ += 4u;
                if (code < 0x80u) out += static_cast<char>(code);
                else out += "\\u" + std::string(text_.substr(at_ - 4u, 4u));
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }
    std::optional<Json> parse(int depth) {
        if (depth > 16) return std::nullopt;
        space();
        if (at_ >= text_.size()) return std::nullopt;
        const char c = text_[at_];
        if (c == '{') {
            ++at_;
            auto object = std::make_shared<Json::Object>();
            space();
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                return Json{object};
            }
            for (;;) {
                space();
                auto key = string();
                space();
                if (!key || at_ >= text_.size() || text_[at_++] != ':') return std::nullopt;
                auto value = parse(depth + 1);
                if (!value) return std::nullopt;
                (*object)[*key] = std::move(*value);
                space();
                if (at_ >= text_.size()) return std::nullopt;
                if (text_[at_] == ',') { ++at_; continue; }
                if (text_[at_] == '}') { ++at_; return Json{object}; }
                return std::nullopt;
            }
        }
        if (c == '[') {
            ++at_;
            auto array = std::make_shared<Json::Array>();
            space();
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                return Json{array};
            }
            for (;;) {
                auto value = parse(depth + 1);
                if (!value) return std::nullopt;
                array->push_back(std::move(*value));
                space();
                if (at_ >= text_.size()) return std::nullopt;
                if (text_[at_] == ',') { ++at_; continue; }
                if (text_[at_] == ']') { ++at_; return Json{array}; }
                return std::nullopt;
            }
        }
        if (c == '"') {
            auto text = string();
            if (!text) return std::nullopt;
            return Json{std::move(*text)};
        }
        if (literal("true")) return Json{true};
        if (literal("false")) return Json{false};
        if (literal("null")) return Json{nullptr};
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) || text_[at_] == '-' ||
                                      text_[at_] == '+' || text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E'))
            ++at_;
        if (start == at_) return std::nullopt;
        const std::string digits(text_.substr(start, at_ - start));
        char *end = nullptr;
        const double number = std::strtod(digits.c_str(), &end);
        if (end != digits.c_str() + digits.size()) return std::nullopt;
        return Json{number};
    }
};

bool tag_character(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}
bool lower_hex(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool expected_asset(PackKind kind, std::string_view name) {
    if (kind == PackKind::HdTextures) return name == kHdAsset;
    if (!name.starts_with("english-") || !name.ends_with(".sstp")) return false;
    const auto middle = name.substr(8u, name.size() - 13u);
    return !middle.empty() && middle.size() <= 64u && std::all_of(middle.begin(), middle.end(), tag_character);
}

std::filesystem::path record_path(const std::filesystem::path &pack) {
    auto path = pack.lexically_normal();
    if (path.filename().empty()) path = path.parent_path();
    path += ".version";
    return path;
}

std::filesystem::path normal(const std::filesystem::path &path) {
    auto out = path.lexically_normal();
    if (out.filename().empty()) out = out.parent_path();
    return out;
}

std::optional<std::filesystem::path> packs_directory(PackKind kind) {
    auto data = platform::user_data_directory();
    if (!data) return std::nullopt;
    return *data / (kind == PackKind::English ? "translations" : "textures");
}

} // namespace

std::string release_tag(std::string_view url) {
    constexpr std::string_view kMarker = "/releases/download/";
    const auto at = url.find(kMarker);
    if (at == std::string_view::npos) return {};
    auto rest = url.substr(at + kMarker.size());
    const auto end = rest.find('/');
    if (end == std::string_view::npos || end == 0u) return {};
    return std::string(rest.substr(0u, end));
}

std::string release_version(std::string_view tag) {
    const auto dash = tag.find('-');
    if (dash == std::string_view::npos || dash + 1u >= tag.size()) return std::string(tag);
    return std::string(tag.substr(dash + 1u));
}

std::optional<PackRelease> pinned_release(PackKind kind) {
    const auto source = kind == PackKind::English ? english_pack_source() : prebuilt_pack_source();
    if (!source) return std::nullopt;
    const auto tag = release_tag(source->url);
    return PackRelease{*source, tag.empty() ? source->sha256.substr(0u, 12u) : release_version(tag), {}};
}

std::optional<PackRelease> pinned_prebuilt_release(std::uint32_t scale) {
    const auto source = prebuilt_pack_source(scale);
    if (!source) return std::nullopt;
    const auto tag = release_tag(source->url);
    return PackRelease{*source, tag.empty() ? source->sha256.substr(0u, 12u) : release_version(tag), {}};
}

std::string latest_release_url(PackKind kind) {
    return "https://api.github.com/repos/" + std::string(kOwner) + "/" + std::string(repository(kind)) + "/releases/latest";
}

std::optional<PackRelease> parse_latest_release(std::string_view json, PackKind kind) {
    if (json.size() > kMaxAnswer) return std::nullopt;
    const auto document = JsonReader(json).document();
    if (!document) return std::nullopt;
    const auto *tag = document->at("tag_name") ? document->at("tag_name")->text() : nullptr;
    if (tag == nullptr || tag->empty() || tag->size() > 100u || !std::all_of(tag->begin(), tag->end(), tag_character))
        return std::nullopt;
    for (const char *flag : {"draft", "prerelease"})
        if (const auto *value = document->at(flag); value && std::get_if<bool>(&value->value) && std::get<bool>(value->value))
            return std::nullopt;
    const auto *assets = document->at("assets") ? document->at("assets")->array() : nullptr;
    if (assets == nullptr) return std::nullopt;
    std::optional<PackRelease> found;
    for (const auto &asset : *assets) {
        const auto *name = asset.at("name") ? asset.at("name")->text() : nullptr;
        if (name == nullptr || !expected_asset(kind, *name)) continue;
        // One asset of the kind, or the release is not one this reads.
        if (found) return std::nullopt;
        const auto *url = asset.at("browser_download_url") ? asset.at("browser_download_url")->text() : nullptr;
        const auto *digest = asset.at("digest") ? asset.at("digest")->text() : nullptr;
        const auto *size = asset.at("size") ? asset.at("size")->number() : nullptr;
        const std::string expected = "https://github.com/" + std::string(kOwner) + "/" + std::string(repository(kind)) +
                                     "/releases/download/" + *tag + "/" + *name;
        if (url == nullptr || *url != expected || digest == nullptr || !digest->starts_with("sha256:") ||
            digest->size() != 71u || !lower_hex(std::string_view(*digest).substr(7u)) || size == nullptr ||
            *size < 1.0 || *size > static_cast<double>(kMaxPackBytes) || *size != static_cast<double>(static_cast<std::uint64_t>(*size)))
            return std::nullopt;
        PackRelease release;
        release.source = {*url, digest->substr(7u), static_cast<std::uint64_t>(*size)};
        release.version = release_version(*tag);
        if (const auto *published = document->at("published_at") ? document->at("published_at")->text() : nullptr;
            published != nullptr && published->size() >= 10u && (*published)[4] == '-' && (*published)[7] == '-')
            release.published = published->substr(0u, 10u);
        found = std::move(release);
    }
    return found;
}

std::optional<std::filesystem::path> release_destination(PackKind kind, const PackRelease &release) {
    return kind == PackKind::English ? english_pack_path(release.source) : prebuilt_pack_directory(release.source);
}

void record_release(const std::filesystem::path &pack, const PackRelease &release) {
    std::ofstream out(record_path(pack), std::ios::binary | std::ios::trunc);
    out << "version=" << release.version << "\npublished=" << release.published << "\nsha256=" << release.source.sha256
        << "\nurl=" << release.source.url << "\nbytes=" << release.source.bytes << '\n';
}

std::optional<PackRelease> installed_release(PackKind kind, const std::filesystem::path &pack) {
    if (pack.empty()) return std::nullopt;
    if (std::ifstream in(record_path(pack), std::ios::binary); in) {
        PackRelease release;
        std::string line;
        while (std::getline(in, line) && line.size() < 4096u) {
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = line.substr(0u, equals), value = line.substr(equals + 1u);
            if (key == "version") release.version = value;
            else if (key == "published") release.published = value;
            else if (key == "sha256") release.source.sha256 = value;
            else if (key == "url") release.source.url = value;
            else if (key == "bytes") release.source.bytes = std::strtoull(value.c_str(), nullptr, 10);
        }
        if (!release.version.empty()) return release;
    }
    if (const auto pinned = pinned_release(kind))
        if (const auto destination = release_destination(kind, *pinned); destination && normal(*destination) == normal(pack))
            return pinned;
    return std::nullopt;
}

std::string installed_pack_id(PackKind kind, const std::filesystem::path &pack) {
    const auto directory = packs_directory(kind);
    if (!directory || pack.empty() || normal(pack).parent_path() != normal(*directory)) return {};
    const auto name = normal(pack).filename().string();
    const std::string_view prefix = kind == PackKind::English ? "english-" : "4x-prebuilt-";
    const std::string_view suffix = kind == PackKind::English ? ".sstp" : "";
    if (!name.starts_with(prefix) || !name.ends_with(suffix) || name.size() <= prefix.size() + suffix.size()) return {};
    const auto id = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    return lower_hex(id) ? id : std::string();
}

bool is_installed(PackKind kind, const PackRelease &release, const std::filesystem::path &pack) {
    const auto destination = release_destination(kind, release);
    return destination && !pack.empty() && normal(*destination) == normal(pack);
}

std::vector<std::filesystem::path> installed_packs(PackKind kind, std::uint32_t scale) {
    const std::string prebuilt = std::to_string(scale) + "x-prebuilt-";
    std::vector<std::filesystem::path> packs;
    const auto directory = packs_directory(kind);
    std::error_code error;
    if (!directory || !std::filesystem::is_directory(*directory, error)) return packs;
    for (const auto &entry : std::filesystem::directory_iterator(*directory, error)) {
        const auto name = entry.path().filename().string();
        std::error_code type_error;
        if (kind == PackKind::English ? name.starts_with("english-") && name.ends_with(".sstp") &&
                                            entry.is_regular_file(type_error)
                                      : name.starts_with(prebuilt) && entry.is_directory(type_error) &&
                                            texture_pack_state(entry.path()) != TexturePackState::Missing)
            packs.push_back(entry.path());
    }
    const auto published = [&](const std::filesystem::path &pack) {
        const auto release = installed_release(kind, pack);
        return release ? release.value().published : std::string();
    };
    const auto written = [](const std::filesystem::path &pack) {
        std::error_code time_error;
        return std::filesystem::last_write_time(pack, time_error);
    };
    std::sort(packs.begin(), packs.end(), [&](const auto &a, const auto &b) {
        const auto pa = published(a), pb = published(b);
        if (pa != pb) return pa > pb;
        return written(a) > written(b);
    });
    return packs;
}

void remove_other_packs(PackKind kind, const std::filesystem::path &keep, std::uint32_t scale) {
    for (const auto &pack : installed_packs(kind, scale)) {
        if (normal(pack) == normal(keep)) continue;
        std::error_code ignored;
        std::filesystem::remove_all(pack, ignored);
        std::filesystem::remove(record_path(pack), ignored);
    }
}

PackUpdateCheck::PackUpdateCheck() : worker_([this] {
    Result result{Result::State::Failed};
    if (const auto curl = curl_program()) {
        bool any = false;
        for (const auto kind : {PackKind::English, PackKind::HdTextures}) {
            if (!pinned_release(kind)) continue;
            const auto fetched = processes_.run({path_argument(*curl), "--fail", "--location", "--silent", "--show-error",
                "--connect-timeout", "5", "--max-time", "20", "--max-redirs", "3", "--max-filesize",
                std::to_string(kMaxAnswer), "--proto", "=https", "--proto-redir", "=https",
                "--header", "Accept: application/vnd.github+json", latest_release_url(kind)});
            if (!fetched.ok()) continue;
            auto latest = parse_latest_release(fetched.output, kind);
            if (!latest) continue;
            any = true;
            (kind == PackKind::English ? result.english : result.hd_textures) = std::move(latest);
        }
        if (any) result.state = Result::State::Done;
    }
    std::lock_guard lock(mutex_);
    result_ = std::move(result);
}) {}

PackUpdateCheck::~PackUpdateCheck() {
    processes_.cancel();
    if (worker_.joinable()) worker_.join();
}

PackUpdateCheck::Result PackUpdateCheck::result() const {
    std::lock_guard lock(mutex_);
    return result_;
}

} // namespace sarusaru::install
