#include "game_identity.hpp"
#include "startup_hle.hpp"
#include "ctrl/controller.hpp"
#include "display/display.hpp"
#include "kernel/early_kernel.hpp"
#include "umd/disc_image.hpp"
#include "umd/umd_drive.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {
void disable_chaining() {
    // Keep the dispatch budget meaningful for this diagnostic host. The caller
    // additionally enforces a wall-clock timeout for loops inside generated units.
#if defined(_WIN32)
    if (_putenv_s("PSPRECOMP_NO_CHAIN", "1") != 0) {
#else
    if (setenv("PSPRECOMP_NO_CHAIN", "1", 1) != 0) {
#endif
        throw psprecomp::Error("Could not disable AOT chaining for diagnostic execution");
    }
}

// The disc must be the one the executable was prepared from: its UMD_DATA.BIN
// names the supported disc and its BOOT.BIN is byte-identical to BOOT.elf.
void verify_disc(sarusaru::umd::DiscImage &disc) {
    const auto umd_data = disc.find("UMD_DATA.BIN");
    if (!umd_data) throw psprecomp::Error("Disc image has no UMD_DATA.BIN");
    const auto bytes = disc.read_file(*umd_data, 4096u);
    const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const std::string expected = std::string(sarusaru::disc_id.substr(0u, 4u)) + "-" +
                                 std::string(sarusaru::disc_id.substr(4u)) + "|";
    if (!text.starts_with(expected)) throw psprecomp::Error("Disc image is not the supported game disc");
    const auto boot = disc.find("PSP_GAME/SYSDIR/BOOT.BIN");
    if (!boot || boot->size != sarusaru::executable_size ||
        psprecomp::sha256(disc.read_file(*boot, boot->size)) != sarusaru::executable_sha256)
        throw psprecomp::Error("Disc image executable does not match the prepared executable");
}
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: sarusaru_bootstrap <prepared BOOT.elf> <game ISO>\n"
                     "Diagnostic only: no complete PSP kernel, renderer, audio or gameplay yet.\n"
                     "Exit 3 identifies an incomplete runtime or exhausted dispatch budget.\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "Usage: sarusaru_bootstrap <prepared BOOT.elf> <game ISO>\n";
        return 2;
    }
    try {
        if (!std::filesystem::is_regular_file(argv[1]) ||
            std::filesystem::file_size(argv[1]) != sarusaru::executable_size) {
            throw psprecomp::Error("Unsupported executable size; prepare your compatible ISO first");
        }
        const auto elf = psprecomp::Elf32Image::from_file(argv[1]);
        if (psprecomp::sha256(elf.bytes()) != sarusaru::executable_sha256) {
            throw psprecomp::Error("Unsupported executable hash; prepare your compatible ISO first");
        }
        sarusaru::umd::DiscImage disc(argv[2]);
        verify_disc(disc);
        disable_chaining();
        psprecomp::Runtime runtime;
        const auto relocations = elf.load_and_relocate(runtime.memory(), sarusaru::load_base);
        if (relocations.invalid != 0u || relocations.unsupported != 0u) {
            throw psprecomp::Error("Cannot execute an incompletely relocated image");
        }
        const auto module = elf.find_module_info(runtime.memory(), sarusaru::load_base);
        if (!module || module->name != "SARU-P2") {
            throw psprecomp::Error("Expected SARU-P2 module metadata");
        }
        psprecomp::register_generated_functions(runtime);
        sarusaru::StartupHle startup_hle;
        startup_hle.install(runtime);
        sarusaru::kernel::EarlyKernel early_kernel;
        std::uint32_t image_end = sarusaru::load_base;
        for (std::size_t index = 0; index < elf.segments().size(); ++index) {
            const auto &segment = elf.segments()[index];
            if (segment.type != 1u) continue;
            const auto segment_end = static_cast<std::uint64_t>(elf.segment_runtime_address(index, sarusaru::load_base)) +
                segment.memory_size;
            if (segment_end > 0x0A000000u) throw psprecomp::Error("Image exceeds PSP user RAM");
            image_end = std::max(image_end, static_cast<std::uint32_t>(segment_end));
        }
        // Keep the bootstrap stack and its guard area outside guest allocations.
        early_kernel.install(runtime, image_end, 0x09FF6000u);
        sarusaru::kernel::register_early_threadman(early_kernel, runtime);
        sarusaru::umd::UmdDrive umd_drive(disc);
        sarusaru::umd::register_umd(umd_drive, runtime);
        sarusaru::ctrl::Controller controller;
        sarusaru::ctrl::register_ctrl(controller, early_kernel, runtime);
        sarusaru::display::Display display;
        sarusaru::display::register_display(display, early_kernel, runtime);

        // Provisional single-thread module-start context, not a PSP kernel.
        // Stack and arguments are above the main image and within 32 MiB RAM.
        constexpr std::uint32_t stack = 0x09FFE000u;
        // Kernel scratch lies below the module image and cannot be overwritten
        // by a user thread stack allocated from the high end of RAM.
        constexpr std::uint32_t arguments = 0x08100000u;
        constexpr std::string_view launch_path = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
        for (std::size_t i = 0; i < launch_path.size(); ++i) {
            runtime.memory().store8(arguments + static_cast<std::uint32_t>(i),
                                    static_cast<std::uint8_t>(launch_path[i]));
        }
        runtime.memory().store8(arguments + static_cast<std::uint32_t>(launch_path.size()), 0u);
        auto &cpu = runtime.cpu();
        cpu.set_gpr(4u, static_cast<std::uint32_t>(launch_path.size() + 1u));
        cpu.set_gpr(5u, arguments);
        cpu.set_gpr(28u, module->gp);
        cpu.set_gpr(29u, stack);
        // Module startup returns through the kernel's thread-exit stub. This
        // keeps an unsupported scheduler transition explicit instead of
        // dispatching guest PC zero as though it were executable code.
        cpu.set_gpr(31u, sarusaru::kernel::kThreadExitStub);
        early_kernel.begin_main_thread(cpu, "module_start", 0x20u);

        const auto entry = elf.runtime_entry(sarusaru::load_base);
        std::cout << "SaruSaru diagnostic AOT bootstrap (not playable)\n"
                  << "Module: " << module->name << "; entry: " << psprecomp::hex32(entry)
                  << "; registered entries: " << runtime.function_count() << '\n' << std::flush;
        runtime.run(entry, 100'000u);
        std::cout << "Stopped at " << psprecomp::hex32(cpu.pc) << ": "
                   << runtime.stop_reason() << '\n'
                   << "Guest time: " << early_kernel.now_us() << " us (virtual)\n"
                   << "Stop arguments: a0=" << psprecomp::hex32(cpu.gpr[4])
                   << " a1=" << psprecomp::hex32(cpu.gpr[5])
                   << " a2=" << psprecomp::hex32(cpu.gpr[6])
                   << " a3=" << psprecomp::hex32(cpu.gpr[7]) << '\n'
                   << "Observed startup values: compiled SDK " << psprecomp::hex32(startup_hle.compiled_sdk_version())
                   << ", compiler " << psprecomp::hex32(startup_hle.compiler_version()) << '\n'
                   << "Only explicitly modeled imports are bound; every other missing call stops execution.\n";
        return 3;
    } catch (const std::exception &error) {
        std::cerr << "sarusaru_bootstrap: " << error.what() << '\n';
        return 1;
    }
}
