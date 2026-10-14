#include "io/io_manager.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>

namespace sarusaru::io {
namespace {
constexpr std::uint32_t kStatModeFile = 0x2000u | 0x016Du;       // FIO_S_IFREG, r-xr-xr-x
constexpr std::uint32_t kStatModeDirectory = 0x1000u | 0x016Du;  // FIO_S_IFDIR, r-xr-xr-x
constexpr std::uint32_t kStatAttrFile = 0x20u;
constexpr std::uint32_t kStatAttrDirectory = 0x10u;
constexpr std::uint32_t kReadChunk = 64u * 1024u;

std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return result;
}

// Parses "sce_lbn0x<sector>_size0x<bytes>".
std::optional<std::pair<std::uint32_t, std::uint32_t>> parse_lbn(std::string_view name) {
    const auto hex = [](std::string_view &text, std::string_view prefix) -> std::optional<std::uint32_t> {
        if (!text.starts_with(prefix)) return std::nullopt;
        text.remove_prefix(prefix.size());
        std::uint32_t value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (error != std::errc{} || end == text.data()) return std::nullopt;
        text.remove_prefix(static_cast<std::size_t>(end - text.data()));
        return value;
    };
    const auto sector = hex(name, "sce_lbn0x");
    const auto size = sector ? hex(name, "_size0x") : std::nullopt;
    if (!size || !name.empty()) return std::nullopt;
    return std::pair{*sector, *size};
}
}

void IoManager::trace(const std::string &line) const {
    static const bool enabled = std::getenv("SARUSARU_TRACE_IO") != nullptr;
    if (enabled) std::cerr << "[io] " << line << '\n';
}

std::variant<umd::DiscImage::Entry, std::uint32_t> IoManager::resolve(const std::string &path) {
    const auto colon = path.find(':');
    if (colon == std::string::npos) {
        runtime_->stop("Relative PSP path is not modeled yet: " + path);
        return kNoDevice;
    }
    const std::string device = lower(std::string_view(path).substr(0u, colon));
    std::string_view rest = std::string_view(path).substr(colon + 1u);
    if (device != "disc0") {
        runtime_->stop("PSP device '" + device + ":' is not modeled yet: " + path);
        return kNoDevice;
    }
    while (rest.starts_with('/')) rest.remove_prefix(1u);
    if (const auto lbn = parse_lbn(lower(rest))) {
        const std::uint64_t end = static_cast<std::uint64_t>(lbn->first) * umd::kSectorSize + lbn->second;
        if (end > static_cast<std::uint64_t>(disc_->sector_count()) * umd::kSectorSize) return kFileNotFound;
        return umd::DiscImage::Entry{std::string(rest), lbn->first, lbn->second, false};
    }
    try {
        if (auto entry = disc_->find(rest)) return *entry;
    } catch (const psprecomp::Error &error) {
        runtime_->stop(std::string("Disc image error resolving ") + path + ": " + error.what());
        return kFileNotFound;
    }
    return kFileNotFound;
}

std::int32_t IoManager::allocate(File file) {
    for (std::int32_t fd = 3; fd < static_cast<std::int32_t>(kMaxDescriptors); ++fd) {
        if (files_.contains(fd)) continue;
        files_.emplace(fd, std::move(file));
        return fd;
    }
    return static_cast<std::int32_t>(kTooManyFiles);
}

IoManager::File *IoManager::file(std::int32_t fd) noexcept {
    const auto found = files_.find(fd);
    return found == files_.end() ? nullptr : &found->second;
}

std::int32_t IoManager::open(const std::string &path, std::uint32_t flags) {
    if ((flags & kOpenWriteFlags) != 0u) {
        trace("open " + path + " flags=" + psprecomp::hex32(flags) + " -> read-only device");
        return static_cast<std::int32_t>(kReadOnly);
    }
    const auto resolved = resolve(path);
    if (const auto *error = std::get_if<std::uint32_t>(&resolved)) {
        trace("open " + path + " -> " + psprecomp::hex32(*error));
        return static_cast<std::int32_t>(*error);
    }
    const auto &entry = std::get<umd::DiscImage::Entry>(resolved);
    if (entry.directory) return static_cast<std::int32_t>(kFileNotFound);
    const auto fd = allocate(File{false, false, entry});
    trace("open " + path + " -> fd " + std::to_string(fd) + " size " + std::to_string(entry.size));
    return fd;
}

