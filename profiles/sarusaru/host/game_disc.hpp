#pragma once

// Recognising the user's own disc image: an ISO9660 image whose UMD_DATA.BIN
// names the supported disc and whose PSP_GAME/SYSDIR/BOOT.BIN is the analysed
// executable, byte for byte. The launcher checks a chosen image with it, and
// the game session reads its executable straight from a verified disc.
#include "umd/disc_image.hpp"
#include "psprecomp/elf32.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace sarusaru {

// What a disc must be. The game's own comes from config/supported_game.json;
// tests describe invented discs.
struct DiscIdentity {
    std::string_view disc_id;  // e.g. UCJS10060
    std::uint64_t executable_size{};
    std::string_view executable_sha256;
};

struct DiscCheck {
    enum class Result { Supported, Unreadable, NotAnIso, NotPspGame, OtherGame, OtherExecutable };
    Result result{Result::Unreadable};
    std::string disc_id;  // as UMD_DATA.BIN names it, when it has one
    // A sentence for the player saying what is wrong (empty when supported).
    std::string problem;
    [[nodiscard]] bool supported() const noexcept { return result == Result::Supported; }
};

// Never throws: every way an image can be wrong is a result.
[[nodiscard]] DiscCheck check_disc(const std::filesystem::path &iso, const DiscIdentity &identity);

// Throws psprecomp::Error unless `disc` is the supported disc.
void verify_disc(umd::DiscImage &disc, const DiscIdentity &identity);

// The executable from a disc verify_disc accepts (throws psprecomp::Error otherwise).
[[nodiscard]] psprecomp::Elf32Image executable_from_disc(const std::filesystem::path &iso, const DiscIdentity &identity);

} // namespace sarusaru
