// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.
#include "utility/savedata_dialog.hpp"

#include "save_data/savedata_store.hpp"
#include "platform/paths.hpp"
#include "psprecomp/common.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string_view>
#include <utility>
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
constexpr std::uint32_t kIdList = 0x5F4u; // {maxCount, resultCount, entries}
} // namespace param

// SceUtilitySavedataIdListEntry: st_mode, ctime, atime, mtime (ScePspDateTime,
// 16 bytes each), then the save name (char[20]).
constexpr std::uint32_t kIdListEntrySize = 0x48u;
constexpr std::uint32_t kDirectoryMode = 0x11FFu;

enum Mode : std::uint32_t {
    kAutoLoad = 0, kAutoSave = 1, kLoad = 2, kSave = 3, kListLoad = 4, kListSave = 5, kListDelete = 6,
    kDelete = 7, kSizes = 8, kAutoDelete = 9, kSingleDelete = 10, kList = 11,
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

    // LIST: the saves of this game whose names match the save-name pattern
    // ('*' any run of characters, '?' any one), sorted by name.
    std::uint32_t list() const {
        const std::uint32_t list = memory.load32(params + param::kIdList);
        if (list == 0u || !memory.contains(list, 12u)) return savedata_result::kLoadParam;
        const std::string game = read_fixed(memory, params + param::kGameName, 13u);
        const std::string pattern = save_name_at(memory, params + param::kSaveName);
        std::vector<std::pair<std::string, std::filesystem::file_time_type>> found;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(savedata::savedata_root(utility.memory_stick()), ec)) {
            const std::string folder = entry.path().filename().string();
            if (!entry.is_directory(ec) || !folder.starts_with(game)) continue;
            const std::string name = folder.substr(game.size());
            if (name.size() >= 20u || !matches(pattern, name)) continue;
            found.emplace_back(name, std::filesystem::last_write_time(entry.path(), ec));
        }
        std::sort(found.begin(), found.end());
        const auto max_count = static_cast<std::int32_t>(memory.load32(list));
        const std::uint32_t entries = memory.load32(list + 8u);
        std::uint32_t count = 0u;
        for (const auto &[name, written] : found) {
            if (static_cast<std::int32_t>(count) >= max_count) break;
            const std::uint32_t at = entries + count * kIdListEntrySize;
            if (!memory.contains(at, kIdListEntrySize)) break;
            memory.store32(at, kDirectoryMode);
            for (std::uint32_t time = 0u; time < 3u; ++time) write_date_time(at + 4u + time * 16u, written);
            write_fixed(memory, at + 52u, name, 20u);
            ++count;
        }
        memory.store32(list + 4u, count);
        std::cerr << "[savedata] listed " << count << " save(s) matching \"" << game << pattern << "\"\n";
        return savedata_result::kOk;
    }

    static bool matches(std::string_view pattern, std::string_view name) {
        if (pattern.empty()) return name.empty();
        if (pattern.front() == '*')
            return matches(pattern.substr(1u), name) || (!name.empty() && matches(pattern, name.substr(1u)));
        return !name.empty() && (pattern.front() == '?' || pattern.front() == name.front()) &&
               matches(pattern.substr(1u), name.substr(1u));
    }

    // ScePspDateTime: year, month, day, hour, minute, second (16-bit), microsecond.
    void write_date_time(std::uint32_t at, std::filesystem::file_time_type written) const {
#if defined(_MSC_VER)
        // MSVC's file_clock converts through UTC only (no to_sys).
        const auto system = std::chrono::clock_cast<std::chrono::system_clock>(written);
#else
        const auto system = std::chrono::file_clock::to_sys(written);
#endif
        const std::time_t seconds = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(system));
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif
        const std::uint16_t fields[6] = {static_cast<std::uint16_t>(local.tm_year + 1900),
            static_cast<std::uint16_t>(local.tm_mon + 1), static_cast<std::uint16_t>(local.tm_mday),
            static_cast<std::uint16_t>(local.tm_hour), static_cast<std::uint16_t>(local.tm_min),
            static_cast<std::uint16_t>(local.tm_sec)};
        for (std::uint32_t i = 0u; i < 6u; ++i) memory.store16(at + i * 2u, fields[i]);
        memory.store32(at + 12u, 0u);
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
    if (auto data = platform::user_data_directory()) return *data / "memstick";
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
    case kList:
        return request.list();
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