std::int32_t IoManager::close(std::int32_t fd) {
    File *open_file = file(fd);
    if (open_file == nullptr || open_file->directory) return static_cast<std::int32_t>(kBadFile);
    if (open_file->async_result) return static_cast<std::int32_t>(kAsyncBusy);
    files_.erase(fd);
    trace("close fd " + std::to_string(fd));
    return 0;
}

std::int64_t IoManager::read(std::int32_t fd, std::uint32_t address, std::uint32_t size) {
    File *open_file = file(fd);
    if (open_file == nullptr || open_file->directory || open_file->open_failed) return static_cast<std::int32_t>(kBadFile);
    if (open_file->async_result) return static_cast<std::int32_t>(kAsyncBusy);
    if (size > 0x7FFFFFFFu) return static_cast<std::int32_t>(kInvalidArgument);
    if (size != 0u && !runtime_->memory().contains(address, size)) return static_cast<std::int32_t>(kInvalidArgument);
    std::vector<std::uint8_t> buffer(std::min(size, kReadChunk));
    std::uint32_t total = 0u;
    try {
        while (total < size) {
            const auto wanted = std::min<std::uint32_t>(size - total, kReadChunk);
            const auto got = disc_->read(open_file->entry, open_file->position, std::span(buffer).first(wanted));
            if (got == 0u) break;
            runtime_->memory().copy_in(address + total, std::span(buffer).first(got));
            open_file->position += got;
            total += static_cast<std::uint32_t>(got);
        }
    } catch (const psprecomp::Error &error) {
        runtime_->stop(std::string("Disc read failed: ") + error.what());
        return static_cast<std::int32_t>(kBadFile);
    }
    trace("read fd " + std::to_string(fd) + " " + std::to_string(size) + " -> " + std::to_string(total));
    return total;
}

std::int32_t IoManager::write(std::int32_t fd, std::uint32_t address, std::uint32_t size) {
    if (fd == 1 || fd == 2) {
        if (!runtime_->memory().contains(address, size)) return static_cast<std::int32_t>(kInvalidArgument);
        std::string text(size, '\0');
        runtime_->memory().copy_out(address, std::span(reinterpret_cast<std::uint8_t *>(text.data()), text.size()));
        std::cerr << (fd == 1 ? "[guest stdout] " : "[guest stderr] ") << text << (text.ends_with('\n') ? "" : "\n");
        return static_cast<std::int32_t>(size);
    }
    if (file(fd) != nullptr) return static_cast<std::int32_t>(kReadOnly);
    return static_cast<std::int32_t>(kBadFile);
}

std::int64_t IoManager::seek(std::int32_t fd, std::int64_t offset, std::uint32_t whence) {
    File *open_file = file(fd);
    if (open_file == nullptr || open_file->directory || open_file->open_failed) return static_cast<std::int32_t>(kBadFile);
    if (open_file->async_result) return static_cast<std::int32_t>(kAsyncBusy);
    std::int64_t base = 0;
    if (whence == 1u) base = static_cast<std::int64_t>(open_file->position);
    else if (whence == 2u) base = open_file->entry.size;
    else if (whence != 0u) return static_cast<std::int32_t>(kInvalidArgument);
    const std::int64_t position = base + offset;
    if (position < 0) return static_cast<std::int32_t>(kInvalidArgument);
    open_file->position = static_cast<std::uint64_t>(position);
    return position;
}

