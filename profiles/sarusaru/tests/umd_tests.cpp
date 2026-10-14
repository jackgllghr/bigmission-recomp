#include "umd/disc_image.hpp"
#include "umd/umd_drive.hpp"

#include "psprecomp/common.hpp"
#include "synthetic_iso.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using sarusaru::umd::DiscImage;
using sarusaru::umd::kSectorSize;
using namespace sarusaru::test;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

bool rejected(const std::vector<std::uint8_t> &image, const char *name) {
    const auto path = write_image(image, name);
    bool threw = false;
    try { DiscImage disc(path); } catch (const psprecomp::Error &) { threw = true; }
    std::filesystem::remove(path);
    return threw;
}

void store_string(psprecomp::Runtime &runtime, std::uint32_t address, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) runtime.memory().store8(address + static_cast<std::uint32_t>(i), text[i]);
    runtime.memory().store8(address + static_cast<std::uint32_t>(text.size()), 0u);
}
}

int main() {
    const auto image = synthetic_image();
    const auto path = write_image(image, "sarusaru_umd_tests.iso");
    {
        DiscImage disc(path);
        require(disc.sector_count() == 24u && disc.root().directory, "volume metadata incorrect");
        const auto umd_data = disc.find("/umd_data.bin");
        require(umd_data && !umd_data->directory && umd_data->size == 11u && umd_data->name == "UMD_DATA.BIN",
                "case-insensitive root lookup failed");
        const auto bytes = disc.read_file(*umd_data, 64u);
        require(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == "TEST-00001|",
                "file contents incorrect");
        const auto data = disc.find("PSP_GAME/DATA.BIN");
        require(data && data->size == 3000u, "nested lookup failed");
        std::vector<std::uint8_t> chunk(100u);
        require(disc.read(*data, 2040u, chunk) == 100u && chunk[0] == static_cast<std::uint8_t>(2040u * 7u) &&
                    chunk[99] == static_cast<std::uint8_t>(2139u * 7u),
                "read across sector boundary incorrect");
        require(disc.read(*data, 2950u, chunk) == 50u && disc.read(*data, 3000u, chunk) == 0u, "read past EOF not clamped");
        require(!disc.find("PSP_GAME/../UMD_DATA.BIN") && !disc.find("PSP_GAME//DATA.BIN") &&
                    !disc.find("UMD_DATA.BIN/X") && !disc.find("MISSING"),
                "unsafe or missing path resolved");
        require(disc.list(disc.root()).size() == 2u, "root listing incorrect");

        psprecomp::Runtime runtime;
        sarusaru::umd::UmdDrive drive(disc);
        sarusaru::umd::register_umd(drive, runtime);
        auto &cpu = runtime.cpu();
        runtime.invoke_import("sceUmdUser", 0x46EBB729u, cpu);
        require(cpu.gpr[2] == 1u, "medium not reported");
        runtime.invoke_import("sceUmdUser", 0x6B4A146Cu, cpu);
        require(cpu.gpr[2] == 0x12u, "inactive drive stat incorrect");
        constexpr std::uint32_t alias = 0x08806000u;
        store_string(runtime, alias, "disc1:");
        cpu.set_gpr(4u, 1u); cpu.set_gpr(5u, alias);
        runtime.invoke_import("sceUmdUser", 0xC6183D47u, cpu);
        require(cpu.gpr[2] == sarusaru::umd::kInvalidArgument && !drive.activated(), "wrong drive alias accepted");
        store_string(runtime, alias, "disc0:");
        runtime.invoke_import("sceUmdUser", 0xC6183D47u, cpu);
        require(cpu.gpr[2] == 0u && drive.drive_stat() == 0x32u, "activation failed");
        cpu.set_gpr(4u, sarusaru::umd::kStatReady);
        runtime.invoke_import("sceUmdUser", 0x8EF08FCEu, cpu);
        require(!runtime.stopped() && cpu.gpr[2] == 0u, "satisfied wait did not return");
        cpu.set_gpr(4u, 1u); cpu.set_gpr(5u, alias);
        runtime.invoke_import("sceUmdUser", 0xE83742BAu, cpu);
        require(cpu.gpr[2] == 0u && !drive.activated(), "deactivation failed");
        cpu.set_gpr(4u, sarusaru::umd::kStatReady);
        runtime.invoke_import("sceUmdUser", 0x8EF08FCEu, cpu);
        require(runtime.stopped(), "unsatisfiable wait did not stop");
    }
    std::filesystem::remove(path);

    auto bad = image;
    bad[16u * kSectorSize + 1u] = 'X';
    require(rejected(bad, "sarusaru_umd_bad_signature.iso"), "missing ISO signature accepted");
    bad = image;  // Point /UMD_DATA.BIN (after the ".", ".." and PSP_GAME records) beyond the volume.
    record(bad, 18u * kSectorSize + 34u + 34u + 42u, "UMD_DATA.BIN;1", 99u, 11u, false);
    bool threw = false;
    {
        const auto bad_path = write_image(bad, "sarusaru_umd_bad_extent.iso");
        DiscImage disc(bad_path);
        try { (void)disc.find("UMD_DATA.BIN"); } catch (const psprecomp::Error &) { threw = true; }
        std::filesystem::remove(bad_path);
    }
    require(threw, "extent outside volume accepted");
    bad = image;
    bad.resize(20u * kSectorSize);
    require(rejected(bad, "sarusaru_umd_truncated.iso"), "truncated volume accepted");
    return 0;
}
