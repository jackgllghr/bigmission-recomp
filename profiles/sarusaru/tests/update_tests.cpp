#include "install/pack_versions.hpp"
#include "install/update_check.hpp"
#include <cstdlib>
#include <iostream>

using namespace sarusaru::install;
void require(bool ok, const char *message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
int main() {
    using State = UpdateResult::State;
    const std::string feed = "SaruSaruRecomp-Update-v1\nversion\t1.2.10\nwindows-x64\thttps://github.com/jackgllghr/bigmission-recomp/releases/download/v1.2.10/SaruSaruRecomp-v1.2.10-windows-x64.zip\n";
    const auto result = parse_update_feed(feed, "1.2.9", "windows-x64");
    require(result.state == State::Available && result.version == "1.2.10" && !result.download_url.empty(), "numeric version comparison failed");
    require(parse_update_feed(feed, "1.2.10", "windows-x64").state == State::Current, "equal version is an update");
    require(parse_update_feed(feed, "2.0.0", "windows-x64").state == State::Current, "downgrade offered");
    require(parse_update_feed(feed, "1.0.0", "linux-arm64").state == State::Unsupported, "wrong platform offered");
    require(parse_update_feed(feed + "version\t1.3.0\n", "1.0.0", "windows-x64").state == State::Failed, "duplicate version accepted");
    auto bad = feed; bad.replace(bad.find("https://github.com"), 18, "https://evil.test/");
    require(parse_update_feed(bad, "1.0.0", "windows-x64").state == State::Failed, "arbitrary update URL accepted");
    require(parse_update_feed(feed.substr(0, feed.size()-1), "1.0.0", "windows-x64").state == State::Failed, "partial feed accepted");
    require(parse_update_feed(std::string(9000, 'x'), "1.0.0", "windows-x64").state == State::Failed, "oversized feed accepted");
    for (const auto version : {"v1.0.0", "1.0", "01.2.3", "1.2.3-beta", "1.2.3.4", "1.-1.0", "4294967296.0.0"})
        require(!parse_release_version(version), "invalid stable version accepted");
    require(update_check_due(100000, 0) && update_check_due(100000, 100000-86400) &&
            !update_check_due(100000, 99999) && update_check_due(100000, 200000), "daily check clock handling failed");

    // The packs' latest releases, as the GitHub API describes them.
    const std::string hd = R"({
  "tag_name": "hd-2026-10-18-previews", "draft": false, "prerelease": false,
  "name": "HD textures \"previews\" \u2014 2026", "published_at": "2026-10-18T20:00:41Z",
  "body": "Redraws the stage-select previews.\n",
  "assets": [{"name": "sarusaru-hd-4x.zip", "size": 102837429,
    "digest": "sha256:d72fb001aa57b5e457b9044c38aadd9cdba883b2a63aab1c3d0c3fd6825af6d4",
    "browser_download_url": "https://github.com/jackgllghr/sarusaru-hd-textures/releases/download/hd-2026-10-18-previews/sarusaru-hd-4x.zip"},
   {"name": "notes.txt", "size": 3, "digest": null, "browser_download_url": "https://example.test/notes.txt"}]
})";
    const auto latest = parse_latest_release(hd, PackKind::HdTextures);
    require(latest && latest->version == "2026-10-18-previews" && latest->published == "2026-10-18" &&
                latest->source.bytes == 102837429u &&
                latest->source.sha256 == "d72fb001aa57b5e457b9044c38aadd9cdba883b2a63aab1c3d0c3fd6825af6d4" &&
                latest->source.url.ends_with("/hd-2026-10-18-previews/sarusaru-hd-4x.zip"),
            "the HD pack's latest release was not read");
    require(!parse_latest_release(hd, PackKind::English), "an HD release was read as an English one");
    auto moved = hd;
    moved.replace(moved.find("https://github.com/jackgllghr/sarusaru-hd-textures"), 50, "https://github.com/someone-else/sarusaru-hd-texture");
    require(!parse_latest_release(moved, PackKind::HdTextures), "an asset from another repository was accepted");
    auto undigested = hd;
    undigested.replace(undigested.find("sha256:d72f"), 11, "md5:d72fb00");
    require(!parse_latest_release(undigested, PackKind::HdTextures), "an asset without a SHA-256 was accepted");
    require(!parse_latest_release(hd.substr(0, hd.size() - 2), PackKind::HdTextures), "truncated JSON was accepted");
    auto doubled = hd;
    doubled.replace(doubled.find("\"notes.txt\""), 11, "\"sarusaru-hd-4x.zip\"");
    require(!parse_latest_release(doubled, PackKind::HdTextures), "a release with two HD assets was accepted");
    const std::string english = R"({"tag_name":"english-e94f09a659f6","published_at":"2026-10-18T22:41:45Z","assets":[
      {"name":"english-e94f09a659f6.sstp","size":65467554,
       "digest":"sha256:8efe91be318684ca08cb29b1d0806d37cd32b9c453187c8de122eaca038a2d40",
       "browser_download_url":"https://github.com/jackgllghr/sarusaru-translations/releases/download/english-e94f09a659f6/english-e94f09a659f6.sstp"}]})";
    const auto text = parse_latest_release(english, PackKind::English);
    require(text && text->version == "e94f09a659f6" && text->source.bytes == 65467554u, "the English pack's latest release was not read");
    require(release_tag("https://github.com/jackgllghr/sarusaru-translations/releases/download/english-e94f09a659f6/x.sstp") ==
                "english-e94f09a659f6" && release_tag("https://example.test/x.sstp").empty() &&
                release_version("hd-2026-10-18") == "2026-10-18",
            "release tags are not read from their URLs");
}
