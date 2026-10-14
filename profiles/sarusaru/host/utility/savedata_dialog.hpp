#pragma once

// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.

// sceUtilitySavedata*: the PSP's save-data utility, backed by PSP-layout
// folders under a host memory-stick directory (save_data/savedata_store.hpp),
// so a save folder can be copied to or from a real memory stick unchanged.
//
// No save-data UI is drawn yet: list modes act as if the player confirmed
// the first suitable entry. Modes beyond load/save/list/delete/sizes report a
// parameter error and are logged.
#include "save_data/savedata_crypto.hpp"
#include "utility/dialog.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace sarusaru::utility {

namespace savedata_result {
inline constexpr std::uint32_t kOk = 0u;
inline constexpr std::uint32_t kLoadDataBroken = 0x80110306u;
inline constexpr std::uint32_t kLoadNoData = 0x80110307u;
inline constexpr std::uint32_t kLoadParam = 0x80110308u;
inline constexpr std::uint32_t kDeleteNoData = 0x80110347u;
inline constexpr std::uint32_t kSaveAccessError = 0x80110385u;
inline constexpr std::uint32_t kSaveParam = 0x80110388u;
inline constexpr std::uint32_t kSizesNoData = 0x801103C7u;
} // namespace savedata_result

// The memory-stick root for this user: SARUSARU_MEMSTICK when set, otherwise
// a per-user application data directory.
[[nodiscard]] std::filesystem::path default_memory_stick();

class SavedataUtility {
public:
    // `compiled_sdk_version` reports the version the game registered (e.g.
    // 0x03030010); its major version decides the mode a PSP encrypts keyed
    // saves with.
    SavedataUtility(std::filesystem::path memory_stick, std::function<std::uint32_t()> compiled_sdk_version)
        : memory_stick_(std::move(memory_stick)), compiled_sdk_version_(std::move(compiled_sdk_version)) {}

    // Performs the request in the parameter block; returns its result code.
    [[nodiscard]] std::uint32_t run(psprecomp::GuestMemory &memory, std::uint32_t params);
    [[nodiscard]] savedata::CryptMode keyed_mode() const {
        return (compiled_sdk_version_() >> 24u) >= 4u ? savedata::CryptMode::Mode5 : savedata::CryptMode::Mode3;
    }
    [[nodiscard]] const std::filesystem::path &memory_stick() const noexcept { return memory_stick_; }
    DialogLifecycle dialog;

private:
    std::filesystem::path memory_stick_;
    std::function<std::uint32_t()> compiled_sdk_version_;
};

void register_savedata(SavedataUtility &savedata, psprecomp::Runtime &runtime);

} // namespace sarusaru::utility
