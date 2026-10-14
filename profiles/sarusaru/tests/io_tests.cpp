#include "io/io_manager.hpp"
#include "synthetic_iso.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sarusaru::io;

void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

constexpr std::uint32_t kBuffer = 0x08807000u, kStat = 0x08808000u, kResult = 0x08809000u;
}

int main() {
    const auto path = sarusaru::test::write_image(sarusaru::test::synthetic_image(), "sarusaru_io_tests.iso");
    {
        sarusaru::umd::DiscImage disc(path);
        psprecomp::Runtime runtime;
        IoManager io(disc, runtime);
        auto &memory = runtime.memory();

        // Plain reads and seeks, including across a sector boundary and past EOF.
        const auto fd = io.open("disc0:/PSP_GAME/DATA.BIN", kOpenRead);
        require(fd == 3, "first descriptor is not 3");
        require(io.read(fd, kBuffer, 4u) == 4 && memory.load8(kBuffer + 3u) == 21u, "read from start");
        require(io.seek(fd, 2040, 0u) == 2040 && io.read(fd, kBuffer, 16u) == 16 &&
                    memory.load8(kBuffer + 15u) == static_cast<std::uint8_t>(2055u * 7u),
                "read across sector boundary");
        require(io.seek(fd, -10, 2u) == 2990 && io.read(fd, kBuffer, 100u) == 10 && io.read(fd, kBuffer, 100u) == 0,
                "read at end of file");
        require(io.seek(fd, -1, 0u) == static_cast<std::int32_t>(kInvalidArgument) &&
                    io.seek(fd, 0, 3u) == static_cast<std::int32_t>(kInvalidArgument),
                "invalid seek accepted");
        require(io.close(fd) == 0 && io.close(fd) == static_cast<std::int32_t>(kBadFile), "close");

        require(io.open("disc0:/PSP_GAME/MISSING.BIN", kOpenRead) == static_cast<std::int32_t>(kFileNotFound) &&
                    io.open("disc0:/PSP_GAME/DATA.BIN", 0x0602u) == static_cast<std::int32_t>(kReadOnly) &&
                    io.open("disc0:/PSP_GAME", kOpenRead) == static_cast<std::int32_t>(kFileNotFound),
                "open errors");

        // Sector extents name raw disc ranges.
        const auto lbn = io.open("disc0:/sce_lbn0x15_size0xBB8", kOpenRead);
        require(lbn >= 3 && io.read(lbn, kBuffer, 3000u) == 3000 && memory.load8(kBuffer + 2999u) == static_cast<std::uint8_t>(2999u * 7u),
                "sector extent read");
        require(io.ioctl(lbn, 0x01020007u, kResult, 8u) == 0 && memory.load32(kResult) == 3000u &&
                    memory.load32(kResult + 4u) == 0u && io.ioctl(lbn, 0x01020006u, kResult, 4u) == 0 &&
                    memory.load32(kResult) == 21u && io.ioctl(lbn, 0x01020007u, kResult, 4u) == static_cast<std::int32_t>(kInvalidArgument),
                "file ioctls");
        require(io.close(lbn) == 0 && io.open("disc0:/sce_lbn0x17_size0x1000", kOpenRead) ==
                    static_cast<std::int32_t>(kFileNotFound),
                "extent beyond volume accepted");

        require(io.get_stat("disc0:/PSP_GAME/DATA.BIN", kStat) == 0 && memory.load32(kStat) == 0x216Du &&
                    memory.load32(kStat + 4u) == 0x20u && memory.load32(kStat + 8u) == 3000u && memory.load32(kStat + 64u) == 21u,
                "file stat");
        require(io.get_stat("disc0:/PSP_GAME", kStat) == 0 && memory.load32(kStat) == 0x116Du, "directory stat");

        // Directory listing starts with "." and "..".
        const auto directory = io.open_directory("disc0:/");
        std::vector<std::string> names;
        while (io.read_directory(directory, kStat) == 1) names.push_back(memory.read_c_string(kStat + kDirentNameOffset));
        require(names == std::vector<std::string>{".", "..", "PSP_GAME", "UMD_DATA.BIN"}, "directory listing");
        require(io.close_directory(directory) == 0 && io.open_directory("disc0:/UMD_DATA.BIN") ==
                    static_cast<std::int32_t>(kNotDirectory),
                "directory errors");

        // Asynchronous operations: one outstanding result per descriptor.
        const auto async = io.open_async("disc0:/UMD_DATA.BIN", kOpenRead);
        require(async >= 3 && io.read_async(async, kBuffer, 11u) == static_cast<std::int32_t>(kAsyncBusy), "busy open");
        require(io.collect_async(async, kResult) == 0 && memory.load32(kResult) == static_cast<std::uint32_t>(async),
                "async open result");
        require(io.read_async(async, kBuffer, 64u) == 0 && io.collect_async(async, kResult) == 0 &&
                    memory.load32(kResult) == 11u && memory.load32(kResult + 4u) == 0u && memory.load8(kBuffer) == 'T',
                "async read result");
        require(io.collect_async(async, kResult) == static_cast<std::int32_t>(kNoAsync), "result collected twice");
        require(io.close_async(async) == 0 && io.collect_async(async, kResult) == 0 &&
                    io.close(async) == static_cast<std::int32_t>(kBadFile),
                "async close");
        const auto failed = io.open_async("disc0:/NOPE", kOpenRead);
        require(failed >= 3 && io.collect_async(failed, kResult) == 0 && memory.load32(kResult) == kFileNotFound &&
                    memory.load32(kResult + 4u) == 0xFFFFFFFFu && io.close(failed) == static_cast<std::int32_t>(kBadFile),
                "failed async open");

        // HLE: 64-bit lseek argument and result registers, stdout writes.
        register_io(io, runtime);
        auto &cpu = runtime.cpu();
        for (std::size_t i = 0; i < 25u; ++i) memory.store8(kBuffer + static_cast<std::uint32_t>(i), "disc0:/PSP_GAME/DATA.BIN"[i]);
        cpu.set_gpr(4u, kBuffer); cpu.set_gpr(5u, kOpenRead);
        runtime.invoke_import("IoFileMgrForUser", 0x109F50BCu, cpu);
        const auto hle_fd = cpu.gpr[2];
        cpu.set_gpr(4u, hle_fd); cpu.set_gpr(6u, 100u); cpu.set_gpr(7u, 0u); cpu.set_gpr(8u, 0u);
        runtime.invoke_import("IoFileMgrForUser", 0x27EB27B8u, cpu);
        require(cpu.gpr[2] == 100u && cpu.gpr[3] == 0u, "HLE lseek result");
        for (std::size_t i = 0; i < 3u; ++i) memory.store8(kBuffer + static_cast<std::uint32_t>(i), "ok\n"[i]);
        cpu.set_gpr(4u, 1u); cpu.set_gpr(5u, kBuffer); cpu.set_gpr(6u, 3u);
        runtime.invoke_import("IoFileMgrForUser", 0x42EC03ACu, cpu);
        require(cpu.gpr[2] == 3u, "stdout write");

        require(io.devctl("ms0:", 0x02425823u, kResult, 4u) == 0 && memory.load32(kResult) == 1u &&
                    io.devctl("ms0:", 0x02425823u, kResult, 2u) == static_cast<std::int32_t>(kInvalidArgument),
                "memory stick presence");
        require(io.open("ms0:/PSP/SAVEDATA/x", kOpenRead) == static_cast<std::int32_t>(kNoDevice) && runtime.stopped(),
                "unmodeled device not reported");
    }
    std::filesystem::remove(path);
    return 0;
}
