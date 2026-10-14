// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.
#include "utility/savedata_dialog.hpp"

#include "save_data/savedata_store.hpp"
#include "psprecomp/common.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <vector>

namespace sarusaru::utility {
namespace {

// SceUtilitySavedataParam, after the common dialog header.
namespace param {
constexpr std::uint32_t kMode = 0x30u;
constexpr std::uint32_t kGameName = 0x3Cu;     // char[13]
constexpr std::uint32_t kSaveName = 0x4Cu;     // char[20]
constexpr std::uint32_t kSaveNameList = 0x60u; // pointer to char[20] entries, "" terminated
constexpr std::uint32_t kFileName = 0x64u;     // char[13]
constexpr std::uint32_t kDataBuf = 0x74u;
constexpr std::uint32_t kDataBufSize = 0x78u;
constexpr std::uint32_t kDataSize = 0x7Cu;
constexpr std::uint32_t kTitle = 0x80u;          // char[128]
constexpr std::uint32_t kSavedataTitle = 0x100u; // char[128]
constexpr std::uint32_t kDetail = 0x180u;        // char[1024]
constexpr std::uint32_t kParentalLevel = 0x580u;
constexpr std::uint32_t kIcon0 = 0x584u; // {buf, bufSize, size, unknown}
constexpr std::uint32_t kIcon1 = 0x594u;
constexpr std::uint32_t kPic1 = 0x5A4u;
constexpr std::uint32_t kSnd0 = 0x5B4u;
constexpr std::uint32_t kMsFree = 0x5D0u;
constexpr std::uint32_t kMsData = 0x5D4u;
constexpr std::uint32_t kUtilityData = 0x5D8u;
constexpr std::uint32_t kKey = 0x5DCu; // char[16], firmware 2.00 and later
constexpr std::uint32_t kSecureVersion = 0x5ECu;
constexpr std::uint32_t kMinimumSizeWithKey = 0x5ECu;
} // namespace param

enum Mode : std::uint32_t {
    kAutoLoad = 0, kAutoSave = 1, kLoad = 2, kSave = 3, kListLoad = 4, kListSave = 5, kListDelete = 6,
    kDelete = 7, kSizes = 8, kAutoDelete = 9, kSingleDelete = 10,
};

const char *mode_name(std::uint32_t mode) {
    static const char *const names[] = {"AUTOLOAD", "AUTOSAVE", "LOAD", "SAVE", "LISTLOAD", "LISTSAVE", "LISTDELETE",
        "DELETE", "SIZES", "AUTODELETE", "SINGLEDELETE", "LIST", "FILES", "MAKEDATASECURE", "MAKEDATA",
        "READDATASECURE", "READDATA", "WRITEDATASECURE", "WRITEDATA", "ERASESECURE", "ERASE", "DELETEDATA", "GETSIZE"};
    return mode < std::size(names) ? names[mode] : "UNKNOWN";
}

constexpr std::uint32_t kClusterSize = 0x8000u;
constexpr std::uint32_t kFreeClusters = 0x8000u; // 1 GiB

// A fixed-size, NUL-padded guest string.
std::string read_fixed(const psprecomp::GuestMemory &memory, std::uint32_t address, std::uint32_t size) {
    std::string text;
    for (std::uint32_t i = 0; i < size; ++i) {
        const char c = static_cast<char>(memory.load8(address + i));
        if (c == '\0') break;
        text.push_back(c);
    }
    return text;
}

void write_fixed(psprecomp::GuestMemory &memory, std::uint32_t address, const std::string &text, std::uint32_t size) {
    for (std::uint32_t i = 0; i < size; ++i)
        memory.store8(address + i, i + 1u < size && i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0u);
}

// "<>" stands for "no save name": the folder is the game name alone.
std::string save_name_at(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    std::string name = read_fixed(memory, address, 20u);
    return name == "<>" ? std::string{} : name;
}

std::vector<std::string> save_name_list(const psprecomp::GuestMemory &memory, std::uint32_t list) {
    std::vector<std::string> names;
    if (list == 0u) return names;
    for (std::uint32_t i = 0; i < 64u; ++i) {
        const std::uint32_t entry = list + i * 20u;
        if (memory.load8(entry) == 0u) break;
        names.push_back(save_name_at(memory, entry));
    }
    return names;
}

std::vector<std::uint8_t> read_guest(const psprecomp::GuestMemory &memory, std::uint32_t address, std::uint32_t size) {
    std::vector<std::uint8_t> bytes(size);
    if (size != 0u) memory.copy_out(address, bytes);
    return bytes;
}

// {buf, bufSize, size, unknown}: the bytes the game supplies for an icon file.
std::vector<std::uint8_t> read_file_data(const psprecomp::GuestMemory &memory, std::uint32_t field) {
    const std::uint32_t buffer = memory.load32(field);
    const std::uint32_t size = std::min(memory.load32(field + 8u), memory.load32(field + 4u));
    if (buffer == 0u || size == 0u) return {};
    return read_guest(memory, buffer, size);
}

void write_size_string(psprecomp::GuestMemory &memory, std::uint32_t address, std::uint64_t kilobytes) {
    const std::string text = kilobytes >= 1024u * 1024u ? std::to_string(kilobytes / (1024u * 1024u)) + " GB"
        : kilobytes >= 1024u                            ? std::to_string(kilobytes / 1024u) + " MB"
                                                        : std::to_string(kilobytes) + " KB";
    write_fixed(memory, address, text, 8u);
}

struct Request {
    psprecomp::GuestMemory &memory;
    std::uint32_t params;
    const SavedataUtility &utility;