void IoManager::write_stat(std::uint32_t address, const umd::DiscImage::Entry &entry) {
    auto &memory = runtime_->memory();
    memory.zero(address, kStatSize);
    memory.store32(address, entry.directory ? kStatModeDirectory : kStatModeFile);
    memory.store32(address + 4u, entry.directory ? kStatAttrDirectory : kStatAttrFile);
    memory.store32(address + 8u, entry.size);
    // Disc timestamps are not read yet; st_private[0] holds the start sector.
    memory.store32(address + 64u, entry.sector);
}

std::int32_t IoManager::get_stat(const std::string &path, std::uint32_t address) {
    if (!runtime_->memory().contains(address, kStatSize)) return static_cast<std::int32_t>(kInvalidArgument);
    const auto resolved = resolve(path);
    if (const auto *error = std::get_if<std::uint32_t>(&resolved)) return static_cast<std::int32_t>(*error);
    write_stat(address, std::get<umd::DiscImage::Entry>(resolved));
    trace("getstat " + path);
    return 0;
}

std::int32_t IoManager::open_directory(const std::string &path) {
    const auto resolved = resolve(path);
    if (const auto *error = std::get_if<std::uint32_t>(&resolved)) return static_cast<std::int32_t>(*error);
    const auto &entry = std::get<umd::DiscImage::Entry>(resolved);
    if (!entry.directory) return static_cast<std::int32_t>(kNotDirectory);
    File directory{true, false, entry};
    // The PSP lists "." and ".." first.
    directory.listing = {{".", entry.sector, entry.size, true}, {"..", entry.sector, entry.size, true}};
    try {
        for (auto &child : disc_->list(entry)) directory.listing.push_back(std::move(child));
    } catch (const psprecomp::Error &error) {
        runtime_->stop(std::string("Disc directory listing failed: ") + error.what());
        return static_cast<std::int32_t>(kBadFile);
    }
    trace("dopen " + path);
    return allocate(std::move(directory));
}

std::int32_t IoManager::read_directory(std::int32_t fd, std::uint32_t address) {
    File *directory = file(fd);
    if (directory == nullptr || !directory->directory) return static_cast<std::int32_t>(kBadFile);
    if (!runtime_->memory().contains(address, kDirentNameOffset + 256u)) return static_cast<std::int32_t>(kInvalidArgument);
    if (directory->next_entry >= directory->listing.size()) return 0;
    const auto &entry = directory->listing[directory->next_entry++];
    write_stat(address, entry);
    auto &memory = runtime_->memory();
    memory.zero(address + kDirentNameOffset, 256u);
    for (std::size_t i = 0; i < entry.name.size() && i < 255u; ++i)
        memory.store8(address + kDirentNameOffset + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(entry.name[i]));
    return 1;
}

std::int32_t IoManager::close_directory(std::int32_t fd) {
    File *directory = file(fd);
    if (directory == nullptr || !directory->directory) return static_cast<std::int32_t>(kBadFile);
    files_.erase(fd);
    return 0;
}

std::int32_t IoManager::ioctl(std::int32_t fd, std::uint32_t command, std::uint32_t out_address,
                               std::uint32_t out_length) {
    File *open_file = file(fd);
    if (open_file == nullptr || open_file->directory || open_file->open_failed) return static_cast<std::int32_t>(kBadFile);
    auto &memory = runtime_->memory();
    switch (command) {
    case 0x01020006u:  // File start sector.
        if (out_length < 4u || !memory.contains(out_address, 4u)) return static_cast<std::int32_t>(kInvalidArgument);
        memory.store32(out_address, open_file->entry.sector);
        return 0;
    case 0x01020007u:  // File size in bytes.
        if (out_length < 8u || !memory.contains(out_address, 8u)) return static_cast<std::int32_t>(kInvalidArgument);
        memory.store32(out_address, open_file->entry.size);
        memory.store32(out_address + 4u, 0u);
        return 0;
    default:
        runtime_->stop("sceIoIoctl command " + psprecomp::hex32(command) + " is not modeled yet");
        return static_cast<std::int32_t>(kInvalidArgument);
    }
}

