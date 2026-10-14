#include "game_disc.hpp"

#include "psprecomp/common.hpp"
#include "psprecomp/sha256.hpp"

#include <exception>
#include <optional>
#include <vector>

namespace sarusaru {
namespace {

// UMD_DATA.BIN starts with the disc ID as "UCJS-10060|"; returns "UCJS10060".
std::optional<std::string> umd_disc_id(umd::DiscImage &disc) {
    const auto umd_data = disc.find("UMD_DATA.BIN");
    if (!umd_data) return std::nullopt;
    const auto bytes = disc.read_file(*umd_data, 64u);
    const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const auto bar = text.find('|');
    if (bar == text.npos || bar != 10u || text[4] != '-') return std::nullopt;
    return std::string(text.substr(0u, 4u)) + std::string(text.substr(5u, 5u));
}

std::vector<std::uint8_t> read_executable(umd::DiscImage &disc, const DiscIdentity &identity) {
    const auto boot = disc.find("PSP_GAME/SYSDIR/BOOT.BIN");
    if (!boot || boot->size != identity.executable_size) return {};
    auto bytes = disc.read_file(*boot, boot->size);
    if (psprecomp::sha256(bytes) != identity.executable_sha256) return {};
    return bytes;
}

} // namespace

DiscCheck check_disc(const std::filesystem::path &iso, const DiscIdentity &identity) {
    DiscCheck check;
    std::error_code error;
    if (!std::filesystem::is_regular_file(iso, error)) {
        check.result = DiscCheck::Result::Missing;
        return check;
    }
    try {
        umd::DiscImage disc(iso);
        const auto id = umd_disc_id(disc);
        if (!id) {
            check.result = DiscCheck::Result::NotPspGame;
            return check;
        }
        check.disc_id = *id;
        if (*id != identity.disc_id) {
            check.result = DiscCheck::Result::OtherGame;
            return check;
        }
        if (read_executable(disc, identity).empty()) {
            check.result = DiscCheck::Result::OtherExecutable;
            return check;
        }
        check.result = DiscCheck::Result::Supported;
    } catch (const psprecomp::Error &) {
        check.result = DiscCheck::Result::NotAnIso;
    } catch (const std::exception &) {
        check.result = DiscCheck::Result::Unreadable;
    }
    return check;
}

void verify_disc(umd::DiscImage &disc, const DiscIdentity &identity) {
    const auto id = umd_disc_id(disc);
    if (!id) throw psprecomp::Error("Disc image has no usable UMD_DATA.BIN");
    if (*id != identity.disc_id) throw psprecomp::Error("Disc image is not the supported game disc");
    if (read_executable(disc, identity).empty())
        throw psprecomp::Error("Disc image executable is not the supported executable");
}

psprecomp::Elf32Image executable_from_disc(const std::filesystem::path &iso, const DiscIdentity &identity) {
    umd::DiscImage disc(iso);
    verify_disc(disc, identity);
    return psprecomp::Elf32Image::from_bytes(read_executable(disc, identity), "disc0:/PSP_GAME/SYSDIR/BOOT.BIN");
}

} // namespace sarusaru
