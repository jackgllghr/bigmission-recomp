#include "install/download.hpp"

#include "psprecomp/sha256.hpp"

#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>
#include <vector>

namespace sarusaru::install {

std::optional<std::filesystem::path> curl_program() {
#if defined(_WIN32)
    return system_tool("curl.exe");
#elif defined(__APPLE__)
    return std::filesystem::path("/usr/bin/curl");
#else
    return on_path("curl");
#endif
}

bool download_verified(ProcessGroup &processes, std::string_view url, std::string_view sha256,
                       const std::filesystem::path &destination,
                       const std::function<void(std::uint64_t)> &downloaded, std::string &error) {
    const auto curl = curl_program();
    if (!curl) {
        error = "Cannot download " + std::string(url) + ": curl is not installed";
        return false;
    }
    std::error_code ignored;
    std::filesystem::create_directories(destination.parent_path(), ignored);
    auto partial = destination;
    partial += ".part";
    std::filesystem::remove(partial, ignored);

    std::atomic<bool> finished{false};
    std::thread watcher([&] {
        while (!finished) {
            std::error_code size_error;
            const auto size = std::filesystem::file_size(partial, size_error);
            if (!size_error) downloaded(size);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
    const auto fetched = processes.run({path_argument(*curl), "--fail", "--location", "--silent", "--show-error",
                                        "--output", path_argument(partial), std::string(url)});
    finished = true;
    watcher.join();
    if (!fetched.ok()) {
        std::filesystem::remove(partial, ignored);
        std::string output = fetched.output;
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' '))
            output.pop_back();
        error = "Cannot download " + std::string(url) + ": " + output;
        return false;
    }
    if (psprecomp::sha256_file(partial) != sha256) {
        std::filesystem::remove(partial, ignored);
        error = "The download from " + std::string(url) + " is not the expected file (SHA-256 mismatch)";
        return false;
    }
    std::filesystem::rename(partial, destination, ignored);
    if (ignored) {
        std::filesystem::remove(partial, ignored);
        error = "Cannot save " + path_argument(destination);
        return false;
    }
    return true;
}

bool unpack_zip(ProcessGroup &processes, const std::filesystem::path &archive, const std::filesystem::path &directory,
                std::string &error) {
#if defined(_WIN32)
    const std::vector<std::string> unpack = {path_argument(system_tool("tar.exe")), "-xf", path_argument(archive), "-C",
                                             path_argument(directory)};
#elif defined(__APPLE__)
    const std::vector<std::string> unpack = {"/usr/bin/tar", "-xf", path_argument(archive), "-C", path_argument(directory)};
#else
    std::vector<std::string> unpack;
    if (const auto unzip = on_path("unzip"))
        unpack = {path_argument(*unzip), "-q", path_argument(archive), "-d", path_argument(directory)};
    else if (const auto bsdtar = on_path("bsdtar"))
        unpack = {path_argument(*bsdtar), "-xf", path_argument(archive), "-C", path_argument(directory)};
    else if (const auto python = on_path("python3"))
        unpack = {path_argument(*python), "-m", "zipfile", "-e", path_argument(archive), path_argument(directory)};
    if (unpack.empty()) {
        error = "unpacking needs unzip, bsdtar or python3";
        return false;
    }
#endif
    const auto unpacked = processes.run(unpack);
    if (!unpacked.ok()) error = unpacked.output;
    return unpacked.ok();
}

} // namespace sarusaru::install
