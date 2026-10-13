#include "game_identity.hpp"
#include "psprecomp/common.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"
#include "psprecomp/sha256.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: sarusaru_bootstrap <prepared BOOT.elf>\n"
                     "Diagnostic only: no PSP kernel, renderer, audio or gameplay yet.\n"
                     "Exit 3 identifies an incomplete runtime or exhausted dispatch budget.\n";
        return 0;
    }
    if (argc != 2) {
        std::cerr << "Usage: sarusaru_bootstrap <prepared BOOT.elf>\n";
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

        // Provisional single-thread module-start context, not a PSP kernel.
        // Stack and arguments are above the main image and within 32 MiB RAM.
        constexpr std::uint32_t stack = 0x09FFE000u;
        constexpr std::uint32_t arguments = 0x09FFF000u;
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
        cpu.set_gpr(31u, 0u);

        const auto entry = elf.runtime_entry(sarusaru::load_base);
        std::cout << "SaruSaru diagnostic AOT bootstrap (not playable)\n"
                  << "Module: " << module->name << "; entry: " << psprecomp::hex32(entry)
                  << "; registered entries: " << runtime.function_count() << '\n' << std::flush;
        runtime.run(entry, 100'000u);
        std::cout << "Stopped at " << psprecomp::hex32(cpu.pc) << ": "
                  << runtime.stop_reason() << '\n'
                  << "No missing PSP imports are stubbed as success.\n";
        return 3;
    } catch (const std::exception &error) {
        std::cerr << "sarusaru_bootstrap: " << error.what() << '\n';
        return 1;
    }
}
