#include "install/update_check.hpp"
#include <cstdlib>
#include <iostream>

using namespace sarusaru::install;
void require(bool ok, const char *message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
int main() {
    using State = UpdateResult::State;
    const std::string feed = "SaruSaruRecomp-Update-v1\nversion\t1.2.10\nwindows-x64\thttps://github.com/jackgllghr/sarusaru-recomp/releases/download/v1.2.10/SaruSaruRecomp-v1.2.10-windows-x64.zip\n";
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
}