    savedata::SaveFiles files_for(const std::string &save_name) const {
        savedata::SaveFiles files;
        files.game_name = read_fixed(memory, params + param::kGameName, 13u);
        files.save_name = save_name;
        files.file_name = read_fixed(memory, params + param::kFileName, 13u);
        files.mode = utility.keyed_mode();
        if (memory.load32(params + dialog_common::kSizeOffset) > param::kMinimumSizeWithKey) {
            savedata::Block key{};
            for (std::uint32_t i = 0; i < key.size(); ++i) key[i] = memory.load8(params + param::kKey + i);
            if (!savedata::is_zero(key)) files.key = key;
        }
        return files;
    }

    std::string folder(const savedata::SaveFiles &files) const {
        return savedata::save_folder(utility.memory_stick(), files).string();
    }

    std::uint32_t load(const std::string &save_name) const {
        const auto files = files_for(save_name);
        const std::uint32_t buffer = memory.load32(params + param::kDataBuf);
        const std::uint32_t capacity = memory.load32(params + param::kDataBufSize);
        if (files.game_name.empty() || files.file_name.empty() || buffer == 0u) return savedata_result::kLoadParam;
        const auto loaded = savedata::load_save(utility.memory_stick(), files);
        if (loaded.status == savedata::LoadStatus::NoData) {
            std::cerr << "[savedata] no save data in " << folder(files) << "\n";
            return savedata_result::kLoadNoData;
        }
        if (loaded.status == savedata::LoadStatus::Broken) {
            std::cerr << "[savedata] broken save in " << folder(files) << ": " << loaded.reason << "\n";
            return savedata_result::kLoadDataBroken;
        }
        const auto &data = loaded.contents.data;
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(data.size(), capacity));
        if (!memory.contains(buffer, count)) return savedata_result::kLoadParam;
        memory.copy_in(buffer, std::span<const std::uint8_t>(data.data(), count));
        memory.store32(params + param::kDataSize, count);
        write_fixed(memory, params + param::kTitle, loaded.contents.title, 128u);
        write_fixed(memory, params + param::kSavedataTitle, loaded.contents.savedata_title, 128u);
        write_fixed(memory, params + param::kDetail, loaded.contents.detail, 1024u);
        std::cerr << "[savedata] loaded " << count << " bytes from " << folder(files)
                  << (files.key ? " (decrypted)" : "") << "\n";
        return savedata_result::kOk;
    }

    std::uint32_t save(const std::string &save_name) const {
        const auto files = files_for(save_name);
        const std::uint32_t buffer = memory.load32(params + param::kDataBuf);
        const std::uint32_t size = memory.load32(params + param::kDataSize);
        if (files.game_name.empty() || files.file_name.empty() || buffer == 0u || !memory.contains(buffer, size))
            return savedata_result::kSaveParam;
        savedata::SaveContents contents;
        contents.data = read_guest(memory, buffer, size);
        contents.title = read_fixed(memory, params + param::kTitle, 128u);
        contents.savedata_title = read_fixed(memory, params + param::kSavedataTitle, 128u);
        contents.detail = read_fixed(memory, params + param::kDetail, 1024u);
        contents.parental_level = memory.load8(params + param::kParentalLevel);
        contents.icon0 = read_file_data(memory, params + param::kIcon0);
        contents.icon1 = read_file_data(memory, params + param::kIcon1);
        contents.pic1 = read_file_data(memory, params + param::kPic1);
        contents.snd0 = read_file_data(memory, params + param::kSnd0);
        std::string error;
        if (!savedata::write_save(utility.memory_stick(), files, contents, error)) {
            std::cerr << "[savedata] save failed: " << error << "\n";
            return savedata_result::kSaveAccessError;
        }
        std::cerr << "[savedata] saved " << size << " bytes to " << folder(files)
                  << (files.key ? " (encrypted, mode " + std::to_string(static_cast<int>(files.mode)) + ")" : "")
                  << "\n";
        return savedata_result::kOk;
    }

