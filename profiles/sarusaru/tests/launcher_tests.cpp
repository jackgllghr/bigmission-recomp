// The launcher's logic without a window: its settings rows, the file
// browser's listings, and recognising the player's disc image (with invented
// ISO images; no game data).
#include "game_disc.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/sha256.hpp"
#include "synthetic_iso.hpp"
#include "ui/file_list.hpp"
#include "ui/settings_rows.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

using sarusaru::test::kSectorSize;
using sarusaru::test::record;

// An invented 26-sector disc: /UMD_DATA.BIN naming `id`, and
// /PSP_GAME/SYSDIR/BOOT.BIN holding `boot`.
std::vector<std::uint8_t> disc_image(std::string_view umd_data, const std::vector<std::uint8_t> &boot) {
    constexpr std::uint32_t sectors = 26u;
    std::vector<std::uint8_t> image(sectors * kSectorSize, 0u);
    const std::size_t pvd = 16u * kSectorSize;
    image[pvd] = 1u;
    for (std::size_t i = 0; i < 5u; ++i) image[pvd + 1u + i] = static_cast<std::uint8_t>("CD001"[i]);
    image[pvd + 6u] = 1u;
    sarusaru::test::both_endian(image, pvd + 80u, sectors, 4u);
    sarusaru::test::both_endian(image, pvd + 128u, kSectorSize, 2u);
    const std::string_view self("\0", 1), parent("\1", 1);
    record(image, pvd + 156u, self, 18u, kSectorSize, true);
    auto offset = record(image, 18u * kSectorSize, self, 18u, kSectorSize, true);
    offset = record(image, offset, parent, 18u, kSectorSize, true);
    offset = record(image, offset, "PSP_GAME", 19u, kSectorSize, true);
    record(image, offset, "UMD_DATA.BIN;1", 21u, static_cast<std::uint32_t>(umd_data.size()), false);
    offset = record(image, 19u * kSectorSize, self, 19u, kSectorSize, true);
    offset = record(image, offset, parent, 18u, kSectorSize, true);
    record(image, offset, "SYSDIR", 20u, kSectorSize, true);
    offset = record(image, 20u * kSectorSize, self, 20u, kSectorSize, true);
    offset = record(image, offset, parent, 19u, kSectorSize, true);
    record(image, offset, "BOOT.BIN;1", 22u, static_cast<std::uint32_t>(boot.size()), false);
    std::copy(umd_data.begin(), umd_data.end(), image.begin() + 21 * kSectorSize);
    std::copy(boot.begin(), boot.end(), image.begin() + 22 * kSectorSize);
    return image;
}

void rows() {
    using namespace sarusaru::ui;
    using sarusaru::shell::Settings;
    // The stepping rule: wraps at both ends, and an off-step value moves to
    // the nearest step in the direction pressed.
    constexpr double steps[] = {0, 15, 20, 30, 60};
    require(step_value(steps, 30, 1) == 60 && step_value(steps, 60, 1) == 0, "forward step or wrap wrong");
    require(step_value(steps, 0, -1) == 60 && step_value(steps, 20, -1) == 15, "backward step or wrap wrong");
    require(step_value(steps, 25, 1) == 30 && step_value(steps, 25, -1) == 20, "off-step value not snapped");
    require(step_value(steps, 75, 1) == 0 && step_value(steps, -5, -1) == 60, "out-of-range value not wrapped");

    Settings settings = sarusaru::shell::default_settings();
    settings.internal_resolution = 8;
    step(SettingRow::InternalResolution, settings, 1);
    require(settings.internal_resolution == 1, "resolution did not wrap up");
    step(SettingRow::InternalResolution, settings, -1);
    require(settings.internal_resolution == 8, "resolution did not wrap down");
    require(describe(SettingRow::InternalResolution, settings).value == "8× (3840×2176)", "resolution label wrong");

    settings.fov_degrees = 52.5f;
    step(SettingRow::Fov, settings, 1);
    require(settings.fov_degrees == 55.0f, "hand-set FOV not snapped to the next step");
    settings.fov_degrees = 120.0f;
    step(SettingRow::Fov, settings, 1);
    require(settings.fov_degrees == 0.0f && describe(SettingRow::Fov, settings).value.starts_with("Native"),
            "FOV did not wrap to native");
    step(SettingRow::Fov, settings, 1);
    require(settings.fov_degrees == 30.0f, "FOV after native is not 30");

    settings.window_width = 1000;
    step(SettingRow::WindowSize, settings, 1);
    require(settings.window_width == 1440 && settings.window_height == 816, "window size step wrong");

    settings.controls.dead_zone = 0.2f;
    step(SettingRow::DeadZone, settings, 1);
    require(std::abs(settings.controls.dead_zone - 0.25f) < 1e-6f && describe(SettingRow::DeadZone, settings).value == "25%",
            "dead zone step wrong");

    // Clashing settings are flagged.
    settings.frame_interpolation = true;
    settings.experimental_game_frame_rate = 0;
    settings.frame_rate = 0;
    require(!describe(SettingRow::FrameInterpolation, settings).warning, "interpolation flagged without a clash");
    step(SettingRow::GameFrameRate, settings, 1);
    require(settings.experimental_game_frame_rate == 15 && describe(SettingRow::FrameInterpolation, settings).warning &&
                describe(SettingRow::GameFrameRate, settings).warning,
            "experimental rate clash not flagged");

    step(SettingRow::ConfirmButton, settings, -1);
    require(!settings.confirm_south, "confirm button did not toggle");

    // Every row is on exactly one tab, and describes itself.
    int seen = 0;
    for (const auto &tab : settings_tabs())
        for (const auto row : tab.rows) {
            ++seen;
            const auto view = describe(row, settings);
            require(!view.label.empty() && !view.value.empty() && !view.description.empty(), "row without text");
        }
    require(seen == 9, "settings rows missing from the tabs");
}

