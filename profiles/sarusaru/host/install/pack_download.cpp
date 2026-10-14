#include "install/pack_download.hpp"

#include "install/download.hpp"
#include "install/pack_config.hpp"
#include "platform/paths.hpp"

#include <cstdlib>

namespace sarusaru::install {

std::optional<PackSource> english_pack_source() {
    if (kEnglishPackUrl.empty() || kEnglishPackSha256.size() != 64u) return std::nullopt;
    return PackSource{std::string(kEnglishPackUrl), std::string(kEnglishPackSha256),
                      std::strtoull(std::string(kEnglishPackSize).c_str(), nullptr, 10)};
}

std::optional<std::filesystem::path> english_pack_path(const PackSource &source) {
    auto data = platform::user_data_directory();
    if (!data || source.sha256.size() < 16u) return std::nullopt;
    return *data / "translations" / ("english-" + source.sha256.substr(0u, 16u) + ".sstp");
}

PackDownload::PackDownload(PackSource source, std::filesystem::path destination)
    : source_(std::move(source)), destination_(std::move(destination)) {
    progress_.step = Progress::Step::Downloading;
    progress_.total = source_.bytes;
    thread_ = std::thread([this] { run(); });
}

PackDownload::~PackDownload() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void PackDownload::cancel() {
    cancelled_ = true;
    processes_.cancel();
}

Progress PackDownload::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

void PackDownload::run() {
    std::string error;
    const bool ok = download_verified(processes_, source_.url, source_.sha256, destination_,
        [this](std::uint64_t bytes) {
            std::lock_guard lock(mutex_);
            progress_.done = bytes;
        },
        error);
    std::lock_guard lock(mutex_);
    if (cancelled_) {
        progress_.step = Progress::Step::Cancelled;
    } else if (ok) {
        progress_.step = Progress::Step::Done;
        progress_.done = progress_.total;
    } else {
        progress_.step = Progress::Step::Failed;
        progress_.error = error;
    }
}

} // namespace sarusaru::install