    std::uint32_t remove(const std::string &save_name) const {
        const auto files = files_for(save_name);
        if (!savedata::delete_save(utility.memory_stick(), files)) return savedata_result::kDeleteNoData;
        std::cerr << "[savedata] deleted " << folder(files) << "\n";
        return savedata_result::kOk;
    }

    // SIZES: free space on the stick, the space an existing save takes, and
    // the space the requested save would need.
    std::uint32_t sizes() const {
        std::uint32_t outcome = savedata_result::kOk;
        if (const std::uint32_t free = memory.load32(params + param::kMsFree); free != 0u) {
            memory.store32(free, kClusterSize);
            memory.store32(free + 4u, kFreeClusters);
            const std::uint64_t free_kb = static_cast<std::uint64_t>(kClusterSize) * kFreeClusters / 1024u;
            memory.store32(free + 8u, static_cast<std::uint32_t>(free_kb));
            write_size_string(memory, free + 12u, free_kb);
        }
        if (const std::uint32_t data = memory.load32(params + param::kMsData); data != 0u) {
            savedata::SaveFiles files;
            files.game_name = read_fixed(memory, data, 13u);
            files.save_name = save_name_at(memory, data + 16u);
            const std::uint64_t bytes = savedata::save_size(utility.memory_stick(), files);
            const std::uint32_t info = data + 36u;
            const auto clusters = static_cast<std::uint32_t>((bytes + kClusterSize - 1u) / kClusterSize);
            memory.store32(info, clusters);
            memory.store32(info + 4u, static_cast<std::uint32_t>((bytes + 1023u) / 1024u));
            write_size_string(memory, info + 8u, (bytes + 1023u) / 1024u);
            memory.store32(info + 16u, static_cast<std::uint32_t>((bytes + 1023u) / 1024u));
            write_size_string(memory, info + 20u, (bytes + 1023u) / 1024u);
            if (bytes == 0u) outcome = savedata_result::kSizesNoData;
        }
        if (const std::uint32_t needed = memory.load32(params + param::kUtilityData); needed != 0u) {
            std::uint64_t bytes = memory.load32(params + param::kDataSize) + 16u;
            for (const std::uint32_t field : {param::kIcon0, param::kIcon1, param::kPic1, param::kSnd0})
                bytes += memory.load32(params + field + 8u);
            bytes += 0x2000u; // PARAM.SFO and directory entries
            const auto clusters = static_cast<std::uint32_t>((bytes + kClusterSize - 1u) / kClusterSize);
            const std::uint64_t kb = static_cast<std::uint64_t>(clusters) * kClusterSize / 1024u;
            memory.store32(needed, clusters);
            memory.store32(needed + 4u, static_cast<std::uint32_t>(kb));
            write_size_string(memory, needed + 8u, kb);
            memory.store32(needed + 16u, static_cast<std::uint32_t>(kb));
            write_size_string(memory, needed + 20u, kb);
        }
        return outcome;
    }

    // The save a list dialog would land on when the player just confirms:
    // for loading and deleting the first listed save that exists, for saving
    // the first listed name.
    std::optional<std::string> pick_from_list(bool must_exist) const {
        const auto names = save_name_list(memory, memory.load32(params + param::kSaveNameList));
        for (const auto &name : names) {
            if (!must_exist || savedata::save_exists(utility.memory_stick(), files_for(name))) return name;
        }
        if (!must_exist) return save_name_at(memory, params + param::kSaveName);
        return std::nullopt;
    }
};

} // namespace