std::int32_t IoManager::devctl(const std::string &device, std::uint32_t command, std::uint32_t out_address,
                                std::uint32_t out_length) {
    const std::string name = lower(device);
    if ((name == "ms0:" || name == "fatms0:") && command == 0x02425823u) {  // Is a medium inserted?
        if (out_length < 4u || !runtime_->memory().contains(out_address, 4u)) return static_cast<std::int32_t>(kInvalidArgument);
        runtime_->memory().store32(out_address, 1u);
        trace("devctl " + device + " medium inserted");
        return 0;
    }
    runtime_->stop("sceIoDevctl " + device + " command " + psprecomp::hex32(command) + " is not modeled yet");
    return static_cast<std::int32_t>(kNoDevice);
}

std::int32_t IoManager::open_async(const std::string &path, std::uint32_t flags) {
    const auto result = open(path, flags);
    if (runtime_->stopped()) return result;
    if (result >= 0) {
        files_.at(result).async_result = result;
        return result;
    }
    // A failed asynchronous open still returns a descriptor carrying the error.
    File failed{false, true};
    failed.async_result = result;
    failed.close_after_collect = true;
    return allocate(std::move(failed));
}

std::int32_t IoManager::read_async(std::int32_t fd, std::uint32_t address, std::uint32_t size) {
    File *open_file = file(fd);
    if (open_file == nullptr || open_file->directory) return static_cast<std::int32_t>(kBadFile);
    if (open_file->async_result) return static_cast<std::int32_t>(kAsyncBusy);
    const auto result = read(fd, address, size);
    files_.at(fd).async_result = result;
    return 0;
}

std::int32_t IoManager::close_async(std::int32_t fd) {
    File *open_file = file(fd);
    if (open_file == nullptr || open_file->directory) return static_cast<std::int32_t>(kBadFile);
    if (open_file->async_result) return static_cast<std::int32_t>(kAsyncBusy);
    open_file->async_result = 0;
    open_file->close_after_collect = true;
    return 0;
}

std::int32_t IoManager::collect_async(std::int32_t fd, std::uint32_t result_address) {
    File *open_file = file(fd);
    if (open_file == nullptr) return static_cast<std::int32_t>(kBadFile);
    if (!open_file->async_result) return static_cast<std::int32_t>(kNoAsync);
    if (result_address != 0u) {
        if (!runtime_->memory().contains(result_address, 8u)) return static_cast<std::int32_t>(kInvalidArgument);
        const auto value = static_cast<std::uint64_t>(*open_file->async_result);
        runtime_->memory().store32(result_address, static_cast<std::uint32_t>(value));
        runtime_->memory().store32(result_address + 4u, static_cast<std::uint32_t>(value >> 32u));
    }
    open_file->async_result.reset();
    if (open_file->close_after_collect) files_.erase(fd);
    return 0;
}

