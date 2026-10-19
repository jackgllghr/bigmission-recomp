#pragma once

// Copying the game's memory-stick saves to and from elsewhere on an Android
// device, where they live in the app's private storage and no file manager
// can reach them. The system's document picker chooses where: the activity
// (SaruSaruActivity.saveTransfer) writes them as a zip, or reads them from a
// zip or from a folder such as PPSSPP's PSP/SAVEDATA, as PSP save folders
// (<game><save>/PARAM.SFO, ...). It reports how it went itself. Elsewhere the
// saves folder is opened in the system's file manager instead.
#include <filesystem>
#include <string>
#include <string_view>

namespace sarusaru::platform {

enum class SaveTransfer { Export, ImportArchive, ImportFolder };

// What the activity says when the transfer ends: {0} is a count or a reason.
struct SaveTransferTexts {
    std::string exported, imported, nothing_found, failed;
};

#if defined(__ANDROID__)
inline constexpr bool kSaveTransfer = true;
// Opens the picker for `transfer` of the save folders under `savedata` whose
// names start with `prefix` (the game's). False if the picker could not open.
bool start_save_transfer(SaveTransfer transfer, const std::filesystem::path &savedata, std::string_view prefix,
                         const SaveTransferTexts &texts);
#else
inline constexpr bool kSaveTransfer = false;
inline bool start_save_transfer(SaveTransfer, const std::filesystem::path &, std::string_view, const SaveTransferTexts &) {
    return false;
}
#endif

} // namespace sarusaru::platform
