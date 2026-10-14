#include "utility/savedata_dialog.hpp"

#include "save_data/param_sfo.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
using namespace sarusaru::utility;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

constexpr std::uint32_t kParams = 0x08810000u, kMsFree = 0x08811000u, kMsData = 0x08811100u, kNeeded = 0x08811200u,
                        kList = 0x08811300u, kData = 0x08812000u, kDataSize = 6272u;

void write_string(psprecomp::GuestMemory &memory, std::uint32_t address, const std::string &text) {
    for (std::size_t i = 0; i <= text.size(); ++i)
        memory.store8(address + static_cast<std::uint32_t>(i), i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0u);
}

// The request this game makes: UCJS10060 / MAIN / GAMEDATA.BIN with a game key.
void prepare(psprecomp::GuestMemory &memory, std::uint32_t mode) {
    memory.zero(kParams, 0x600u);
    memory.store32(kParams, 0x600u);
    memory.store32(kParams + 0x30u, mode);
    write_string(memory, kParams + 0x3Cu, "UCJS10060");
    write_string(memory, kParams + 0x4Cu, "MAIN");
    write_string(memory, kParams + 0x64u, "GAMEDATA.BIN");
    memory.store32(kParams + 0x74u, kData);
    memory.store32(kParams + 0x78u, kDataSize);
    memory.store32(kParams + 0x7Cu, kDataSize);
    write_string(memory, kParams + 0x80u, "Title");
    memory.store32(kParams + 0x5D0u, kMsFree);
    memory.store32(kParams + 0x5D4u, kMsData);
    memory.store32(kParams + 0x5D8u, kNeeded);
    for (std::uint32_t i = 0; i < 16u; ++i) memory.store8(kParams + 0x5DCu + i, static_cast<std::uint8_t>(i + 1u));
    write_string(memory, kMsData, "UCJS10060");
    write_string(memory, kMsData + 16u, "MAIN");
}

std::uint32_t run(psprecomp::Runtime &runtime, std::uint32_t mode, bool prepared = false) {
    auto &cpu = runtime.cpu();
    if (!prepared) prepare(runtime.memory(), mode);
    cpu.set_gpr(4u, kParams);
    runtime.invoke_import("sceUtility", 0x50C4CD57u, cpu);
    require(cpu.gpr[2] == 0u, "InitStart failed");
    // Poll to QUIT, then shut down to NONE.
    for (const std::uint32_t expected : {1u, 2u, 3u}) {
        runtime.invoke_import("sceUtility", 0x8874DBE0u, cpu);
        require(cpu.gpr[2] == expected, "dialog status sequence");
    }
    runtime.invoke_import("sceUtility", 0x9790B33Cu, cpu);
    require(cpu.gpr[2] == 0u, "ShutdownStart");
    runtime.invoke_import("sceUtility", 0x8874DBE0u, cpu);
    require(cpu.gpr[2] == 4u, "FINISHED after shutdown");
    runtime.invoke_import("sceUtility", 0x8874DBE0u, cpu);
    require(cpu.gpr[2] == 0u, "NONE after FINISHED");
    return runtime.memory().load32(kParams + 0x1Cu);
}
}

int main() {
    const auto stick = std::filesystem::temp_directory_path() / "sarusaru_savedata_dialog_tests";
    std::filesystem::remove_all(stick);
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    SavedataUtility savedata(stick, [] { return 0x03030010u; });
    register_savedata(savedata, runtime);
    require(savedata.keyed_mode() == sarusaru::savedata::CryptMode::Mode3, "SDK 3 games use mode 3");

    require(run(runtime, 8u) == savedata_result::kSizesNoData, "SIZES on an empty stick reports no data");
    require(memory.load32(kMsFree) == 0x8000u && memory.load32(kMsData + 36u) == 0u && memory.load32(kNeeded) == 1u,
            "SIZES outputs");

    prepare(memory, 1u);
    for (std::uint32_t i = 0; i < kDataSize; ++i) memory.store8(kData + i, static_cast<std::uint8_t>(i * 13u));
    require(run(runtime, 1u, true) == savedata_result::kOk, "AUTOSAVE");
    const auto folder = stick / "PSP" / "SAVEDATA" / "UCJS10060MAIN";
    std::ifstream in(folder / "PARAM.SFO", std::ios::binary);
    const std::vector<std::uint8_t> sfo_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto sfo = sarusaru::savedata::ParamSfo::parse(sfo_bytes);
    const auto *params = sfo ? sfo->binary("SAVEDATA_PARAMS") : nullptr;
    require(params != nullptr && params->at(0) == 0x21u && sfo->string("TITLE") == "Title" &&
                std::filesystem::file_size(folder / "GAMEDATA.BIN") == 16u + kDataSize,
            "the save is written in PSP layout, encrypted in mode 3");

    memory.zero(kData, kDataSize);
    require(run(runtime, 0u) == savedata_result::kOk, "AUTOLOAD");
    bool restored = memory.load32(kParams + 0x7Cu) == kDataSize;
    for (std::uint32_t i = 0; i < kDataSize && restored; ++i) restored = memory.load8(kData + i) == static_cast<std::uint8_t>(i * 13u);
    require(restored, "AUTOLOAD restores the saved bytes");

    require(run(runtime, 8u) == savedata_result::kOk && memory.load32(kMsData + 36u) == 1u, "SIZES sees the save");

    prepare(memory, 4u);
    write_string(memory, kList, "NOPE");
    write_string(memory, kList + 20u, "MAIN");
    memory.store8(kList + 40u, 0u);
    memory.store32(kParams + 0x60u, kList);
    memory.zero(kData, kDataSize);
    require(run(runtime, 4u, true) == savedata_result::kOk && memory.read_c_string(kParams + 0x4Cu) == "MAIN" &&
                memory.load8(kData + 1u) == 13u,
            "LISTLOAD picks the first existing save");

    require(run(runtime, 13u) == savedata_result::kLoadParam, "unmodeled modes report a parameter error");
    runtime.invoke_import("sceUtility", 0x9790B33Cu, runtime.cpu());
    require(runtime.cpu().gpr[2] == kErrorInvalidStatus, "ShutdownStart without a dialog");
    std::filesystem::remove_all(stick);
    return 0;
}