std::filesystem::path default_memory_stick() {
    if (const char *override_path = std::getenv("SARUSARU_MEMSTICK"); override_path != nullptr && *override_path != '\0')
        return override_path;
#if defined(_WIN32)
    if (const char *appdata = std::getenv("APPDATA")) return std::filesystem::path(appdata) / "SaruSaruRecomp" / "memstick";
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME"))
        return std::filesystem::path(home) / "Library" / "Application Support" / "SaruSaruRecomp" / "memstick";
#else
    if (const char *data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != '\0')
        return std::filesystem::path(data) / "SaruSaruRecomp" / "memstick";
    if (const char *home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local" / "share" / "SaruSaruRecomp" / "memstick";
#endif
    throw psprecomp::Error("Cannot determine a per-user data directory; set SARUSARU_MEMSTICK");
}

std::uint32_t SavedataUtility::run(psprecomp::GuestMemory &memory, std::uint32_t params) {
    const Request request{memory, params, *this};
    const std::uint32_t mode = memory.load32(params + param::kMode);
    const std::string save_name = save_name_at(memory, params + param::kSaveName);
    std::cerr << "[savedata] " << mode_name(mode) << " (" << mode << ") game=\""
              << read_fixed(memory, params + param::kGameName, 13u) << "\" save=\"" << save_name << "\" file=\""
              << read_fixed(memory, params + param::kFileName, 13u) << "\"\n";
    if (memory.load32(params + dialog_common::kSizeOffset) > param::kMinimumSizeWithKey &&
        memory.load32(params + param::kSecureVersion) != 0u)
        std::cerr << "[savedata] secureVersion " << memory.load32(params + param::kSecureVersion)
                  << " is not modeled; using the SDK's automatic mode\n";
    switch (mode) {
    case kAutoLoad:
    case kLoad:
        return request.load(save_name);
    case kAutoSave:
    case kSave:
        return request.save(save_name);
    case kListLoad: {
        const auto chosen = request.pick_from_list(true);
        if (!chosen) return savedata_result::kLoadNoData;
        write_fixed(memory, params + param::kSaveName, *chosen, 20u);
        return request.load(*chosen);
    }
    case kListSave: {
        const auto chosen = request.pick_from_list(false);
        write_fixed(memory, params + param::kSaveName, *chosen, 20u);
        return request.save(*chosen);
    }
    case kListDelete: {
        const auto chosen = request.pick_from_list(true);
        if (!chosen) return savedata_result::kDeleteNoData;
        write_fixed(memory, params + param::kSaveName, *chosen, 20u);
        return request.remove(*chosen);
    }
    case kDelete:
    case kAutoDelete:
    case kSingleDelete:
        return request.remove(save_name);
    case kSizes:
        return request.sizes();
    default:
        // Report a parameter error so the guest takes its failure path instead
        // of reading results that were never written.
        std::cerr << "[savedata] mode " << mode << " (" << mode_name(mode) << ") is not implemented\n";
        return savedata_result::kLoadParam;
    }
}

void register_savedata(SavedataUtility &savedata, psprecomp::Runtime &runtime) {
    runtime.register_hle("sceUtility", 0x50C4CD57u,  // sceUtilitySavedataInitStart
        [&savedata](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const std::uint32_t params = ctx.gpr[4];
            if (!rt.memory().contains(params, 0x5ECu)) { ctx.set_gpr(2u, savedata_result::kLoadParam); return; }
            if (savedata.dialog.active())
                std::cerr << "[savedata] InitStart while a dialog is active (status " << savedata.dialog.status() << ")\n";
            const std::uint32_t outcome = savedata.run(rt.memory(), params);
            rt.memory().store32(params + dialog_common::kResultOffset, outcome);
            if (outcome != savedata_result::kOk) std::cerr << "[savedata] result " << psprecomp::hex32(outcome) << "\n";
            savedata.dialog.start();
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceUtility", 0xD4B95FFBu,  // sceUtilitySavedataUpdate
        [&savedata](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            (void)savedata.dialog.poll();
            ctx.set_gpr(2u, 0u);
        });
    runtime.register_hle("sceUtility", 0x8874DBE0u,  // sceUtilitySavedataGetStatus
        [&savedata](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, savedata.dialog.poll());
        });
    runtime.register_hle("sceUtility", 0x9790B33Cu,  // sceUtilitySavedataShutdownStart
        [&savedata](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            if (!savedata.dialog.active()) { ctx.set_gpr(2u, kErrorInvalidStatus); return; }
            if (!savedata.dialog.shutdown()) std::cerr << "[savedata] ShutdownStart before the dialog finished\n";
            ctx.set_gpr(2u, 0u);
        });
}

} // namespace sarusaru::utility