void files(const std::filesystem::path &directory) {
    using namespace sarusaru::ui;
    const auto folder = directory / "browse";
    std::filesystem::create_directories(folder / "b folder");
    std::filesystem::create_directories(folder / "A folder");
    std::filesystem::create_directories(folder / ".hidden");
    for (const char *name : {"zeta.iso", "Alpha.ISO", "notes.txt", ".secret.iso"})
        std::ofstream(folder / name) << "data";
    auto listed = list_folder(folder, false);
    require(listed.size() == 4u, "listing did not skip hidden and non-disc files");
    require(listed[0].name == "A folder" && listed[1].name == "b folder" && listed[0].directory,
            "folders not first or not sorted ignoring case");
    require(listed[2].name == "Alpha.ISO" && listed[3].name == "zeta.iso" && listed[2].size == 4u,
            "disc images not sorted or sized");
    listed = list_folder(folder, true);
    require(listed.size() == 5u && listed[4].name == "zeta.iso" && listed[3].name == "notes.txt",
            "all-files listing wrong");
    try {
        (void)list_folder(folder / "missing", false);
        require(false, "missing folder listed");
    } catch (const std::filesystem::filesystem_error &) {}
    require(format_size(512) == "512 bytes" && format_size(1'500'000) == "1.5 MB" && format_size(1'800'000'000) == "1.8 GB",
            "sizes formatted wrongly");
}

void discs(const std::filesystem::path &directory) {
    using namespace sarusaru;
    std::vector<std::uint8_t> boot(3000u);
    for (std::size_t i = 0; i < boot.size(); ++i) boot[i] = static_cast<std::uint8_t>(i * 13u + 5u);
    const std::string hash = psprecomp::sha256(boot);
    const DiscIdentity identity{"TEST00001", boot.size(), hash};
    const auto write = [&](const char *name, const std::vector<std::uint8_t> &bytes) {
        const auto path = directory / name;
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()),
                                                     static_cast<std::streamsize>(bytes.size()));
        return path;
    };

    const auto good = write("good.iso", disc_image("TEST-00001|0001|", boot));
    const auto check = check_disc(good, identity);
    require(check.supported() && check.disc_id == "TEST00001" && check.problem.empty(), "supported disc rejected");

    require(check_disc(directory / "absent.iso", identity).result == DiscCheck::Result::Unreadable, "missing file accepted");
    require(check_disc(write("text.iso", std::vector<std::uint8_t>(40000u, 'x')), identity).result ==
                DiscCheck::Result::NotAnIso,
            "non-ISO accepted");
    require(check_disc(write("nopsp.iso", disc_image("not a umd", boot)), identity).result == DiscCheck::Result::NotPspGame,
            "non-PSP disc accepted");
    const auto other = check_disc(write("other.iso", disc_image("ULUS-12345|0001|", boot)), identity);
    require(other.result == DiscCheck::Result::OtherGame && other.disc_id == "ULUS12345" &&
                other.problem.find("ULUS12345") != std::string::npos,
            "other game accepted or not named");
    auto changed = boot;
    changed[100] ^= 1u;
    require(check_disc(write("changed.iso", disc_image("TEST-00001|0001|", changed)), identity).result ==
                DiscCheck::Result::OtherExecutable,
            "changed executable accepted");

    umd::DiscImage disc(good);
    verify_disc(disc, identity);
    try {
        umd::DiscImage wrong(directory / "other.iso");
        verify_disc(wrong, identity);
        require(false, "verify_disc accepted another game");
    } catch (const psprecomp::Error &) {}
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("sarusaru-launcher-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    rows();
    files(directory);
    discs(directory);
    std::filesystem::remove_all(directory);
    std::cout << "Launcher rows, file listing and disc recognition tests passed\n";
}