void register_io(IoManager &io, psprecomp::Runtime &runtime) {
    const auto path_at = [](psprecomp::Runtime &rt, std::uint32_t address) -> std::optional<std::string> {
        try { return rt.memory().read_c_string(address, 256u); }
        catch (const psprecomp::Error &) { return std::nullopt; }
    };
    const auto set64 = [](psprecomp::AllegrexContext &ctx, std::int64_t value) {
        ctx.set_gpr(2u, static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)));
        ctx.set_gpr(3u, static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) >> 32u));
    };
    runtime.register_hle("IoFileMgrForUser", 0x109F50BCu,  // sceIoOpen
        [&io, path_at](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto path = path_at(rt, ctx.gpr[4]);
            ctx.set_gpr(2u, path ? static_cast<std::uint32_t>(io.open(*path, ctx.gpr[5])) : kInvalidArgument);
        });
    runtime.register_hle("IoFileMgrForUser", 0x89AA9906u,  // sceIoOpenAsync
        [&io, path_at](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto path = path_at(rt, ctx.gpr[4]);
            ctx.set_gpr(2u, path ? static_cast<std::uint32_t>(io.open_async(*path, ctx.gpr[5])) : kInvalidArgument);
        });
    runtime.register_hle("IoFileMgrForUser", 0x810C4BC3u,  // sceIoClose
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.close(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("IoFileMgrForUser", 0xFF5940B6u,  // sceIoCloseAsync
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.close_async(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("IoFileMgrForUser", 0x6A638D83u,  // sceIoRead
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.read(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("IoFileMgrForUser", 0xA0B5A7C2u,  // sceIoReadAsync
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.read_async(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("IoFileMgrForUser", 0x42EC03ACu,  // sceIoWrite
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.write(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5], ctx.gpr[6])));
        });
    runtime.register_hle("IoFileMgrForUser", 0x27EB27B8u,  // sceIoLseek(fd, offset in a2:a3, whence in t0)
        [&io, set64](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            const auto offset = static_cast<std::int64_t>(static_cast<std::uint64_t>(ctx.gpr[6]) |
                                                          (static_cast<std::uint64_t>(ctx.gpr[7]) << 32u));
            set64(ctx, io.seek(static_cast<std::int32_t>(ctx.gpr[4]), offset, ctx.gpr[8]));
        });
    runtime.register_hle("IoFileMgrForUser", 0xACE946E8u,  // sceIoGetstat
        [&io, path_at](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto path = path_at(rt, ctx.gpr[4]);
            ctx.set_gpr(2u, path ? static_cast<std::uint32_t>(io.get_stat(*path, ctx.gpr[5])) : kInvalidArgument);
        });
    runtime.register_hle("IoFileMgrForUser", 0xB29DDF9Cu,  // sceIoDopen
        [&io, path_at](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto path = path_at(rt, ctx.gpr[4]);
            ctx.set_gpr(2u, path ? static_cast<std::uint32_t>(io.open_directory(*path)) : kInvalidArgument);
        });
    runtime.register_hle("IoFileMgrForUser", 0xE3EB004Cu,  // sceIoDread
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.read_directory(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5])));
        });
    runtime.register_hle("IoFileMgrForUser", 0xEB092469u,  // sceIoDclose
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.close_directory(static_cast<std::int32_t>(ctx.gpr[4]))));
        });
    runtime.register_hle("IoFileMgrForUser", 0x63632449u,  // sceIoIoctl(fd, cmd, in, inlen, out in t0, outlen in t1)
        [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            ctx.set_gpr(2u, static_cast<std::uint32_t>(io.ioctl(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5],
                                                                ctx.gpr[8], ctx.gpr[9])));
        });
    runtime.register_hle("IoFileMgrForUser", 0x54F5FB11u,  // sceIoDevctl(dev, cmd, in, inlen, out in t0, outlen in t1)
        [&io, path_at](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const auto device = path_at(rt, ctx.gpr[4]);
            ctx.set_gpr(2u, device ? static_cast<std::uint32_t>(io.devctl(*device, ctx.gpr[5], ctx.gpr[8], ctx.gpr[9]))
                                   : kInvalidArgument);
        });
    // Operations complete when issued, so waiting and polling both collect at once.
    const auto collect = [&io](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
        ctx.set_gpr(2u, static_cast<std::uint32_t>(io.collect_async(static_cast<std::int32_t>(ctx.gpr[4]), ctx.gpr[5])));
    };
    runtime.register_hle("IoFileMgrForUser", 0xE23EEC33u, collect);  // sceIoWaitAsync
    runtime.register_hle("IoFileMgrForUser", 0x3251EA56u, collect);  // sceIoPollAsync
    runtime.register_hle("StdioForUser", 0x172D316Eu,  // sceKernelStdin
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 0u); });
    runtime.register_hle("StdioForUser", 0xA6BAB2E9u,  // sceKernelStdout
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 1u); });
    runtime.register_hle("StdioForUser", 0xF78BA90Au,  // sceKernelStderr
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 2u); });
}

} // namespace sarusaru::io
